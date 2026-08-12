#pragma once

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Core/Math/Matrix4.h"
#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Types.h"
#include "Pico/Object/ObjectTypes.h"

#include <memory>
#include <array>
#include <span>

namespace Pico
{
class FAssetManager;
class FAssetRegistry;
class PWorld;
class PActor;

using FOpenGLProcedure = void (*)();
using FOpenGLProcLoader = FOpenGLProcedure (*)(const char*);

struct FSceneView
{
    FVector3 Position = FVector3(500.0f, -500.0f, 350.0f);
    FVector3 Target = FVector3::ZeroVector;
    FVector3 Up = FVector3::UpVector;
    float VerticalFieldOfViewDegrees = 50.0f;
    float NearPlane = 1.0f;
    float FarPlane = 10000.0f;
};

struct FDirectionalLightData
{
    bool bEnabled = false;
    FVector3 Direction = FVector3(0.45f, -0.55f, 0.8f);
    FVector3 Color = FVector3::OneVector;
    float Intensity = 3.0f;
};

struct FPointLightData
{
    FVector3 Position = FVector3::ZeroVector;
    FVector3 Color = FVector3::OneVector;
    float Intensity = 1.0f;
    float AttenuationRadius = 500.0f;
};

struct FSceneLighting
{
    static constexpr std::size_t MaxPointLights = 4;

    FDirectionalLightData DirectionalLight;
    std::array<FPointLightData, MaxPointLights> PointLights {};
    std::size_t PointLightCount = 0;
    bool bHasAuthoredLights = false;
};

FMatrix4 BuildSceneViewMatrix(const FSceneView& View);
FMatrix4 BuildSceneProjectionMatrix(
    const FSceneView& View,
    float AspectRatio);
bool TryBuildActiveCameraView(
    const PWorld* World,
    FSceneView& OutView,
    bool bAllowInactiveFallback = false);
bool TryBuildActorCameraView(
    const PActor* ViewTarget,
    FSceneView& OutView,
    bool bAllowInactiveFallback = false);
FSceneLighting GatherSceneLighting(const PWorld* World);

class FSceneViewportRenderer
{
public:
    FSceneViewportRenderer();
    ~FSceneViewportRenderer();

    FSceneViewportRenderer(const FSceneViewportRenderer&) = delete;
    FSceneViewportRenderer& operator=(const FSceneViewportRenderer&) = delete;

    bool Initialize(FOpenGLProcLoader Loader);
    void Shutdown();
    bool Resize(uint32 Width, uint32 Height);
    bool Render(
        PWorld* World,
        FAssetRegistry& AssetRegistry,
        FAssetManager& AssetManager,
        const FSceneView& View,
        std::span<const FObjectHandle> SelectedObjects = {},
        bool bDrawComponentVisualizations = true);
    bool PresentToBackBuffer(uint32 Width, uint32 Height) const;
    FObjectHandle Pick(uint32 X, uint32 Y) const;

    uint32 GetColorTexture() const;
    uint32 GetWidth() const;
    uint32 GetHeight() const;
    bool IsInitialized() const;
    void InvalidateStaticMesh(const FAssetPath& AssetPath);

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
