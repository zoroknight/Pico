#include "Pico/Core/PlatformTextInput.h"

#include "Pico/Core/Platform.h"

#include <utility>

#if PICO_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <imm.h>
#endif

namespace Pico
{
FPlatformTextInputContext::FPlatformTextInputContext(void* InNativeWindow)
    : NativeWindow(InNativeWindow)
{
}

FPlatformTextInputContext::~FPlatformTextInputContext()
{
    RestoreTextInput();
}

FPlatformTextInputContext::FPlatformTextInputContext(
    FPlatformTextInputContext&& Other) noexcept
    : NativeWindow(std::exchange(Other.NativeWindow, nullptr))
    , SavedInputContext(std::exchange(Other.SavedInputContext, nullptr))
    , bTextInputEnabled(std::exchange(Other.bTextInputEnabled, true))
    , bHasSavedInputContext(std::exchange(Other.bHasSavedInputContext, false))
{
}

FPlatformTextInputContext& FPlatformTextInputContext::operator=(
    FPlatformTextInputContext&& Other) noexcept
{
    if (this != &Other)
    {
        RestoreTextInput();
        NativeWindow = std::exchange(Other.NativeWindow, nullptr);
        SavedInputContext = std::exchange(Other.SavedInputContext, nullptr);
        bTextInputEnabled = std::exchange(Other.bTextInputEnabled, true);
        bHasSavedInputContext = std::exchange(Other.bHasSavedInputContext, false);
    }
    return *this;
}

void FPlatformTextInputContext::SetNativeWindow(void* InNativeWindow)
{
    if (NativeWindow == InNativeWindow)
    {
        return;
    }
    const bool bWasEnabled = bTextInputEnabled;
    RestoreTextInput();
    NativeWindow = InNativeWindow;
    if (!bWasEnabled)
    {
        SetTextInputEnabled(false);
    }
}

void FPlatformTextInputContext::SetTextInputEnabled(bool bEnabled)
{
    if (bTextInputEnabled == bEnabled)
    {
        return;
    }

#if PICO_PLATFORM_WINDOWS
    if (NativeWindow != nullptr)
    {
        HWND WindowHandle = static_cast<HWND>(NativeWindow);
        if (!bEnabled)
        {
            SavedInputContext = ImmAssociateContext(WindowHandle, nullptr);
            bHasSavedInputContext = true;
        }
        else if (bHasSavedInputContext)
        {
            ImmAssociateContext(
                WindowHandle, static_cast<HIMC>(SavedInputContext));
            SavedInputContext = nullptr;
            bHasSavedInputContext = false;
        }
    }
#endif

    bTextInputEnabled = bEnabled;
}

bool FPlatformTextInputContext::IsTextInputEnabled() const
{
    return bTextInputEnabled;
}

void FPlatformTextInputContext::RestoreTextInput()
{
    if (!bTextInputEnabled)
    {
        SetTextInputEnabled(true);
    }
}
}
