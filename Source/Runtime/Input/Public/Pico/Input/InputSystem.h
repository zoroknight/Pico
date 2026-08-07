#pragma once

#include "Pico/Core/Math/Vector2.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Pico
{
class FConfigFile;

enum class EKey
{
    Unknown,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Space,
    Escape,
    Enter,
    Tab,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    Up,
    Down,
    Left,
    Right,
    MouseLeft,
    MouseRight,
    MouseMiddle,
    Count
};

enum class EInputAxisSource
{
    Key,
    MouseX,
    MouseY,
    MouseWheel
};

struct FAxisMapping
{
    EInputAxisSource Source = EInputAxisSource::Key;
    EKey Key = EKey::Unknown;
    float Scale = 1.0f;
};

class FInputSystem
{
public:
    void BeginFrame();
    void EndFrame();
    void SetKeyState(EKey Key, bool bDown);
    void SetMousePosition(double X, double Y);
    void AddMouseWheelDelta(double Delta);
    void SetFocused(bool bFocused);

    bool IsKeyDown(EKey Key) const;
    bool WasKeyPressed(EKey Key) const;
    bool WasKeyReleased(EKey Key) const;
    FVector2 GetMousePosition() const;
    FVector2 GetMouseDelta() const;
    float GetMouseWheelDelta() const;

    void ClearMappings();
    void AddActionMapping(std::string ActionName, EKey Key);
    void AddAxisMapping(std::string AxisName, FAxisMapping Mapping);
    void LoadMappings(const FConfigFile& Config);
    bool IsActionDown(std::string_view ActionName) const;
    bool WasActionPressed(std::string_view ActionName) const;
    bool WasActionReleased(std::string_view ActionName) const;
    float GetAxisValue(std::string_view AxisName) const;
    float GetMouseSensitivity() const;

    static EKey KeyFromName(std::string_view Name);
    static std::string_view GetKeyName(EKey Key);

private:
    static constexpr std::size_t KeyCount = static_cast<std::size_t>(EKey::Count);
    static std::size_t GetKeyIndex(EKey Key);
    void AddDefaultMappingsForMissingNames();

    std::array<bool, KeyCount> DownStates {};
    std::array<bool, KeyCount> PressedStates {};
    std::array<bool, KeyCount> ReleasedStates {};
    std::unordered_map<std::string, std::vector<EKey>> ActionMappings;
    std::unordered_map<std::string, std::vector<FAxisMapping>> AxisMappings;
    FVector2 MousePosition;
    FVector2 MouseDelta;
    float MouseWheelDelta = 0.0f;
    float MouseSensitivity = 1.0f;
    bool bHasMousePosition = false;
    bool bFocused = true;
};
}
