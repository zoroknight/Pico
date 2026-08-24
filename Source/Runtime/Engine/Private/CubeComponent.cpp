#include "Pico/Engine/CubeComponent.h"

#include "Pico/Object/Class.h"

#include <cmath>
#include <utility>
#include <vector>

namespace Pico
{
PICO_DEFINE_CLASS(PCubeComponent)

bool PCubeComponent::RegisterProperties(PClass& Class)
{
    std::vector<PProperty> Properties;
    FPropertyMetadata ExtentMetadata;
    ExtentMetadata.Description =
        "Positive box half-size; full size is twice this value on each axis";
    ExtentMetadata.Semantic = "BoxExtent";
    ExtentMetadata.Units = "centimeters";
    ExtentMetadata.Minimum = 0.001;
    ExtentMetadata.Maximum = 1000000.0;
    PICO_ADD_PROPERTY_METADATA(Properties, Extent, ExtentMetadata);
    return Class.AddProperties(std::move(Properties));
}

PCubeComponent::PCubeComponent(const FObjectConstructionParams& Params)
    : PPrimitiveComponent(Params)
{
}

const FVector3& PCubeComponent::GetExtent() const
{
    return Extent;
}

void PCubeComponent::SetExtent(const FVector3& InExtent)
{
    Extent = FVector3(
        std::max(std::abs(InExtent.X), 0.001f),
        std::max(std::abs(InExtent.Y), 0.001f),
        std::max(std::abs(InExtent.Z), 0.001f));
    RecreatePhysicsState();
}

void PCubeComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
    PPrimitiveComponent::PostEditChangeProperty(Event);
    if (Event.Property != nullptr
        && Event.Property->GetName() == FName("Extent"))
    {
        Extent.X = std::clamp(std::abs(Extent.X), 0.001f, 1000000.0f);
        Extent.Y = std::clamp(std::abs(Extent.Y), 0.001f, 1000000.0f);
        Extent.Z = std::clamp(std::abs(Extent.Z), 0.001f, 1000000.0f);
        RecreatePhysicsState();
    }
}

FCollisionShape PCubeComponent::GetCollisionShape() const
{
    const FVector3 Scale = GetWorldTransform().Scale;
    return FCollisionShape::MakeBox(FVector3(
        std::abs(Extent.X * Scale.X),
        std::abs(Extent.Y * Scale.Y),
        std::abs(Extent.Z * Scale.Z)));
}
}
