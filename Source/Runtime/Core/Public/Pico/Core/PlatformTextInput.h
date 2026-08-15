#pragma once

namespace Pico
{
class FPlatformTextInputContext
{
public:
    explicit FPlatformTextInputContext(void* NativeWindow = nullptr);
    ~FPlatformTextInputContext();

    FPlatformTextInputContext(const FPlatformTextInputContext&) = delete;
    FPlatformTextInputContext& operator=(const FPlatformTextInputContext&) = delete;
    FPlatformTextInputContext(FPlatformTextInputContext&& Other) noexcept;
    FPlatformTextInputContext& operator=(FPlatformTextInputContext&& Other) noexcept;

    void SetNativeWindow(void* NativeWindow);
    void SetTextInputEnabled(bool bEnabled);
    bool IsTextInputEnabled() const;

private:
    void RestoreTextInput();

    void* NativeWindow = nullptr;
    void* SavedInputContext = nullptr;
    bool bTextInputEnabled = true;
    bool bHasSavedInputContext = false;
};
}
