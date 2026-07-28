#pragma once

#include "Pico/Core/Math/Vector3.h"
#include "Pico/Core/Types.h"

#include <memory>

namespace Pico
{
class PWorld;

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
    bool Render(PWorld* World, const FSceneView& View);

    uint32 GetColorTexture() const;
    uint32 GetWidth() const;
    uint32 GetHeight() const;
    bool IsInitialized() const;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
