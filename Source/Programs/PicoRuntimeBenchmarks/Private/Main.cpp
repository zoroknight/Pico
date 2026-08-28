#include "Pico/Core/Profiler.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/EngineLoop.h"
#include "Pico/Engine/Replication.h"
#include "Pico/Engine/TickTaskManager.h"
#include "Pico/Engine/World.h"
#include "Pico/Object/ObjectGlobals.h"
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
    void SetValue(Pico::int32 InValue) { Value = InValue; }

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
        std::size_t BytesPerFrame = 0)
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
                {BytesPerFrame}});
            return;
        }
        Existing->SamplesMicroseconds.push_back(DurationMicroseconds);
        Existing->BytesPerFrame.push_back(BytesPerFrame);
    }

    bool Write(
        const std::filesystem::path& OutputRoot,
        bool bFull,
        const std::vector<Pico::FProfileAggregate>& Aggregates) const
    {
        using FJson = nlohmann::json;
        std::error_code Error;
        std::filesystem::create_directories(OutputRoot, Error);
        if (Error) return false;
        std::ofstream Json(OutputRoot / "PicoRuntimeBenchmarks.json",
            std::ios::binary | std::ios::trunc);
        std::ofstream Csv(OutputRoot / "PicoRuntimeBenchmarks.csv",
            std::ios::binary | std::ios::trunc);
        if (!Json || !Csv) return false;
        FJson JsonRecords = FJson::array();
        Csv << "format_version,preset,suite,case,scale,operations,samples,p50_us,p95_us,max_us,mean_us,ns_per_operation,bytes_per_frame,parameters\n";
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
            JsonRecords.push_back({{"suite", Record.Suite}, {"case", Record.Case},
                {"scale", Record.Scale}, {"operations", Record.Operations},
                {"sample_count", Record.SamplesMicroseconds.size()},
                {"p50_us", P50}, {"p95_us", P95}, {"max_us", Max},
                {"mean_us", Mean}, {"ns_per_operation", NanosecondsPerOperation},
                {"bytes_per_frame", Bytes}, {"parameters", Record.Parameters}});
            Csv << "2," << (bFull ? "full" : "quick") << ','
                << Record.Suite << ',' << Record.Case << ',' << Record.Scale
                << ',' << Record.Operations << ',' << Record.SamplesMicroseconds.size()
                << ',' << P50 << ',' << P95 << ',' << Max << ',' << Mean
                << ',' << std::fixed << std::setprecision(3)
                << NanosecondsPerOperation << ',' << Bytes << ','
                << Record.Parameters << '\n';
        }
        FJson ProfileScopes = FJson::array();
        for (const Pico::FProfileAggregate& Aggregate : Aggregates)
            ProfileScopes.push_back({{"name", Aggregate.Name},
                {"count", Aggregate.Count}, {"total_us", Aggregate.TotalMicroseconds},
                {"min_us", Aggregate.MinMicroseconds},
                {"max_us", Aggregate.MaxMicroseconds}});
        const FJson Baseline = {{"format_version", 2},
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
            {"full_scale_contract", {{"object", {1000, 10000, 100000}},
                {"tick", {1000, 10000}}, {"replication", {100, 1000}},
                {"dirty_ratio_percent", {1, 10, 100}}}}};
        Json << Baseline.dump(2) << '\n';
        std::ofstream RuntimeBaseline(OutputRoot / "RuntimeBaseline.json",
            std::ios::binary | std::ios::trunc);
        if (!RuntimeBaseline) return false;
        RuntimeBaseline << Baseline.dump(2) << '\n';
        return static_cast<bool>(Json) && static_cast<bool>(Csv);
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
            + ";collected=" + std::to_string(Result.CollectedObjectCount));
    for (std::size_t Index = 0; Index < SurvivorCount; ++Index)
        if (Pico::ResolveObject(Objects[Index]->GetHandle()) == Objects[Index])
            Pico::RemoveFromRoot(Objects[Index]);
    Pico::CollectGarbage();
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
        Replication.ReplicateServerConnection(Connection, Queue);
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
            const std::uint64_t Duration = MeasureMicroseconds([&]()
            {
                Replication.ReplicateServerConnection(Connection, Queue);
            });
            Report.Add("Replication", "DirtyScan", Scale, Scale, Duration,
                "dirty_percent=" + std::to_string(DirtyPercent), BytesQueued);
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
    std::size_t SampleCount = 5;
    std::filesystem::path OutputRoot =
        std::filesystem::current_path() / "BenchmarkResults";
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string Argument = Argv[Index];
        if (Argument == "--full") bFull = true;
        else if (Argument == "--quick") bFull = false;
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
    const bool bReportWritten = Report.Write(OutputRoot, bFull,
        Pico::FProfiler::Get().GetAggregates());
    std::string TraceError;
    const bool bTraceWritten = Pico::FProfiler::Get().WriteChromeTrace(
        OutputRoot / "PicoRuntimeBenchmarks.trace.json", &TraceError);
    Pico::FProfiler::Get().SetEnabled(false);
    EngineLoop.Exit();
    if (!bReportWritten || !bTraceWritten)
    {
        std::cerr << "Could not write benchmark report: " << TraceError << '\n';
        return 2;
    }
    std::cout << "Pico Runtime benchmarks written to " << OutputRoot << '\n';
    return 0;
}
