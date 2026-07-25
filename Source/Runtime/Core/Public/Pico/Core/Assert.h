#pragma once

#include "Pico/Core/Log.h"
#include "Pico/Core/Platform.h"

#include <cstdlib>

#define PICO_CHECK(Expression) \
    do \
    { \
        if (!(Expression)) \
        { \
            PICO_LOG(LogCore, Error, "Check failed: {} ({}:{})", #Expression, __FILE__, __LINE__); \
            PICO_DEBUG_BREAK(); \
            std::abort(); \
        } \
    } while (false)

#define PICO_CHECK_MSG(Expression, ...) \
    do \
    { \
        if (!(Expression)) \
        { \
            PICO_LOG(LogCore, Error, __VA_ARGS__); \
            PICO_LOG(LogCore, Error, "Check failed: {} ({}:{})", #Expression, __FILE__, __LINE__); \
            PICO_DEBUG_BREAK(); \
            std::abort(); \
        } \
    } while (false)

#define PICO_ENSURE(Expression) \
    ([&]() -> bool \
    { \
        if (!(Expression)) \
        { \
            PICO_LOG(LogCore, Error, "Ensure failed: {} ({}:{})", #Expression, __FILE__, __LINE__); \
            return false; \
        } \
        return true; \
    }())
