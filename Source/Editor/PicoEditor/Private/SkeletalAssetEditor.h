#pragma once

#include "Pico/Core/AssetPath.h"

#include <functional>
#include <memory>
#include <string>

namespace Pico
{
class FEngineLoop;

class FSkeletalAssetEditor
{
public:
    using FStatus = std::function<void(std::string, bool)>;
    using FImported = std::function<void(const FAssetPath&)>;

    FSkeletalAssetEditor(
        FEngineLoop* EngineLoop,
        FStatus SetStatus,
        FImported OnImported);
    ~FSkeletalAssetEditor();

    FSkeletalAssetEditor(const FSkeletalAssetEditor&) = delete;
    FSkeletalAssetEditor& operator=(const FSkeletalAssetEditor&) = delete;

    void OpenImport();
    void OpenAsset(const FAssetPath& AssetPath);
    void Draw();

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
