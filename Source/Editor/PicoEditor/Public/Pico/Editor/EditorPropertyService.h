#pragma once

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Core/Name.h"
#include "Pico/Editor/EditorTransactionManager.h"
#include "Pico/Object/ObjectTypes.h"

#include <string>
#include <variant>

namespace Pico
{
class FEditorSelection;
class FEngineLoop;

using FEditorPropertyValue = std::variant<
    int32,
    float,
    bool,
    FVector3,
    FRotator,
    FTransform,
    FAssetPath>;

struct FEditorPropertyResult
{
    bool bSucceeded = false;
    std::string Message;
};

class FEditorPropertyService
{
public:
    FEditorPropertyService(
        FEngineLoop* EngineLoop,
        FEditorSelection* Selection,
        FEditorTransactionManager* Transactions,
        FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot);

    FEditorPropertyResult SetProperty(
        FObjectHandle ObjectHandle,
        FName PropertyName,
        const FEditorPropertyValue& Value);

private:
    bool ValidateAssetReference(
        const class PProperty& Property,
        const FAssetPath& AssetPath) const;

    FEngineLoop* EngineLoop = nullptr;
    FEditorSelection* Selection = nullptr;
    FEditorTransactionManager* Transactions = nullptr;
    FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot;
};
}
