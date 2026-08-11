#pragma once

#include "AssetReferenceWidget.h"

#include "Pico/Core/AssetPath.h"
#include "Pico/Core/Name.h"
#include "Pico/Object/ObjectTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace Pico
{
class FEditorSelection;
class PActor;
class PObject;
class PProperty;
class PSceneComponent;

class FDetailsPanel
{
public:
    using FPrepareEdit = std::function<bool(
        const std::string&,
        std::string,
        bool,
        bool)>;
    using FCompleteEdit = std::function<void(
        const std::string&,
        bool,
        bool,
        bool)>;
    using FSetStatus = std::function<void(std::string, bool)>;
    using FAddRoot = std::function<void()>;
    using FSetAssetReference = std::function<void(
        FObjectHandle,
        FName,
        const FAssetPath&)>;
    using FBrowseAsset = std::function<void(const FAssetPath&)>;

    void Draw(
        FEditorSelection& Selection,
        FPrepareEdit PrepareEdit,
        FCompleteEdit CompleteEdit,
        FSetStatus SetStatus,
        FAddRoot AddRoot,
        const FAssetRegistry& AssetRegistry,
        FBrowseAsset BrowseAsset,
        const FAssetPath& SelectedAsset,
        FSetAssetReference SetAssetReference);

private:
    void DrawObjectIdentity(PObject* Object);
    void DrawActorDetails(PActor* Actor);
    void DrawSceneComponentDetails(PSceneComponent* Component);
    void DrawReflectedProperties(PObject* Object);
    void DrawEventBindings(
        PObject* Object,
        const std::vector<const PProperty*>& Properties);
    void DrawPropertyEditor(PObject* Object, const PProperty* Property);
    bool PrepareInteractiveEdit(
        const std::string& Key,
        std::string Description,
        bool bActivated,
        bool bChanged);
    void CompleteInteractiveEdit(
        const std::string& Key,
        bool bChanged,
        bool bActive,
        bool bApplied);
    void SetStatus(std::string Message, bool bError = false);

    FEditorSelection* Selection = nullptr;
    FPrepareEdit PrepareEdit;
    FCompleteEdit CompleteEdit;
    FSetStatus StatusSink;
    FAddRoot AddRoot;
    const FAssetRegistry* AssetRegistry = nullptr;
    FBrowseAsset BrowseAsset;
    FAssetPath SelectedAsset;
    FObjectHandle BindingOwnerHandle;
    FName BindingPropertyName;
    FObjectHandle BindingTargetHandle;
    FName BindingFunctionName;
    FSetAssetReference SetAssetReference;
    FAssetReferenceWidget AssetReferenceWidget;
};
}
