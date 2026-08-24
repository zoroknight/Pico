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
class PObject;
class PProperty;

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

FEditorPropertyResult ApplyEditorPropertyValue(
    FEngineLoop* EngineLoop,
    PObject* Object,
    const PProperty* Property,
    const FEditorPropertyValue& Value);

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
    FEngineLoop* EngineLoop = nullptr;
    FEditorSelection* Selection = nullptr;
    FEditorTransactionManager* Transactions = nullptr;
    FEditorTransactionManager::FRestoreSnapshot RestoreSnapshot;
};
}
