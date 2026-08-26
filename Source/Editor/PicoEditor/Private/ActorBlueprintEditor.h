#pragma once

#include "Pico/Core/AssetPath.h"

#include <functional>
#include <memory>
#include <string>

namespace Pico
{
class FEngineLoop;

class FActorBlueprintEditor
{
public:
    using FStatus = std::function<void(std::string, bool)>;
    using FSpawnInLevel = std::function<void(const FAssetPath&)>;
    using FAssetCreated = std::function<void(const FAssetPath&)>;
    using FWorldChanged = std::function<void()>;

    FActorBlueprintEditor(
        FEngineLoop* EngineLoop,
        FStatus SetStatus,
        FSpawnInLevel SpawnInLevel,
        FAssetCreated AssetCreated,
        FWorldChanged WorldChanged);
    ~FActorBlueprintEditor();

    FActorBlueprintEditor(const FActorBlueprintEditor&) = delete;
    FActorBlueprintEditor& operator=(const FActorBlueprintEditor&) = delete;

    void OpenCreate();
    void OpenAsset(const FAssetPath& AssetPath);
    void Draw();
    bool IsOpen() const;
    FAssetPath GetOpenedAsset() const;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
