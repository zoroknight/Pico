#pragma once

#include "Pico/Core/Math/Vector3.h"

#include <functional>
#include <string>

struct GLFWwindow;

namespace Pico
{
class FEditorSelection;
class FSceneViewportRenderer;
class PObject;
class PWorld;

class FEditorViewportPanel
{
public:
    using FSelectObject = std::function<void(PObject*)>;
    using FSetStatus = std::function<void(std::string)>;

    FEditorViewportPanel(FSceneViewportRenderer* Renderer, GLFWwindow* Window);
    ~FEditorViewportPanel();

    void Draw(
        PWorld* World,
        const FEditorSelection& Selection,
        float Width,
        float Height,
        const FSelectObject& SelectObject,
        const FSetStatus& SetStatus);

private:
    void BeginCameraCapture();
    void EndCameraCapture();

    FSceneViewportRenderer* Renderer = nullptr;
    GLFWwindow* Window = nullptr;
    FVector3 CameraPosition = FVector3(530.0f, -530.0f, 400.0f);
    float CameraYawDegrees = 135.0f;
    float CameraPitchDegrees = -28.0f;
    float CameraMoveSpeed = 600.0f;
    bool bCameraCaptured = false;
    double LastCursorX = 0.0;
    double LastCursorY = 0.0;
};
}
