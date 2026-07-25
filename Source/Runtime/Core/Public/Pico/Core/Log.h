#pragma once

#include <format>
#include <iostream>
#include <string_view>
#include <utility>

namespace Pico
{
enum class ELogLevel
{
    Trace,
    Info,
    Warning,
    Error
};

class FLog
{
public:
    static constexpr std::string_view DefaultCategory = "LogCore";

    static void Write(ELogLevel Level, std::string_view Message);
    static void Write(std::string_view Category, ELogLevel Level, std::string_view Message);

    template <typename... TArgs>
    static void Trace(std::string_view Category, std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Write(Category, ELogLevel::Trace, std::format(Format, std::forward<TArgs>(Args)...));
    }

    template <typename... TArgs>
    static void Info(std::string_view Category, std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Write(Category, ELogLevel::Info, std::format(Format, std::forward<TArgs>(Args)...));
    }

    template <typename... TArgs>
    static void Warning(std::string_view Category, std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Write(Category, ELogLevel::Warning, std::format(Format, std::forward<TArgs>(Args)...));
    }

    template <typename... TArgs>
    static void Error(std::string_view Category, std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Write(Category, ELogLevel::Error, std::format(Format, std::forward<TArgs>(Args)...));
    }

    template <typename... TArgs>
    static void Trace(std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Trace(DefaultCategory, Format, std::forward<TArgs>(Args)...);
    }

    template <typename... TArgs>
    static void Info(std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Info(DefaultCategory, Format, std::forward<TArgs>(Args)...);
    }

    template <typename... TArgs>
    static void Warning(std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Warning(DefaultCategory, Format, std::forward<TArgs>(Args)...);
    }

    template <typename... TArgs>
    static void Error(std::format_string<TArgs...> Format, TArgs&&... Args)
    {
        Error(DefaultCategory, Format, std::forward<TArgs>(Args)...);
    }
};
}

#define PICO_LOG(Category, Level, ...) \
    ::Pico::FLog::Write(#Category, ::Pico::ELogLevel::Level, std::format(__VA_ARGS__))
