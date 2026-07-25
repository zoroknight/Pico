#pragma once

#if defined(_WIN32)
    #define PICO_PLATFORM_WINDOWS 1
#else
    #define PICO_PLATFORM_WINDOWS 0
#endif

#if defined(_MSC_VER)
    #define PICO_COMPILER_MSVC 1
#else
    #define PICO_COMPILER_MSVC 0
#endif

#if defined(__clang__)
    #define PICO_COMPILER_CLANG 1
#else
    #define PICO_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
    #define PICO_COMPILER_GCC 1
#else
    #define PICO_COMPILER_GCC 0
#endif

#if defined(NDEBUG)
    #define PICO_CONFIG_DEBUG 0
    #define PICO_CONFIG_RELEASE 1
#else
    #define PICO_CONFIG_DEBUG 1
    #define PICO_CONFIG_RELEASE 0
#endif

#if PICO_COMPILER_MSVC
    #define PICO_DEBUG_BREAK() __debugbreak()
#elif PICO_COMPILER_CLANG || PICO_COMPILER_GCC
    #define PICO_DEBUG_BREAK() __builtin_trap()
#else
    #define PICO_DEBUG_BREAK() ((void)0)
#endif
