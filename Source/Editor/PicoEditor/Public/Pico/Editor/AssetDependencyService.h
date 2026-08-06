#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Name.h"
#include "Pico/Object/ObjectTypes.h"

#include <string>
#include <vector>

namespace Pico
{
class FAssetRegistry;
class PWorld;

struct FObjectAssetReference
{
    FObjectHandle ObjectHandle;
    std::string ObjectPath;
    FName PropertyName;
    FAssetPath AssetPath;
};

class FAssetDependencyService
{
public:
    static std::vector<FObjectAssetReference> GatherWorldReferences(
        const PWorld* World);
    static std::vector<FObjectAssetReference> FindWorldReferencers(
        const PWorld* World,
        const FAssetPath& AssetPath);
    static std::size_t ReplaceWorldReferences(
        PWorld* World,
        const FAssetPath& OldAssetPath,
        const FAssetPath& NewAssetPath);
    static std::vector<FAssetPath> GetAssetDependencies(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry);
    static std::vector<FAssetPath> FindAssetReferencers(
        const FAssetPath& AssetPath,
        const FAssetRegistry& Registry);
};
}
