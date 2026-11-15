#pragma once

#include <Events/EventBase.h>

namespace BECore {

    enum class AssertType { Assert, Expect, FatalError };

    struct AssertEvent : EventBase<AssertEvent> {
        const char* file;
        int line;
        const char* expression;  ///< nullptr for FATALERROR
        const char* message;     ///< nullptr when the macro had none
        AssertType type;

        AssertEvent(const char* file, int line, const char* expression, const char* message, AssertType type) : file(file), line(line), expression(expression), message(message), type(type) {}
    };

}  // namespace BECore
