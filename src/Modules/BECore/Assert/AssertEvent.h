#pragma once

#include <Events/EventBase.h>

namespace BECore {

    /**
     * @brief Type of assertion that was triggered
     */
    enum class AssertType {
        Assert,     ///< Regular assertion (ASSERT macro)
        Expect,     ///< Soft expectation (EXPECT macro)
        FatalError  ///< Fatal error (FATALERROR macro)
    };

    /**
     * @brief Event emitted when an assertion fails
     * 
     * Contains all information about the failed assertion including
     * file location, expression, and optional message.
     * 
     * Handlers can subscribe to this event to implement custom behavior
     * such as logging, debug breaks, or crash reporting.
     * 
     * @example
     * // Subscribe to assert events
     * AssertEvent::Subscribe<&MyHandler::OnAssert>(this);
     * 
     * void OnAssert(const AssertEvent& event) {
     *     // Handle assertion failure
     * }
     */
    struct AssertEvent : EventBase<AssertEvent> {
        const char* file;        ///< Source file where assertion failed
        int line;                ///< Line number where assertion failed
        const char* expression;  ///< The expression that was evaluated (nullptr for FATALERROR)
        const char* message;     ///< Optional user message (nullptr if not provided)
        AssertType type;         ///< Type of assertion

        /**
         * @brief Construct an AssertEvent
         * 
         * @param file Source file path
         * @param line Line number
         * @param expression Expression that failed (can be nullptr)
         * @param message Optional message (can be nullptr)
         * @param type Type of assertion
         */
        AssertEvent(
            const char* file,
            int line,
            const char* expression,
            const char* message,
            AssertType type
        )
            : file(file)
            , line(line)
            , expression(expression)
            , message(message)
            , type(type)
        {}
    };

}  // namespace BECore
