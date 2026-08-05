#include "Pico/Editor/EditorTransactionManager.h"

#include "Pico/Engine/World.h"

#include <algorithm>
#include <utility>

namespace Pico
{
namespace
{
void ReportError(
    EWorldSerializationError* OutError,
    EWorldSerializationError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
}
}

FEditorTransactionManager::FEditorTransactionManager(
    std::size_t InMaxHistoryEntries)
    : MaxHistoryEntries(std::max<std::size_t>(InMaxHistoryEntries, 1))
{
}

bool FEditorTransactionManager::Begin(
    std::string Description,
    const PWorld& World,
    std::string SelectedObjectPath,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (Description.empty() || PendingTransaction.has_value())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }

    FTransaction Transaction;
    Transaction.Description = std::move(Description);
    if (!CaptureSnapshot(
            World,
            std::move(SelectedObjectPath),
            Transaction.Before,
            OutError))
    {
        return false;
    }

    PendingTransaction = std::move(Transaction);
    return true;
}

bool FEditorTransactionManager::Commit(
    const PWorld& World,
    std::string SelectedObjectPath,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (!PendingTransaction.has_value())
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }
    if (!CaptureSnapshot(
            World,
            std::move(SelectedObjectPath),
            PendingTransaction->After,
            OutError))
    {
        return false;
    }

    PushWithLimit(UndoStack, std::move(*PendingTransaction));
    PendingTransaction.reset();
    RedoStack.clear();
    return true;
}

void FEditorTransactionManager::Cancel()
{
    PendingTransaction.reset();
}

bool FEditorTransactionManager::Rollback(
    const FRestoreSnapshot& RestoreSnapshot,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (!PendingTransaction.has_value() || !RestoreSnapshot)
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }

    if (!RestoreSnapshot(PendingTransaction->Before, OutError))
    {
        return false;
    }

    PendingTransaction.reset();
    return true;
}

void FEditorTransactionManager::Clear()
{
    PendingTransaction.reset();
    UndoStack.clear();
    RedoStack.clear();
}

bool FEditorTransactionManager::Undo(
    const FRestoreSnapshot& RestoreSnapshot,
    std::string* OutDescription,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (OutDescription != nullptr)
    {
        OutDescription->clear();
    }
    if (PendingTransaction.has_value()
        || UndoStack.empty()
        || !RestoreSnapshot)
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }

    FTransaction& Transaction = UndoStack.back();
    if (!RestoreSnapshot(Transaction.Before, OutError))
    {
        return false;
    }

    if (OutDescription != nullptr)
    {
        *OutDescription = Transaction.Description;
    }
    PushWithLimit(RedoStack, std::move(Transaction));
    UndoStack.pop_back();
    return true;
}

bool FEditorTransactionManager::Redo(
    const FRestoreSnapshot& RestoreSnapshot,
    std::string* OutDescription,
    EWorldSerializationError* OutError)
{
    ReportError(OutError, EWorldSerializationError::None);
    if (OutDescription != nullptr)
    {
        OutDescription->clear();
    }
    if (PendingTransaction.has_value()
        || RedoStack.empty()
        || !RestoreSnapshot)
    {
        ReportError(OutError, EWorldSerializationError::InvalidArgument);
        return false;
    }

    FTransaction& Transaction = RedoStack.back();
    if (!RestoreSnapshot(Transaction.After, OutError))
    {
        return false;
    }

    if (OutDescription != nullptr)
    {
        *OutDescription = Transaction.Description;
    }
    PushWithLimit(UndoStack, std::move(Transaction));
    RedoStack.pop_back();
    return true;
}

bool FEditorTransactionManager::HasPendingTransaction() const
{
    return PendingTransaction.has_value();
}

bool FEditorTransactionManager::CanUndo() const
{
    return !PendingTransaction.has_value() && !UndoStack.empty();
}

bool FEditorTransactionManager::CanRedo() const
{
    return !PendingTransaction.has_value() && !RedoStack.empty();
}

std::string_view FEditorTransactionManager::GetUndoDescription() const
{
    return CanUndo() ? std::string_view(UndoStack.back().Description) : std::string_view {};
}

std::string_view FEditorTransactionManager::GetRedoDescription() const
{
    return CanRedo() ? std::string_view(RedoStack.back().Description) : std::string_view {};
}

std::size_t FEditorTransactionManager::GetUndoCount() const
{
    return UndoStack.size();
}

std::size_t FEditorTransactionManager::GetRedoCount() const
{
    return RedoStack.size();
}

bool FEditorTransactionManager::CaptureSnapshot(
    const PWorld& World,
    std::string SelectedObjectPath,
    FEditorWorldSnapshot& OutSnapshot,
    EWorldSerializationError* OutError) const
{
    FEditorWorldSnapshot Snapshot;
    if (!CaptureWorld(World, Snapshot.WorldData, OutError))
    {
        return false;
    }
    Snapshot.SelectedObjectPath = std::move(SelectedObjectPath);
    OutSnapshot = std::move(Snapshot);
    return true;
}

void FEditorTransactionManager::PushWithLimit(
    std::vector<FTransaction>& Stack,
    FTransaction Transaction)
{
    if (Stack.size() == MaxHistoryEntries)
    {
        Stack.erase(Stack.begin());
    }
    Stack.push_back(std::move(Transaction));
}
}
