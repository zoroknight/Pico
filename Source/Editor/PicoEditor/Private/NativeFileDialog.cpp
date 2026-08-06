#include "NativeFileDialog.h"

#if defined(_WIN32)
#include <Windows.h>
#include <commdlg.h>
#endif

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
}
