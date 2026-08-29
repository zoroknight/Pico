#include "Pico/Core/Profiler.h"
#include "Pico/Core/MemoryTracker.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Replication.h"
#include "Pico/Engine/TickTaskManager.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"
#include "Pico/Object/ObjectRegistry.h"
#include "Pico/Object/Class.h"
#include "Pico/Object/Property.h"
#include "Pico/Net/NetConnection.h"
#include "Pico/Object/GarbageCollection.h"
#include "Pico/Object/ReferenceCollector.h"
#include "Pico/Object/ReflectionMacros.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <numeric>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifndef PICO_BENCHMARK_BUILD_CONFIG
#define PICO_BENCHMARK_BUILD_CONFIG "Unknown"
#endif

namespace
{
class PBenchmarkObject final : public Pico::PObject
{
    PICO_DECLARE_CLASS(PBenchmarkObject, Pico::PObject)

public:
    void AddReference(Pico::PObject* Object)
    {
        if (Object != nullptr) References.push_back(Object->GetHandle());
    }

protected:
    explicit PBenchmarkObject(const Pico::FObjectConstructionParams& Params)
        : PObject(Params)
    {
    }

    void AddReferencedObjects(Pico::FReferenceCollector& Collector) const override
    {
        Collector.AddReferencedHandles(References);
    }

private:
    std::vector<Pico::FObjectHandle> References;
};

PICO_DEFINE_CLASS_NO_PROPERTIES(PBenchmarkObject)

class PBenchmarkActor final : public Pico::PActor
{
    PICO_DECLARE_CLASS(PBenchmarkActor, Pico::PActor)

public:
    void SetValue(Pico::int32 InValue)
    {
        if (Value == InValue) return;
        Value = InValue;
        MarkReplicatedPropertyDirty(Pico::FName("Value"));
    }

protected:
    explicit PBenchmarkActor(const Pico::FObjectConstructionParams& Params)
        : PActor(Params)
    {
        SetReplicates(true);
    }

private:
    Pico::int32 Value = 0;
};

PICO_DEFINE_CLASS(PBenchmarkActor)

bool PBenchmarkActor::RegisterProperties(Pico::PClass& Class)
{
    Pico::FPropertyMetadata Metadata;
    Metadata.Flags = Pico::EPropertyFlags::Transient
        | Pico::EPropertyFlags::Replicated;
    std::vector<Pico::PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, Value, Metadata);
    return Class.AddProperties(std::move(Properties));
}

class FBenchmarkTick final : public Pico::FTickFunction
{
public:
    std::uint64_t ExecuteCount = 0;

private:
    void ExecuteTick(float) override { ++ExecuteCount; }
};

struct FBenchmarkRecord
{
    std::string Suite;
    std::string Case;
    std::size_t Scale = 0;
    std::size_t Operations = 0;
    std::string Parameters;
    std::vector<std::uint64_t> SamplesMicroseconds;
    std::vector<std::size_t> BytesPerFrame;
    struct FRuntimeMetrics
    {
        std::uint64_t ActorsSkipped = 0;
        std::uint64_t DirtyActors = 0;
        std::uint64_t DirtyProperties = 0;
        std::uint64_t EncodedProperties = 0;
        std::uint64_t SchemaCacheHits = 0;
        std::uint64_t SchemaCacheMisses = 0;
        std::uint64_t ChannelIndexHits = 0;
        std::uint64_t GatherNanoseconds = 0;
        std::uint64_t CompareNanoseconds = 0;
        std::uint64_t SerializeNanoseconds = 0;
        std::uint64_t QueueNanoseconds = 0;
        std::uint64_t GCRootScanNanoseconds = 0;
        std::uint64_t GCMarkNanoseconds = 0;
        std::uint64_t GCUnreachableSortNanoseconds = 0;
        std::uint64_t GCDestroyNanoseconds = 0;
        std::uint64_t GCStrongReferenceLayouts = 0;
        std::uint64_t GCStrongReferenceProperties = 0;
        std::uint64_t GCScratchPeakBytes = 0;
        std::uint64_t GCScratchReservedBytes = 0;
        std::uint64_t GCScratchGrowthCount = 0;
    };
    std::vector<FRuntimeMetrics> RuntimeMetrics;
};

std::uint64_t Percentile(
    const std::vector<std::uint64_t>& Samples,
    double Quantile)
{
    if (Samples.empty()) return 0;
    std::vector<std::uint64_t> Sorted = Samples;
    std::sort(Sorted.begin(), Sorted.end());
    const std::size_t Index = std::min(Sorted.size() - 1,
        static_cast<std::size_t>(std::ceil(Quantile * Sorted.size())) - 1);
    return Sorted[Index];
}

class FBenchmarkReport
{
public:
    void Add(
        std::string Suite,
        std::string Case,
        std::size_t Scale,
        std::size_t Operations,
        std::uint64_t DurationMicroseconds,
        std::string Parameters = {},
        std::size_t BytesPerFrame = 0,
        FBenchmarkRecord::FRuntimeMetrics RuntimeMetrics = {})
    {
        auto Existing = std::find_if(Records.begin(), Records.end(),
            [&](const FBenchmarkRecord& Record)
            {
                return Record.Suite == Suite && Record.Case == Case
                    && Record.Scale == Scale && Record.Operations == Operations
                    && Record.Parameters == Parameters;
            });
        if (Existing == Records.end())
        {
            Records.push_back({std::move(Suite), std::move(Case), Scale,
                Operations, std::move(Parameters), {DurationMicroseconds},
                {BytesPerFrame}, {RuntimeMetrics}});
            return;
        }
        Existing->SamplesMicroseconds.push_back(DurationMicroseconds);
        Existing->BytesPerFrame.push_back(BytesPerFrame);
        Existing->RuntimeMetrics.push_back(RuntimeMetrics);
    }

    bool Write(
        const std::filesystem::path& OutputRoot,
        bool bFull,
        const std::vector<Pico::FProfileAggregate>& Aggregates,
        const std::vector<Pico::FMemorySnapshot>& MemorySnapshots) const
    {
        using FJson = nlohmann::json;
        std::error_code Error;
        std::filesystem::create_directories(OutputRoot, Error);
        if (Error) return false;
        std::ofstream Json(OutputRoot / "PicoRuntimeBenchmarks.json",
            std::ios::binary | std::ios::trunc);
        std::ofstream Csv(OutputRoot / "PicoRuntimeBenchmarks.csv",
            std::ios::binary | std::ios::trunc);
        std::ofstream MemoryCsv(OutputRoot / "PicoRuntimeMemory.csv",
            std::ios::binary | std::ios::trunc);
        if (!Json || !Csv || !MemoryCsv) return false;
        FJson JsonRecords = FJson::array();
        Csv << "format_version,preset,suite,case,scale,operations,samples,p50_us,p95_us,max_us,mean_us,ns_per_operation,bytes_per_frame,actors_skipped,dirty_actors,dirty_properties,encoded_properties,schema_cache_hits,schema_cache_misses,channel_index_hits,gather_p50_ns,compare_p50_ns,serialize_p50_ns,queue_p50_ns,gc_root_scan_p50_ns,gc_mark_p50_ns,gc_unreachable_sort_p50_ns,gc_destroy_p50_ns,gc_strong_reference_layouts,gc_strong_reference_properties,gc_scratch_peak_bytes,gc_scratch_reserved_bytes,gc_scratch_growth_count,parameters\n";
        for (const FBenchmarkRecord& Record : Records)
        {
            const std::uint64_t P50 = Percentile(Record.SamplesMicroseconds, 0.50);
            const std::uint64_t P95 = Percentile(Record.SamplesMicroseconds, 0.95);
            const std::uint64_t Max = *std::max_element(
                Record.SamplesMicroseconds.begin(), Record.SamplesMicroseconds.end());
            const double Mean = static_cast<double>(std::accumulate(
                Record.SamplesMicroseconds.begin(), Record.SamplesMicroseconds.end(),
                std::uint64_t {0})) / Record.SamplesMicroseconds.size();
            const double NanosecondsPerOperation = Record.Operations == 0 ? 0.0
                : Mean * 1000.0
                    / static_cast<double>(Record.Operations);
            const std::size_t Bytes = Record.BytesPerFrame.empty() ? 0
                : *std::max_element(Record.BytesPerFrame.begin(),
                    Record.BytesPerFrame.end());
            const auto MetricValues = [&Record](auto Member)
            {
                std::vector<std::uint64_t> Values;
                Values.reserve(Record.RuntimeMetrics.size());
                for (const auto& Metrics : Record.RuntimeMetrics)
                    Values.push_back(Metrics.*Member);
                return Values;
            };
            const auto MetricP50 = [&](auto Member)
            {
                return Percentile(MetricValues(Member), 0.50);
            };
            const FJson RuntimeMetrics = {
                {"actors_skipped", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::ActorsSkipped)},
                {"dirty_actors", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::DirtyActors)},
                {"dirty_properties", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::DirtyProperties)},
                {"encoded_properties", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::EncodedProperties)},
                {"schema_cache_hits", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::SchemaCacheHits)},
                {"schema_cache_misses", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::SchemaCacheMisses)},
                {"channel_index_hits", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::ChannelIndexHits)},
                {"gather_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GatherNanoseconds)},
                {"compare_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::CompareNanoseconds)},
                {"serialize_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::SerializeNanoseconds)},
                {"queue_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::QueueNanoseconds)},
                {"gc_root_scan_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCRootScanNanoseconds)},
                {"gc_mark_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCMarkNanoseconds)},
                {"gc_unreachable_sort_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCUnreachableSortNanoseconds)},
                {"gc_destroy_p50_ns", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCDestroyNanoseconds)},
                {"gc_strong_reference_layouts", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCStrongReferenceLayouts)},
                {"gc_strong_reference_properties", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCStrongReferenceProperties)},
                {"gc_scratch_peak_bytes", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCScratchPeakBytes)},
                {"gc_scratch_reserved_bytes", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCScratchReservedBytes)},
                {"gc_scratch_growth_count", MetricP50(
                    &FBenchmarkRecord::FRuntimeMetrics::GCScratchGrowthCount)}};
            JsonRecords.push_back({{"suite", Record.Suite}, {"case", Record.Case},
                {"scale", Record.Scale}, {"operations", Record.Operations},
                {"sample_count", Record.SamplesMicroseconds.size()},
                {"p50_us", P50}, {"p95_us", P95}, {"max_us", Max},
                {"mean_us", Mean}, {"ns_per_operation", NanosecondsPerOperation},
                {"bytes_per_frame", Bytes}, {"parameters", Record.Parameters},
                {"runtime_metrics", RuntimeMetrics}});
            Csv << "3," << (bFull ? "full" : "quick") << ','
                << Record.Suite << ',' << Record.Case << ',' << Record.Scale
                << ',' << Record.Operations << ',' << Record.SamplesMicroseconds.size()
                << ',' << P50 << ',' << P95 << ',' << Max << ',' << Mean
                << ',' << std::fixed << std::setprecision(3)
                << NanosecondsPerOperation << ',' << Bytes << ','
                << RuntimeMetrics["actors_skipped"] << ','
                << RuntimeMetrics["dirty_actors"] << ','
                << RuntimeMetrics["dirty_properties"] << ','
                << RuntimeMetrics["encoded_properties"] << ','
                << RuntimeMetrics["schema_cache_hits"] << ','
                << RuntimeMetrics["schema_cache_misses"] << ','
                << RuntimeMetrics["channel_index_hits"] << ','
                << RuntimeMetrics["gather_p50_ns"] << ','
                << RuntimeMetrics["compare_p50_ns"] << ','
                << RuntimeMetrics["serialize_p50_ns"] << ','
                << RuntimeMetrics["queue_p50_ns"] << ','
                << RuntimeMetrics["gc_root_scan_p50_ns"] << ','
                << RuntimeMetrics["gc_mark_p50_ns"] << ','
                << RuntimeMetrics["gc_unreachable_sort_p50_ns"] << ','
                << RuntimeMetrics["gc_destroy_p50_ns"] << ','
                << RuntimeMetrics["gc_strong_reference_layouts"] << ','
                << RuntimeMetrics["gc_strong_reference_properties"] << ','
                << RuntimeMetrics["gc_scratch_peak_bytes"] << ','
                << RuntimeMetrics["gc_scratch_reserved_bytes"] << ','
                << RuntimeMetrics["gc_scratch_growth_count"] << ','
                << Record.Parameters << '\n';
        }
        FJson ProfileScopes = FJson::array();
        for (const Pico::FProfileAggregate& Aggregate : Aggregates)
            ProfileScopes.push_back({{"name", Aggregate.Name},
                {"count", Aggregate.Count}, {"total_us", Aggregate.TotalMicroseconds},
                {"min_us", Aggregate.MinMicroseconds},
                {"max_us", Aggregate.MaxMicroseconds}});
        FJson MemoryCategories = FJson::array();
        MemoryCsv << "format_version,category,current_bytes,reserved_bytes,peak_bytes,element_count,growth_count\n";
        for (const Pico::FMemorySnapshot& Snapshot : MemorySnapshots)
        {
            const std::string Name(Pico::GetMemoryTagName(Snapshot.Tag));
            MemoryCategories.push_back({{"category", Name},
                {"current_bytes", Snapshot.CurrentBytes},
                {"reserved_bytes", Snapshot.ReservedBytes},
                {"peak_bytes", Snapshot.PeakBytes},
                {"element_count", Snapshot.ElementCount},
                {"growth_count", Snapshot.GrowthCount}});
            MemoryCsv << "3," << Name << ',' << Snapshot.CurrentBytes << ','
                << Snapshot.ReservedBytes << ',' << Snapshot.PeakBytes << ','
                << Snapshot.ElementCount << ',' << Snapshot.GrowthCount << '\n';
        }
        const FJson Baseline = {{"format_version", 3},
            {"build_config", PICO_BENCHMARK_BUILD_CONFIG},
            {"preset", bFull ? "full" : "quick"},
            {"environment", {{"hardware_threads", std::thread::hardware_concurrency()},
                {"pointer_size", sizeof(void*)}}},
            {"core_type_sizes", {{"PObject", sizeof(Pico::PObject)},
                {"PClass", sizeof(Pico::PClass)}, {"PProperty", sizeof(Pico::PProperty)},
                {"PActor", sizeof(Pico::PActor)},
                {"PActorComponent", sizeof(Pico::PActorComponent)},
                {"FObjectHandle", sizeof(Pico::FObjectHandle)},
                {"FTickFunction", sizeof(Pico::FTickFunction)},
                {"FReplicationFieldDescriptor", sizeof(Pico::FReplicationFieldDescriptor)},
                {"FActorChannelSnapshot", sizeof(Pico::FActorChannelSnapshot)},
                {"FNetConnection", sizeof(Pico::FNetConnection)}}},
            {"records", std::move(JsonRecords)}, {"profile_scopes", ProfileScopes},
            {"memory_categories", std::move(MemoryCategories)},
            {"full_scale_contract", {{"object", {1000, 10000, 100000}},
                {"tick", {1000, 10000}}, {"replication", {100, 1000}},
                {"dirty_ratio_percent", {1, 10, 100}}}}};
        Json << Baseline.dump(2) << '\n';
        std::ofstream RuntimeBaseline(OutputRoot / "RuntimeBaseline.json",
            std::ios::binary | std::ios::trunc);
        if (!RuntimeBaseline) return false;
        RuntimeBaseline << Baseline.dump(2) << '\n';
        return static_cast<bool>(Json) && static_cast<bool>(Csv)
            && static_cast<bool>(MemoryCsv);
    }

private:
    std::vector<FBenchmarkRecord> Records;
};

std::uint64_t MeasureMicroseconds(const std::function<void()>& Function)
{
    const auto Start = std::chrono::steady_clock::now();
    Function();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - Start).count());
}

bool ValidateMemorySnapshots(
    const std::vector<Pico::FMemorySnapshot>& Snapshots,
    std::string& OutError)
{
    const std::size_t ExpectedCount =
        static_cast<std::size_t>(Pico::EMemoryTag::Count);
    if (Snapshots.size() != ExpectedCount)
    {
        OutError = "Memory snapshot category count is incomplete";
        return false;
    }
    for (std::size_t Index = 0; Index < Snapshots.size(); ++Index)
    {
        const Pico::FMemorySnapshot& Snapshot = Snapshots[Index];
        if (Snapshot.Tag != static_cast<Pico::EMemoryTag>(Index)
            || Pico::GetMemoryTagName(Snapshot.Tag) == "Unknown")
        {
            OutError = "Memory snapshot category order or name is invalid";
            return false;
        }
        if (Snapshot.CurrentBytes > Snapshot.ReservedBytes)
        {
            OutError = "Memory snapshot current bytes exceed reserved bytes";
            return false;
        }
        if (Snapshot.PeakBytes == 0 || Snapshot.GrowthCount == 0)
        {
            OutError = "Memory snapshot did not observe category activity: "
                + std::string(Pico::GetMemoryTagName(Snapshot.Tag));
            return false;
        }
    }
    return true;
}

void RunObjectBenchmarks(FBenchmarkReport& Report, bool bFull)
{
    const std::vector<std::size_t> Scales = bFull
        ? std::vector<std::size_t> {1000, 10000, 100000}
        : std::vector<std::size_t> {1000};
    for (const std::size_t Scale : Scales)
    {
        std::vector<PBenchmarkObject*> Objects;
        Objects.reserve(Scale);
        const std::uint64_t CreateDuration = MeasureMicroseconds([&]()
        {
            for (std::size_t Index = 0; Index < Scale; ++Index)
                Objects.push_back(Pico::NewObject<PBenchmarkObject>(
                    nullptr, "BenchObject_" + std::to_string(Index)));
        });
        Report.Add("Object", "Create", Scale, Scale, CreateDuration);
        Pico::FObjectRegistry::PublishMemoryStatistics();

        std::size_t FoundCount = 0;
        const std::uint64_t FindDuration = MeasureMicroseconds([&]()
        {
            for (std::size_t Index = 0; Index < Scale; ++Index)
                FoundCount += Pico::FindObject(nullptr,
                    Pico::FName("BenchObject_" + std::to_string(Index))) != nullptr;
        });
        Report.Add("Object", "Find", Scale, Scale, FindDuration,
            "found=" + std::to_string(FoundCount));

        const std::uint64_t RenameDuration = MeasureMicroseconds([&]()
        {
            for (std::size_t Index = 0; Index < Objects.size(); ++Index)
                Pico::RenameObject(Objects[Index],
                    Pico::FName("RenamedObject_" + std::to_string(Index)));
        });
        Report.Add("Object", "Rename", Scale, Scale, RenameDuration);

        const std::uint64_t DestroyDuration = MeasureMicroseconds([&]()
        {
            for (PBenchmarkObject* Object : Objects) Pico::DestroyObject(Object);
        });
        Report.Add("Object", "Destroy", Scale, Scale, DestroyDuration);
        Pico::FObjectRegistry::PublishMemoryStatistics();

        const Pico::FObjectHierarchyIndexStats HierarchyBefore =
            Pico::FObjectRegistry::GetHierarchyIndexStats();
        PBenchmarkObject* HierarchyRoot = Pico::NewObject<PBenchmarkObject>(
            nullptr, "HierarchyRoot_" + std::to_string(Scale));
        std::vector<PBenchmarkObject*> Children;
        Children.reserve(Scale);
        const std::uint64_t HierarchyCreateDuration = MeasureMicroseconds([&]()
        {
            for (std::size_t Index = 0; Index < Scale; ++Index)
                Children.push_back(Pico::NewObject<PBenchmarkObject>(
                    HierarchyRoot, "HierarchyChild_" + std::to_string(Index)));
        });
        const Pico::FObjectHierarchyIndexStats HierarchyPopulated =
            Pico::FObjectRegistry::GetHierarchyIndexStats();
        Pico::FObjectRegistry::PublishMemoryStatistics();
        const std::size_t AddedStorage =
            HierarchyPopulated.EstimatedStorageBytes
                >= HierarchyBefore.EstimatedStorageBytes
            ? HierarchyPopulated.EstimatedStorageBytes
                - HierarchyBefore.EstimatedStorageBytes
            : 0;
        const std::string HierarchyParameters =
            "parent_entries=" + std::to_string(
                HierarchyPopulated.ParentEntryCount
                    - HierarchyBefore.ParentEntryCount)
            + ";child_relations=" + std::to_string(
                HierarchyPopulated.ChildRelationCount
                    - HierarchyBefore.ChildRelationCount)
            + ";estimated_storage_bytes=" + std::to_string(AddedStorage);
        Report.Add("Object", "HierarchyCreate", Scale, Scale,
            HierarchyCreateDuration, HierarchyParameters);

        const std::uint64_t HierarchyDestroyDuration = MeasureMicroseconds([&]()
        {
            Pico::DestroyObjectTree(HierarchyRoot);
        });
        Report.Add("Object", "HierarchyDestroy", Scale, Scale + 1,
            HierarchyDestroyDuration, HierarchyParameters);
        Pico::FObjectRegistry::PublishMemoryStatistics();
    }
}

void RunTickBenchmarks(FBenchmarkReport& Report, bool bFull)
{
    const std::vector<std::size_t> Scales = bFull
        ? std::vector<std::size_t> {1000, 10000}
        : std::vector<std::size_t> {1000};
    for (const std::size_t Scale : Scales)
    {
        Pico::FTickTaskManager Manager;
        std::vector<PBenchmarkObject*> Owners;
        std::vector<std::unique_ptr<FBenchmarkTick>> Ticks;
        Owners.reserve(Scale);
        Ticks.reserve(Scale);
        for (std::size_t Index = 0; Index < Scale; ++Index)
        {
            PBenchmarkObject* Owner = Pico::NewObject<PBenchmarkObject>(
                nullptr, "TickOwner_" + std::to_string(Index));
            auto Tick = std::make_unique<FBenchmarkTick>();
            Tick->SetCanEverTick(true);
            Tick->SetTickEnabled(true);
            Manager.RegisterTickFunction(*Tick, Owner);
            Owners.push_back(Owner);
            Ticks.push_back(std::move(Tick));
        }
        const std::uint64_t StaticDuration = MeasureMicroseconds([&]()
        {
            Manager.Tick(1.0f / 60.0f);
        });
        Report.Add("Tick", "StaticSchedule", Scale, Scale, StaticDuration);

        const Pico::uint64 BuildCountBeforeCachedFrames =
            Manager.GetScheduleBuildCount(Pico::ETickGroup::PrePhysics);
        constexpr std::size_t CachedFrameCount = 60;
        const std::uint64_t CachedDuration = MeasureMicroseconds([&]()
        {
            for (std::size_t Frame = 0; Frame < CachedFrameCount; ++Frame)
                Manager.Tick(1.0f / 60.0f);
        });
        const Pico::uint64 CachedRebuildCount =
            Manager.GetScheduleBuildCount(Pico::ETickGroup::PrePhysics)
                - BuildCountBeforeCachedFrames;
        Report.Add("Tick", "CachedStaticFrames", Scale,
            Scale * CachedFrameCount, CachedDuration,
            "frames=" + std::to_string(CachedFrameCount)
                + ";schedule_rebuilds=" + std::to_string(CachedRebuildCount));

        const std::uint64_t DependencyDuration = MeasureMicroseconds([&]()
        {
            for (std::size_t Index = 1; Index < Ticks.size(); ++Index)
                Ticks[Index]->AddPrerequisite(*Ticks[Index - 1]);
            Manager.Tick(1.0f / 60.0f);
            for (std::size_t Index = 1; Index < Ticks.size(); ++Index)
                Ticks[Index]->ClearPrerequisites();
        });
        Report.Add("Tick", "DependencyChange", Scale, Scale,
            DependencyDuration, "chain=1");
        Manager.Reset();
        for (PBenchmarkObject* Owner : Owners) Pico::DestroyObject(Owner);
    }
}

void RunGarbageCollectionBenchmark(
    FBenchmarkReport& Report,
    std::size_t Scale,
    std::size_t SurvivalPercent,
    std::size_t ReferenceDensity,
    std::size_t OuterDepth)
{
    std::vector<PBenchmarkObject*> Objects;
    Objects.reserve(Scale);
    PBenchmarkObject* Outer = nullptr;
    for (std::size_t Index = 0; Index < Scale; ++Index)
    {
        if (OuterDepth == 0 || Index % (OuterDepth + 1) == 0) Outer = nullptr;
        PBenchmarkObject* Object = Pico::NewObject<PBenchmarkObject>(
            Outer, "GCObject_" + std::to_string(Index));
        Objects.push_back(Object);
        Outer = Object;
    }
    const std::size_t SurvivorCount = Scale * SurvivalPercent / 100;
    for (std::size_t Index = 0; Index < SurvivorCount; ++Index)
    {
        Pico::AddToRoot(Objects[Index]);
        for (std::size_t Offset = 1;
             Offset <= ReferenceDensity && Index + Offset < SurvivorCount;
             ++Offset)
            Objects[Index]->AddReference(Objects[Index + Offset]);
    }
    Pico::FGarbageCollectionResult Result;
    const std::uint64_t Duration = MeasureMicroseconds([&]()
    {
        Result = Pico::CollectGarbage();
    });
    Report.Add("GC", "Collect", Scale, Scale, Duration,
        "survival=" + std::to_string(SurvivalPercent)
            + ";density=" + std::to_string(ReferenceDensity)
            + ";outer_depth=" + std::to_string(OuterDepth)
            + ";collected=" + std::to_string(Result.CollectedObjectCount),
        0,
        {.GCRootScanNanoseconds = Result.RootScanNanoseconds,
            .GCMarkNanoseconds = Result.MarkNanoseconds,
            .GCUnreachableSortNanoseconds =
                Result.UnreachableSortNanoseconds,
            .GCDestroyNanoseconds = Result.DestroyNanoseconds,
            .GCStrongReferenceLayouts = Result.StrongReferenceLayoutCount,
            .GCStrongReferenceProperties =
                Result.StrongReferencePropertyVisitCount,
            .GCScratchPeakBytes = Result.ScratchPeakBytes,
            .GCScratchReservedBytes = Result.ScratchReservedBytes,
            .GCScratchGrowthCount = Result.ScratchGrowthCount});
    for (std::size_t Index = 0; Index < SurvivorCount; ++Index)
        if (Pico::ResolveObject(Objects[Index]->GetHandle()) == Objects[Index])
            Pico::RemoveFromRoot(Objects[Index]);
    Pico::CollectGarbage();
    Pico::FObjectRegistry::PublishMemoryStatistics();
}

void RunGarbageCollectionBenchmarks(FBenchmarkReport& Report, bool bFull)
{
    RunGarbageCollectionBenchmark(Report, 1000, 10, 0, 0);
    RunGarbageCollectionBenchmark(Report, 1000, 50, 2, 4);
    RunGarbageCollectionBenchmark(Report, 1000, 90, 8, 16);
    if (bFull)
    {
        RunGarbageCollectionBenchmark(Report, 10000, 10, 0, 0);
        RunGarbageCollectionBenchmark(Report, 10000, 50, 2, 4);
        RunGarbageCollectionBenchmark(Report, 10000, 90, 8, 16);
    }
}

void RunReplicationBenchmarks(
    FBenchmarkReport& Report,
    Pico::PWorld& World,
    bool bFull)
{
    const std::vector<std::size_t> Scales = bFull
        ? std::vector<std::size_t> {100, 1000}
        : std::vector<std::size_t> {100};
    for (const std::size_t Scale : Scales)
    {
        std::vector<PBenchmarkActor*> Actors;
        Actors.reserve(Scale);
        for (std::size_t Index = 0; Index < Scale; ++Index)
            Actors.push_back(World.SpawnActor<PBenchmarkActor>(
                "RepActor_" + std::to_string(Scale) + "_" + std::to_string(Index)));

        Pico::FReplicationSystem Replication;
        Replication.SetWorld(&World);
        const Pico::FNetConnectionId Connection {1};
        Pico::uint32 NextReliableId = 1;
        std::vector<Pico::uint32> ReliableIds;
        std::size_t BytesQueued = 0;
        const auto Queue = [&](std::span<const Pico::uint8> Payload,
            Pico::uint32* OutReliableId)
        {
            const Pico::uint32 Id = NextReliableId++;
            if (OutReliableId) *OutReliableId = Id;
            ReliableIds.push_back(Id);
            BytesQueued += Payload.size();
            return true;
        };
        Replication.BeginNetworkFrame();
        Replication.ReplicateServerConnection(Connection, Queue);
        Replication.PublishMemoryStatistics();
        for (const Pico::uint32 Id : ReliableIds)
            Replication.HandleReliableAcknowledged(Connection, Id);

        for (const std::size_t DirtyPercent :
            std::array<std::size_t, 3> {1, 10, 100})
        {
            ReliableIds.clear();
            BytesQueued = 0;
            const std::size_t DirtyCount = std::max<std::size_t>(
                1, Scale * DirtyPercent / 100);
            for (std::size_t Index = 0; Index < DirtyCount; ++Index)
                Actors[Index]->SetValue(static_cast<Pico::int32>(
                    DirtyPercent * 100000 + Index));
            Replication.BeginNetworkFrame();
            const Pico::FReplicationStatistics Before =
                Replication.GetStatistics();
            const std::uint64_t Duration = MeasureMicroseconds([&]()
            {
                Replication.ReplicateServerConnection(Connection, Queue);
            });
            const Pico::FReplicationStatistics After =
                Replication.GetStatistics();
            Replication.PublishMemoryStatistics();
            const auto Delta = [](Pico::uint64 NewValue, Pico::uint64 OldValue)
            {
                return NewValue - OldValue;
            };
            FBenchmarkRecord::FRuntimeMetrics RuntimeMetrics;
            RuntimeMetrics.ActorsSkipped = Delta(
                After.ActorsSkippedUnchanged,
                Before.ActorsSkippedUnchanged);
            RuntimeMetrics.DirtyActors = Delta(
                After.DirtyActors, Before.DirtyActors);
            RuntimeMetrics.DirtyProperties = Delta(
                After.DirtyProperties, Before.DirtyProperties);
            RuntimeMetrics.EncodedProperties = Delta(
                After.PropertiesEncoded, Before.PropertiesEncoded);
            RuntimeMetrics.SchemaCacheHits = Delta(
                After.SchemaCacheHits, Before.SchemaCacheHits);
            RuntimeMetrics.SchemaCacheMisses = Delta(
                After.SchemaCacheMisses, Before.SchemaCacheMisses);
            RuntimeMetrics.ChannelIndexHits = Delta(
                After.ChannelIndexHits, Before.ChannelIndexHits);
            RuntimeMetrics.GatherNanoseconds = Delta(
                After.GatherNanoseconds, Before.GatherNanoseconds);
            RuntimeMetrics.CompareNanoseconds = Delta(
                After.CompareNanoseconds, Before.CompareNanoseconds);
            RuntimeMetrics.SerializeNanoseconds = Delta(
                After.SerializeNanoseconds, Before.SerializeNanoseconds);
            RuntimeMetrics.QueueNanoseconds = Delta(
                After.QueueNanoseconds, Before.QueueNanoseconds);
            Report.Add("Replication", "DirtyScan", Scale, Scale, Duration,
                "dirty_percent=" + std::to_string(DirtyPercent),
                BytesQueued, RuntimeMetrics);
            for (const Pico::uint32 Id : ReliableIds)
                Replication.HandleReliableAcknowledged(Connection, Id);
        }
        Replication.Reset();
        for (PBenchmarkActor* Actor : Actors) World.DestroyActor(Actor);
        World.Tick(0.0f);
    }
}
}

int main(int Argc, char** Argv)
{
    bool bFull = false;
    bool bWriteTrace = true;
    std::size_t SampleCount = 5;
    std::filesystem::path OutputRoot =
        std::filesystem::current_path() / "BenchmarkResults";
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string Argument = Argv[Index];
        if (Argument == "--full") bFull = true;
        else if (Argument == "--quick") bFull = false;
        else if (Argument == "--no-trace") bWriteTrace = false;
        else if (Argument.starts_with("--samples="))
            SampleCount = std::max<std::size_t>(1, std::stoull(
                Argument.substr(std::string("--samples=").size())));
        else if (Argument.starts_with("--output="))
            OutputRoot = Argument.substr(std::string("--output=").size());
    }

    char Program[] = "PicoRuntimeBenchmarks";
    char MaxFps[] = "-maxfps=0";
    char* EngineArguments[] = {Program, MaxFps};
    Pico::FEngineLoop EngineLoop;
    if (EngineLoop.PreInit(2, EngineArguments) != 0
        || EngineLoop.Init() != 0
        || !PBenchmarkObject::RegisterClass()
        || !PBenchmarkActor::RegisterClass())
    {
        std::cerr << "Could not initialize Pico Runtime benchmark fixture\n";
        EngineLoop.Exit();
        return 1;
    }

    Pico::FMemoryTracker::Get().SetEnabled(true);
    Pico::FMemoryTracker::Get().Reset();
    Pico::FProfiler::Get().Reset();
    Pico::FProfiler::Get().SetEnabled(true);
    Pico::FProfiler::Get().BeginFrame();
    FBenchmarkReport Report;
    for (std::size_t Sample = 0; Sample < SampleCount; ++Sample)
    {
        RunObjectBenchmarks(Report, bFull);
        RunTickBenchmarks(Report, bFull);
        RunGarbageCollectionBenchmarks(Report, bFull);
        RunReplicationBenchmarks(Report, *EngineLoop.GetWorld(), bFull);
    }
    Pico::FProfiler::Get().EndFrame();
    Pico::FObjectRegistry::PublishMemoryStatistics();
    const std::vector<Pico::FMemorySnapshot> MemorySnapshots =
        Pico::FMemoryTracker::Get().GetSnapshots();
    std::string MemoryValidationError;
    const bool bMemorySnapshotsValid = ValidateMemorySnapshots(
        MemorySnapshots, MemoryValidationError);
    const bool bReportWritten = Report.Write(OutputRoot, bFull,
        Pico::FProfiler::Get().GetAggregates(),
        MemorySnapshots);
    std::string TraceError;
    const bool bTraceWritten = !bWriteTrace
        || Pico::FProfiler::Get().WriteChromeTrace(
            OutputRoot / "PicoRuntimeBenchmarks.trace.json", &TraceError);
    Pico::FProfiler::Get().SetEnabled(false);
    Pico::FMemoryTracker::Get().SetEnabled(false);
    EngineLoop.Exit();
    if (!bMemorySnapshotsValid || !bReportWritten || !bTraceWritten)
    {
        std::cerr << "Could not validate or write benchmark report: "
            << (!MemoryValidationError.empty()
                ? MemoryValidationError : TraceError) << '\n';
        return 2;
    }
    std::cout << "Pico Runtime benchmarks written to " << OutputRoot << '\n';
    return 0;
}
