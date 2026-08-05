#pragma once

#include "Pico/Engine/WorldSerialization.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class PWorld;

struct FEditorWorldSnapshot
{
    FWorldAssetData WorldData;
    std::string SelectedObjectPath;
};

class FEditorTransactionManager
{
public:
    using FRestoreSnapshot = std::function<bool(
        const FEditorWorldSnapshot&,
        EWorldSerializationError*)>;

    explicit FEditorTransactionManager(std::size_t MaxHistoryEntries = 64);

    bool Begin(
        std::string Description,
        const PWorld& World,
        std::string SelectedObjectPath,
        EWorldSerializationError* OutError = nullptr);
    bool Commit(
        const PWorld& World,
        std::string SelectedObjectPath,
        EWorldSerializationError* OutError = nullptr);
    void Cancel();
    bool Rollback(
        const FRestoreSnapshot& RestoreSnapshot,
        EWorldSerializationError* OutError = nullptr);
    void Clear();

    bool Undo(
        const FRestoreSnapshot& RestoreSnapshot,
        std::string* OutDescription = nullptr,
        EWorldSerializationError* OutError = nullptr);
    bool Redo(
        const FRestoreSnapshot& RestoreSnapshot,
        std::string* OutDescription = nullptr,
        EWorldSerializationError* OutError = nullptr);

    bool HasPendingTransaction() const;
    bool CanUndo() const;
    bool CanRedo() const;
    std::string_view GetUndoDescription() const;
    std::string_view GetRedoDescription() const;
    std::size_t GetUndoCount() const;
    std::size_t GetRedoCount() const;

private:
    struct FTransaction
    {
        std::string Description;
        FEditorWorldSnapshot Before;
        FEditorWorldSnapshot After;
    };

    bool CaptureSnapshot(
        const PWorld& World,
        std::string SelectedObjectPath,
        FEditorWorldSnapshot& OutSnapshot,
        EWorldSerializationError* OutError) const;
    void PushWithLimit(
        std::vector<FTransaction>& Stack,
        FTransaction Transaction);

    std::size_t MaxHistoryEntries = 64;
    std::optional<FTransaction> PendingTransaction;
    std::vector<FTransaction> UndoStack;
    std::vector<FTransaction> RedoStack;
};
}
