#pragma once

#include "Pico/Core/Math/Vector3.h"

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Pico
{
class FAssetManager;
class FAssetPath;
class FAssetRegistry;
class FEditorSelection;
class FEditorTransformService;
class FSceneViewportRenderer;
class PObject;
class PWorld;
enum class EEditorSelectionOperation;
struct FEditorToolState;

class FEditorViewportPanel
{
public:
    using FSelectObject = std::function<void(
        PObject*,
        EEditorSelectionOperation,
        const std::vector<PObject*>&)>;
    using FSetStatus = std::function<void(std::string)>;
    using FBeginTransaction = std::function<bool(std::string)>;
    using FFinishTransaction = std::function<void(bool)>;

    FEditorViewportPanel(FSceneViewportRenderer* Renderer, GLFWwindow* Window);
    ~FEditorViewportPanel();

    void Draw(
        PWorld* World,
        FAssetRegistry& AssetRegistry,
        FAssetManager& AssetManager,
        const FEditorSelection& Selection,
        const FEditorToolState& ToolState,
        FEditorTransformService& TransformService,
        float Width,
        float Height,
        const FSelectObject& SelectObject,
        const FSetStatus& SetStatus,
        const FBeginTransaction& BeginTransaction,
        const FFinishTransaction& FinishTransaction);

    bool IsCameraCaptured() const;
    bool IsTransformActive() const;
    bool CancelActiveTransform();
    bool FocusSelection(
        const FAssetRegistry& AssetRegistry,
        FAssetManager& AssetManager,
        const FEditorSelection& Selection);
    void InvalidateStaticMesh(const FAssetPath& AssetPath);

private:
    void BeginCameraCapture();
    void EndCameraCapture();

    FSceneViewportRenderer* Renderer = nullptr;
    GLFWwindow* Window = nullptr;
    FVector3 CameraPosition = FVector3(530.0f, -530.0f, 400.0f);
    float CameraYawDegrees = 135.0f;
    float CameraPitchDegrees = -28.0f;
    float CameraMoveSpeed = 600.0f;
    float CameraNearPlane = 1.0f;
    float CameraFarPlane = 10000.0f;
    bool bCameraCaptured = false;
    bool bIgnoreGizmoUntilRelease = false;
    bool bTransformChanged = false;
    FEditorTransformService* ActiveTransformService = nullptr;
    double LastCursorX = 0.0;
    double LastCursorY = 0.0;
};
}
