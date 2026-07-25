#pragma once

#include "Pico/Object/Archive.h"

#include <filesystem>
#include <string_view>

namespace Pico
{
class PObject;

enum class EObjectSerializationError
{
    None,
    InvalidArgument,
    InvalidArchive,
    UnsupportedVersion,
    ClassNotFound,
    ObjectCreationFailed,
    PropertyTypeMismatch,
    PropertyAccessFailed,
    PostLoadFailed,
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileTooLarge,
    TrailingData
};

std::string_view ToString(EObjectSerializationError Error);

bool SaveObject(
    FArchive& Archive,
    const PObject* Object,
    EObjectSerializationError* OutError = nullptr);
PObject* LoadObject(
    FArchive& Archive,
    PObject* Outer,
    EObjectSerializationError* OutError = nullptr);

bool SaveObjectToFile(
    const std::filesystem::path& FilePath,
    const PObject* Object,
    EObjectSerializationError* OutError = nullptr);
PObject* LoadObjectFromFile(
    const std::filesystem::path& FilePath,
    PObject* Outer,
    EObjectSerializationError* OutError = nullptr);
}
