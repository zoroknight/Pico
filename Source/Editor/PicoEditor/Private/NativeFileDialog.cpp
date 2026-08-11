#include "NativeFileDialog.h"

#if defined(_WIN32)
#include <Windows.h>
#include <commdlg.h>
#endif

#include <algorithm>
#include <array>

namespace Pico
{
std::optional<std::filesystem::path> OpenObjFileDialog()
{
#if defined(_WIN32)
    std::array<wchar_t, 32768> Buffer {};
    OPENFILENAMEW Dialog {};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = GetActiveWindow();
    Dialog.lpstrFilter = L"Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0\0";
    Dialog.lpstrFile = Buffer.data();
    Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
    Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    Dialog.lpstrDefExt = L"obj";
    if (GetOpenFileNameW(&Dialog) != FALSE)
    {
        return std::filesystem::path(Buffer.data());
    }
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> OpenTextureFileDialog()
{
#if defined(_WIN32)
    std::array<wchar_t, 32768> Buffer {};
    OPENFILENAMEW Dialog {};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = GetActiveWindow();
    Dialog.lpstrFilter =
        L"Image Files (*.png;*.jpg;*.jpeg;*.tga;*.bmp)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
        L"All Files (*.*)\0*.*\0\0";
    Dialog.lpstrFile = Buffer.data();
    Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
    Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&Dialog) != FALSE)
    {
        return std::filesystem::path(Buffer.data());
    }
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> OpenSkeletalMeshFileDialog()
{
#if defined(_WIN32)
    std::array<wchar_t, 32768> Buffer {};
    OPENFILENAMEW Dialog {};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = GetActiveWindow();
    Dialog.lpstrFilter =
        L"Skeletal Models (*.gltf;*.glb;*.fbx)\0*.gltf;*.glb;*.fbx\0"
        L"glTF (*.gltf;*.glb)\0*.gltf;*.glb\0"
        L"Autodesk FBX (*.fbx)\0*.fbx\0"
        L"All Files (*.*)\0*.*\0\0";
    Dialog.lpstrFile = Buffer.data();
    Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
    Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&Dialog) != FALSE)
    {
        return std::filesystem::path(Buffer.data());
    }
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> OpenWorldFileDialog(
    const std::filesystem::path& InitialDirectory)
{
#if defined(_WIN32)
    std::array<wchar_t, 32768> Buffer {};
    const std::wstring Directory = InitialDirectory.wstring();
    OPENFILENAMEW Dialog {};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = GetActiveWindow();
    Dialog.lpstrFilter = L"Pico World (*.pworld)\0*.pworld\0All Files (*.*)\0*.*\0\0";
    Dialog.lpstrFile = Buffer.data();
    Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
    Dialog.lpstrInitialDir = Directory.empty() ? nullptr : Directory.c_str();
    Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    Dialog.lpstrDefExt = L"pworld";
    if (GetOpenFileNameW(&Dialog) != FALSE)
    {
        return std::filesystem::path(Buffer.data());
    }
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> SaveWorldFileDialog(
    const std::filesystem::path& InitialDirectory,
    const std::filesystem::path& SuggestedFile)
{
#if defined(_WIN32)
    std::array<wchar_t, 32768> Buffer {};
    const std::wstring Suggested = SuggestedFile.filename().wstring();
    std::copy_n(
        Suggested.data(),
        (std::min)(Suggested.size(), Buffer.size() - 1),
        Buffer.data());
    const std::wstring Directory = InitialDirectory.wstring();
    OPENFILENAMEW Dialog {};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = GetActiveWindow();
    Dialog.lpstrFilter = L"Pico World (*.pworld)\0*.pworld\0\0";
    Dialog.lpstrFile = Buffer.data();
    Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
    Dialog.lpstrInitialDir = Directory.empty() ? nullptr : Directory.c_str();
    Dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    Dialog.lpstrDefExt = L"pworld";
    if (GetSaveFileNameW(&Dialog) != FALSE)
    {
        return std::filesystem::path(Buffer.data());
    }
#endif
    return std::nullopt;
}
}
