#pragma once

#include "Pico/Core/AssetPath.h"

#include <functional>
#include <memory>
#include <string>

namespace Pico
{
class FEngineLoop;

class FPicoGraphEditor
{
public:
    using FStatus = std::function<void(std::string, bool)>;
    using FAssetCreated = std::function<void(const FAssetPath&)>;

    FPicoGraphEditor(
        FEngineLoop* EngineLoop,
        FStatus SetStatus,
        FAssetCreated AssetCreated);
    ~FPicoGraphEditor();

    void OpenCreate();
    void OpenAsset(const FAssetPath& AssetPath);
    void Draw();
    bool IsOpen() const;
    bool IsKeyboardFocused() const;
    FAssetPath GetOpenedAsset() const;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
