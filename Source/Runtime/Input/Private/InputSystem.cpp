#include "Pico/Input/InputSystem.h"

#include "Pico/Core/Config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

namespace Pico
{
namespace
{
std::string Trim(std::string_view Text)
{
    std::size_t Begin = 0;
    while (Begin < Text.size()
        && std::isspace(static_cast<unsigned char>(Text[Begin])))
    {
        ++Begin;
    }
    std::size_t End = Text.size();
    while (End > Begin
        && std::isspace(static_cast<unsigned char>(Text[End - 1])))
    {
        --End;
    }
    return std::string(Text.substr(Begin, End - Begin));
}

std::string ToLower(std::string_view Text)
{
    std::string Result;
    Result.reserve(Text.size());
    for (const char Character : Text)
    {
        Result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(Character))));
    }
    return Result;
}

std::vector<std::string> Split(std::string_view Text, char Delimiter)
{
    std::vector<std::string> Parts;
    std::size_t Begin = 0;
    while (Begin <= Text.size())
    {
        const std::size_t End = Text.find(Delimiter, Begin);
        Parts.push_back(Trim(Text.substr(
            Begin,
            End == std::string_view::npos ? Text.size() - Begin : End - Begin)));
        if (End == std::string_view::npos)
        {
            break;
        }
        Begin = End + 1;
    }
    return Parts;
}

float ParseScale(std::string_view Text, float DefaultValue)
{
    float Result = 0.0f;
    const char* Begin = Text.data();
    const char* End = Begin + Text.size();
    const std::from_chars_result Parsed =
        std::from_chars(Begin, End, Result, std::chars_format::general);
    return Parsed.ec == std::errc {} && Parsed.ptr == End && std::isfinite(Result)
        ? Result : DefaultValue;
}

bool ParseAxisSource(std::string_view Name, FAxisMapping& OutMapping)
{
    const std::string Lower = ToLower(Name);
    if (Lower == "mousex")
    {
        OutMapping.Source = EInputAxisSource::MouseX;
        return true;
    }
    if (Lower == "mousey")
    {
        OutMapping.Source = EInputAxisSource::MouseY;
        return true;
    }
    if (Lower == "mousewheel")
    {
        OutMapping.Source = EInputAxisSource::MouseWheel;
        return true;
    }

    OutMapping.Key = FInputSystem::KeyFromName(Name);
    OutMapping.Source = EInputAxisSource::Key;
    return OutMapping.Key != EKey::Unknown;
}
}

void FInputSystem::BeginFrame()
{
    PressedStates.fill(false);
    ReleasedStates.fill(false);
    MouseDelta = {};
    MouseWheelDelta = 0.0f;
}

void FInputSystem::EndFrame()
{
}

void FInputSystem::SetKeyState(EKey Key, bool bDown)
{
    const std::size_t Index = GetKeyIndex(Key);
    if (Key == EKey::Unknown || Index >= KeyCount || DownStates[Index] == bDown)
    {
        return;
    }
    DownStates[Index] = bDown;
    PressedStates[Index] = bDown;
    ReleasedStates[Index] = !bDown;
}

void FInputSystem::SetMousePosition(double X, double Y)
{
    const FVector2 NewPosition(static_cast<float>(X), static_cast<float>(Y));
    if (bHasMousePosition)
    {
        MouseDelta.X += NewPosition.X - MousePosition.X;
        MouseDelta.Y += NewPosition.Y - MousePosition.Y;
    }
    MousePosition = NewPosition;
    bHasMousePosition = true;
}

void FInputSystem::AddMouseWheelDelta(double Delta)
{
    MouseWheelDelta += static_cast<float>(Delta);
}

void FInputSystem::SetFocused(bool bInFocused)
{
    if (bFocused == bInFocused)
    {
        return;
    }
    bFocused = bInFocused;
    bHasMousePosition = false;
    if (bFocused)
    {
        return;
    }
    for (std::size_t Index = 0; Index < KeyCount; ++Index)
    {
        if (DownStates[Index])
        {
            DownStates[Index] = false;
            ReleasedStates[Index] = true;
        }
    }
}

bool FInputSystem::IsKeyDown(EKey Key) const
{
    const std::size_t Index = GetKeyIndex(Key);
    return Index < KeyCount && DownStates[Index];
}

bool FInputSystem::WasKeyPressed(EKey Key) const
{
    const std::size_t Index = GetKeyIndex(Key);
    return Index < KeyCount && PressedStates[Index];
}

bool FInputSystem::WasKeyReleased(EKey Key) const
{
    const std::size_t Index = GetKeyIndex(Key);
    return Index < KeyCount && ReleasedStates[Index];
}

FVector2 FInputSystem::GetMousePosition() const
{
    return MousePosition;
}

FVector2 FInputSystem::GetMouseDelta() const
{
    return MouseDelta;
}

float FInputSystem::GetMouseWheelDelta() const
{
    return MouseWheelDelta;
}

void FInputSystem::ClearMappings()
{
    ActionMappings.clear();
    AxisMappings.clear();
}

void FInputSystem::AddActionMapping(std::string ActionName, EKey Key)
{
    if (!ActionName.empty() && Key != EKey::Unknown)
    {
        ActionMappings[std::move(ActionName)].push_back(Key);
    }
}

void FInputSystem::AddAxisMapping(std::string AxisName, FAxisMapping Mapping)
{
    if (!AxisName.empty()
        && (Mapping.Source != EInputAxisSource::Key
            || Mapping.Key != EKey::Unknown)
        && std::isfinite(Mapping.Scale))
    {
        AxisMappings[std::move(AxisName)].push_back(Mapping);
    }
}

void FInputSystem::LoadMappings(const FConfigFile& Config)
{
    ClearMappings();
    MouseSensitivity = static_cast<float>(
        Config.GetDouble("Input", "MouseSensitivity", 1.0));
    if (!std::isfinite(MouseSensitivity) || MouseSensitivity < 0.0f)
    {
        MouseSensitivity = 1.0f;
    }

    for (const auto& [SettingName, Value] : Config.GetSectionEntries("Input"))
    {
        constexpr std::string_view ActionPrefix = "Action.";
        constexpr std::string_view AxisPrefix = "Axis.";
        if (SettingName.starts_with(ActionPrefix))
        {
            const std::string ActionName = SettingName.substr(ActionPrefix.size());
            for (const std::string& KeyName : Split(Value, ','))
            {
                AddActionMapping(ActionName, KeyFromName(KeyName));
            }
        }
        else if (SettingName.starts_with(AxisPrefix))
        {
            const std::string AxisName = SettingName.substr(AxisPrefix.size());
            for (const std::string& BindingText : Split(Value, ','))
            {
                const std::size_t Colon = BindingText.find(':');
                const std::string SourceName = Trim(BindingText.substr(0, Colon));
                FAxisMapping Mapping;
                Mapping.Scale = Colon == std::string::npos
                    ? 1.0f
                    : ParseScale(Trim(BindingText.substr(Colon + 1)), 1.0f);
                if (ParseAxisSource(SourceName, Mapping))
                {
                    AddAxisMapping(AxisName, Mapping);
                }
            }
        }
    }
    AddDefaultMappingsForMissingNames();
}

bool FInputSystem::IsActionDown(std::string_view ActionName) const
{
    const auto Found = ActionMappings.find(std::string(ActionName));
    return Found != ActionMappings.end()
        && std::any_of(Found->second.begin(), Found->second.end(),
            [this](EKey Key) { return IsKeyDown(Key); });
}

bool FInputSystem::WasActionPressed(std::string_view ActionName) const
{
    const auto Found = ActionMappings.find(std::string(ActionName));
    return Found != ActionMappings.end()
        && std::any_of(Found->second.begin(), Found->second.end(),
            [this](EKey Key) { return WasKeyPressed(Key); });
}

bool FInputSystem::WasActionReleased(std::string_view ActionName) const
{
    const auto Found = ActionMappings.find(std::string(ActionName));
    return Found != ActionMappings.end()
        && std::any_of(Found->second.begin(), Found->second.end(),
            [this](EKey Key) { return WasKeyReleased(Key); });
}

float FInputSystem::GetAxisValue(std::string_view AxisName) const
{
    const auto Found = AxisMappings.find(std::string(AxisName));
    if (Found == AxisMappings.end())
    {
        return 0.0f;
    }

    float KeyValue = 0.0f;
    float AnalogValue = 0.0f;
    for (const FAxisMapping& Mapping : Found->second)
    {
        switch (Mapping.Source)
        {
        case EInputAxisSource::Key:
            KeyValue += IsKeyDown(Mapping.Key) ? Mapping.Scale : 0.0f;
            break;
        case EInputAxisSource::MouseX:
            AnalogValue += MouseDelta.X * Mapping.Scale * MouseSensitivity;
            break;
        case EInputAxisSource::MouseY:
            AnalogValue += MouseDelta.Y * Mapping.Scale * MouseSensitivity;
            break;
        case EInputAxisSource::MouseWheel:
            AnalogValue += MouseWheelDelta * Mapping.Scale;
            break;
        }
    }
    return std::clamp(KeyValue, -1.0f, 1.0f) + AnalogValue;
}

float FInputSystem::GetMouseSensitivity() const
{
    return MouseSensitivity;
}

EKey FInputSystem::KeyFromName(std::string_view Name)
{
    const std::string Lower = ToLower(Trim(Name));
    if (Lower.size() == 1 && Lower[0] >= 'a' && Lower[0] <= 'z')
    {
        return static_cast<EKey>(
            static_cast<int>(EKey::A) + (Lower[0] - 'a'));
    }
    if (Lower == "1" || Lower == "one") return EKey::One;
    if (Lower == "2" || Lower == "two") return EKey::Two;
    if (Lower == "3" || Lower == "three") return EKey::Three;
    if (Lower == "space") return EKey::Space;
    if (Lower == "escape") return EKey::Escape;
    if (Lower == "enter") return EKey::Enter;
    if (Lower == "tab") return EKey::Tab;
    if (Lower == "leftshift") return EKey::LeftShift;
    if (Lower == "rightshift") return EKey::RightShift;
    if (Lower == "leftcontrol" || Lower == "leftctrl") return EKey::LeftControl;
    if (Lower == "rightcontrol" || Lower == "rightctrl") return EKey::RightControl;
    if (Lower == "up") return EKey::Up;
    if (Lower == "down") return EKey::Down;
    if (Lower == "left") return EKey::Left;
    if (Lower == "right") return EKey::Right;
    if (Lower == "mouseleft") return EKey::MouseLeft;
    if (Lower == "mouseright") return EKey::MouseRight;
    if (Lower == "mousemiddle") return EKey::MouseMiddle;
    return EKey::Unknown;
}

std::string_view FInputSystem::GetKeyName(EKey Key)
{
    static constexpr std::array<std::string_view, KeyCount> Names {
        "Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I",
        "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
        "U", "V", "W", "X", "Y", "Z", "1", "2", "3", "Space", "Escape", "Enter",
        "Tab", "LeftShift", "RightShift", "LeftControl", "RightControl",
        "Up", "Down", "Left", "Right", "MouseLeft", "MouseRight",
        "MouseMiddle"
    };
    const std::size_t Index = GetKeyIndex(Key);
    return Index < Names.size() ? Names[Index] : Names[0];
}

std::size_t FInputSystem::GetKeyIndex(EKey Key)
{
    return static_cast<std::size_t>(Key);
}

void FInputSystem::AddDefaultMappingsForMissingNames()
{
    if (!ActionMappings.contains("Jump")) AddActionMapping("Jump", EKey::Space);
    if (!ActionMappings.contains("Aim")) AddActionMapping("Aim", EKey::MouseRight);
    if (!AxisMappings.contains("MoveForward"))
    {
        AddAxisMapping("MoveForward", { EInputAxisSource::Key, EKey::W, 1.0f });
        AddAxisMapping("MoveForward", { EInputAxisSource::Key, EKey::S, -1.0f });
    }
    if (!AxisMappings.contains("MoveRight"))
    {
        AddAxisMapping("MoveRight", { EInputAxisSource::Key, EKey::D, 1.0f });
        AddAxisMapping("MoveRight", { EInputAxisSource::Key, EKey::A, -1.0f });
    }
    if (!AxisMappings.contains("LookYaw"))
    {
        AddAxisMapping("LookYaw", { EInputAxisSource::MouseX, EKey::Unknown, 1.0f });
    }
    if (!AxisMappings.contains("LookPitch"))
    {
        AddAxisMapping("LookPitch", { EInputAxisSource::MouseY, EKey::Unknown, -1.0f });
    }
}
}
