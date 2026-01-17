#pragma once

#include <BECore/Assert/AssertEvent.h>
#include <BECore/Assert/PlatformDebug.h>

/**
 * @file AssertMacros.h
 * @brief Assertion macros for BagiEngine
 * 
 * Provides ASSERT, EXPECT, and FATALERROR macros that emit events
 * through the event system, allowing custom handlers for logging,
 * debug breaks, etc.
 * 
 * Macros can be disabled via CMake options:
 * - ENGINE_ENABLE_ASSERTS: Controls ASSERT and FATALERROR
 * - ENGINE_ENABLE_EXPECTS: Controls EXPECT
 * 
 * @example
 * ASSERT(ptr != nullptr);
 * ASSERT(index < size, "Index out of bounds");
 * EXPECT(value > 0);
 * FATALERROR("Unrecoverable error");
 */

// Helper to get number of variadic args (0 or 1 for our case)
#define ENGINE_ASSERT_GET_MACRO(_1, _2, NAME, ...) NAME

// Helper to stringify expression
#define ENGINE_STRINGIFY(x) #x

// =============================================================================
// ASSERT Macros
// =============================================================================
#if ENGINE_ENABLE_ASSERTS

    /**
     * @brief Internal implementation for ASSERT without message
     */
    #define ENGINE_ASSERT_IMPL(expr) \
        do { \
            if (!(expr)) { \
                ::BECore::AssertEvent::Emit( \
                    __FILE__, \
                    __LINE__, \
                    ENGINE_STRINGIFY(expr), \
                    nullptr, \
                    ::BECore::AssertType::Assert \
                ); \
            } \
        } while (false)

    /**
     * @brief Internal implementation for ASSERT with message
     */
    #define ENGINE_ASSERT_MSG_IMPL(expr, msg) \
        do { \
            if (!(expr)) { \
                ::BECore::AssertEvent::Emit( \
                    __FILE__, \
                    __LINE__, \
                    ENGINE_STRINGIFY(expr), \
                    msg, \
                    ::BECore::AssertType::Assert \
                ); \
            } \
        } while (false)

    /**
     * @brief Assert that an expression is true
     * 
     * If the expression evaluates to false, emits an AssertEvent.
     * Can be used with or without a message:
     * - ASSERT(expr)
     * - ASSERT(expr, "message")
     * 
     * Disabled when ENGINE_ENABLE_ASSERTS is 0.
     */
    #define ASSERT(...) \
        ENGINE_ASSERT_GET_MACRO(__VA_ARGS__, ENGINE_ASSERT_MSG_IMPL, ENGINE_ASSERT_IMPL)(__VA_ARGS__)

    /**
     * @brief Trigger a fatal error unconditionally
     * 
     * Always emits an AssertEvent with FatalError type.
     * Use for unrecoverable error conditions.
     */
    #define FATALERROR(msg) \
        do { \
            ::BECore::AssertEvent::Emit( \
                __FILE__, \
                __LINE__, \
                nullptr, \
                msg, \
                ::BECore::AssertType::FatalError \
            ); \
        } while (false)

#else
    // Asserts disabled - compile to nothing
    #define ASSERT(...) ((void)0)
    #define FATALERROR(msg) ((void)0)
#endif

// =============================================================================
// EXPECT Macros
// =============================================================================
#if ENGINE_ENABLE_EXPECTS

    /**
     * @brief Internal implementation for EXPECT without message
     */
    #define ENGINE_EXPECT_IMPL(expr) \
        do { \
            if (!(expr)) { \
                ::BECore::AssertEvent::Emit( \
                    __FILE__, \
                    __LINE__, \
                    ENGINE_STRINGIFY(expr), \
                    nullptr, \
                    ::BECore::AssertType::Expect \
                ); \
            } \
        } while (false)

    /**
     * @brief Internal implementation for EXPECT with message
     */
    #define ENGINE_EXPECT_MSG_IMPL(expr, msg) \
        do { \
            if (!(expr)) { \
                ::BECore::AssertEvent::Emit( \
                    __FILE__, \
                    __LINE__, \
                    ENGINE_STRINGIFY(expr), \
                    msg, \
                    ::BECore::AssertType::Expect \
                ); \
            } \
        } while (false)

    /**
     * @brief Soft expectation that an expression is true
     * 
     * Similar to ASSERT but can be disabled separately via
     * ENGINE_ENABLE_EXPECTS. Useful for less critical checks
     * that you might want to keep in release builds.
     * 
     * Can be used with or without a message:
     * - EXPECT(expr)
     * - EXPECT(expr, "message")
     */
    #define EXPECT(...) \
        ENGINE_ASSERT_GET_MACRO(__VA_ARGS__, ENGINE_EXPECT_MSG_IMPL, ENGINE_EXPECT_IMPL)(__VA_ARGS__)

#else
    // Expects disabled - compile to nothing
    #define EXPECT(...) ((void)0)
#endif
