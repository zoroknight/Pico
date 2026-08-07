#include "TestRunner.h"

#include "Pico/Core/Config.h"
#include "Pico/Engine/GameEngine.h"
#include "Pico/Input/InputSystem.h"

#include <filesystem>

namespace
{
void TestKeyTransitions(FTestRunner& Runner)
{
    Pico::FInputSystem Input;
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::W, true);
    Runner.Expect(Input.IsKeyDown(Pico::EKey::W), "pressed key is down");
    Runner.Expect(Input.WasKeyPressed(Pico::EKey::W), "press transition is recorded");
    Runner.Expect(!Input.WasKeyReleased(Pico::EKey::W), "press is not a release");

    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::W, true);
    Runner.Expect(Input.IsKeyDown(Pico::EKey::W), "held key stays down");
    Runner.Expect(!Input.WasKeyPressed(Pico::EKey::W), "held key does not repeat press");

    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::W, false);
    Runner.Expect(!Input.IsKeyDown(Pico::EKey::W), "released key is up");
    Runner.Expect(Input.WasKeyReleased(Pico::EKey::W), "release transition is recorded");
}

void TestFocusAndPointer(FTestRunner& Runner)
{
    Pico::FInputSystem Input;
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::A, true);
    Input.SetMousePosition(100.0, 200.0);
    Input.SetMousePosition(112.0, 193.0);
    Input.AddMouseWheelDelta(2.0);
    Runner.Expect(
        Input.GetMouseDelta().Equals(Pico::FVector2(12.0f, -7.0f)),
        "mouse delta accumulates within a frame");
    Runner.Expect(Input.GetMouseWheelDelta() == 2.0f, "mouse wheel accumulates");

    Input.SetFocused(false);
    Runner.Expect(!Input.IsKeyDown(Pico::EKey::A), "focus loss clears held keys");
    Runner.Expect(Input.WasKeyReleased(Pico::EKey::A), "focus loss records release");

    Input.BeginFrame();
    Runner.Expect(
        Input.GetMouseDelta().Equals(Pico::FVector2()),
        "mouse delta resets each frame");
    Runner.Expect(Input.GetMouseWheelDelta() == 0.0f, "mouse wheel resets each frame");
}

void TestMappings(FTestRunner& Runner)
{
    Pico::FConfigFile Config;
    Runner.Expect(
        Config.Load(std::filesystem::path("Tests/Fixtures/Input.ini")),
        "input fixture loads");

    Pico::FInputSystem Input;
    Input.LoadMappings(Config);
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::Enter, true);
    Runner.Expect(Input.IsActionDown("Jump"), "an action supports multiple keys");
    Runner.Expect(Input.WasActionPressed("Jump"), "mapped action reports press");

    Input.SetKeyState(Pico::EKey::W, true);
    Runner.Expect(Input.GetAxisValue("MoveForward") == 1.0f, "positive axis mapping works");
    Input.SetKeyState(Pico::EKey::S, true);
    Runner.Expect(Input.GetAxisValue("MoveForward") == 0.0f, "opposite keys cancel");
    Input.SetKeyState(Pico::EKey::W, false);
    Runner.Expect(Input.GetAxisValue("MoveForward") == -1.0f, "negative axis mapping works");

    Input.SetMousePosition(10.0, 10.0);
    Input.SetMousePosition(14.0, 7.0);
    Runner.Expect(Input.GetAxisValue("LookYaw") == 4.0f, "mouse axis uses sensitivity");
    Runner.Expect(Input.GetAxisValue("LookPitch") == 1.5f, "mouse axis supports inversion");
}

void TestDefaultMappings(FTestRunner& Runner)
{
    Pico::FConfigFile Config;
    Runner.Expect(
        Config.Load(std::filesystem::path("Config/Pico.ini")),
        "engine config loads");
    Pico::FInputSystem Input;
    Input.LoadMappings(Config);
    Input.BeginFrame();
    Input.SetKeyState(Pico::EKey::Space, true);
    Input.SetKeyState(Pico::EKey::D, true);
    Runner.Expect(Input.IsActionDown("Jump"), "missing action gets runtime default");
    Runner.Expect(Input.GetAxisValue("MoveRight") == 1.0f, "missing axis gets runtime default");
}

void TestContentPathResolution(FTestRunner& Runner)
{
    const std::filesystem::path ContentRoot =
        std::filesystem::absolute("Projects/PicoSandbox/Content");
    std::filesystem::path Resolved;
    Runner.Expect(
        Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "/Game/Maps/EditorWorld.pworld", Resolved),
        "valid default map resolves under Content");
    Runner.Expect(
        Resolved.filename() == "EditorWorld.pworld",
        "resolved map preserves the configured file");
    Runner.Expect(
        !Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "/Game/../PicoSandbox.pico", Resolved),
        "default map cannot escape Content");
    Runner.Expect(
        !Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "/Game/Maps/Missing.pworld", Resolved),
        "missing default map fails safely");
    Runner.Expect(
        !Pico::FGameEngine::ResolveContentPath(
            ContentRoot, "Maps/EditorWorld.pworld", Resolved),
        "default map requires a Game virtual path");
}
}

int main()
{
    FTestRunner Runner;
    TestKeyTransitions(Runner);
    TestFocusAndPointer(Runner);
    TestMappings(Runner);
    TestDefaultMappings(Runner);
    TestContentPathResolution(Runner);
    return Runner.Finish();
}
