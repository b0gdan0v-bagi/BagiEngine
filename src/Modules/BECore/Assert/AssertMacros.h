#pragma once

#include <BECore/Assert/AssertEvent.h>
#include <BECore/Assert/PlatformDebug.h>

/**
 * @file AssertMacros.h
 * @brief ASSERT, EXPECT and FATALERROR: a failure emits an AssertEvent that the handlers (log, debug break, stack trace) receive.
 *
 * The CMake option ENGINE_ENABLE_ASSERTS switches ASSERT and FATALERROR, ENGINE_ENABLE_EXPECTS switches EXPECT; a switched-off macro
 * compiles to nothing, so its arguments are never evaluated and must have no side effects.
 */

// Picks the one-argument or the two-argument form by the number of arguments.
#define ENGINE_ASSERT_GET_MACRO(_1, _2, NAME, ...) NAME

#define ENGINE_STRINGIFY(x) #x

// =============================================================================
// ASSERT Macros
// =============================================================================
#if ENGINE_ENABLE_ASSERTS

#define ENGINE_ASSERT_IMPL(expr)                                                                                                                                                                       \
    do {                                                                                                                                                                                               \
        if (!(expr)) {                                                                                                                                                                                 \
            ::BECore::AssertEvent::Emit(__FILE__, __LINE__, ENGINE_STRINGIFY(expr), nullptr, ::BECore::AssertType::Assert);                                                                            \
        }                                                                                                                                                                                              \
    } while (false)

#define ENGINE_ASSERT_MSG_IMPL(expr, msg)                                                                                                                                                              \
    do {                                                                                                                                                                                               \
        if (!(expr)) {                                                                                                                                                                                 \
            ::BECore::AssertEvent::Emit(__FILE__, __LINE__, ENGINE_STRINGIFY(expr), msg, ::BECore::AssertType::Assert);                                                                                \
        }                                                                                                                                                                                              \
    } while (false)

/// Emits an AssertEvent when `expr` is false; the message is optional (ASSERT(expr) or ASSERT(expr, "message")) and evaluated only on failure.
#define ASSERT(...) ENGINE_ASSERT_GET_MACRO(__VA_ARGS__, ENGINE_ASSERT_MSG_IMPL, ENGINE_ASSERT_IMPL)(__VA_ARGS__)

/// Emits a FatalError event unconditionally (the event has no expression).
#define FATALERROR(msg)                                                                                                                                                                                \
    do {                                                                                                                                                                                               \
        ::BECore::AssertEvent::Emit(__FILE__, __LINE__, nullptr, msg, ::BECore::AssertType::FatalError);                                                                                               \
    } while (false)

#else
#define ASSERT(...) ((void)0)
#define FATALERROR(msg) ((void)0)
#endif

// =============================================================================
// EXPECT Macros
// =============================================================================
#if ENGINE_ENABLE_EXPECTS

#define ENGINE_EXPECT_IMPL(expr)                                                                                                                                                                       \
    do {                                                                                                                                                                                               \
        if (!(expr)) {                                                                                                                                                                                 \
            ::BECore::AssertEvent::Emit(__FILE__, __LINE__, ENGINE_STRINGIFY(expr), nullptr, ::BECore::AssertType::Expect);                                                                            \
        }                                                                                                                                                                                              \
    } while (false)

#define ENGINE_EXPECT_MSG_IMPL(expr, msg)                                                                                                                                                              \
    do {                                                                                                                                                                                               \
        if (!(expr)) {                                                                                                                                                                                 \
            ::BECore::AssertEvent::Emit(__FILE__, __LINE__, ENGINE_STRINGIFY(expr), msg, ::BECore::AssertType::Expect);                                                                                \
        }                                                                                                                                                                                              \
    } while (false)

/// Like ASSERT, for a less critical check, so it can stay on in a build where asserts are off.
#define EXPECT(...) ENGINE_ASSERT_GET_MACRO(__VA_ARGS__, ENGINE_EXPECT_MSG_IMPL, ENGINE_EXPECT_IMPL)(__VA_ARGS__)

#else
#define EXPECT(...) ((void)0)
#endif
