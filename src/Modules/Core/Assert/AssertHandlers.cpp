#include "AssertHandlers.h"

#include <Core/Assert/PlatformDebug.h>
#include <Core/Logger/Logger.h>

#include <fmt/core.h>

namespace Core {

    void DebugBreakHandler::Initialize() {
        if (_initialized) {
            return;
        }

        AssertEvent::Subscribe<&DebugBreakHandler::OnAssert>(this);
        _initialized = true;
    }

    void DebugBreakHandler::OnAssert(const AssertEvent& event) {
        if (!_enabled) {
            return;
        }

        // Always break on fatal errors and asserts
        // Expects also break if enabled
        if (event.type == AssertType::FatalError || 
            event.type == AssertType::Assert ||
            event.type == AssertType::Expect) {
            ENGINE_DEBUG_BREAK();
        }
    }

    void LogHandler::Initialize() {
        if (_initialized) {
            return;
        }

        AssertEvent::Subscribe<&LogHandler::OnAssert>(this);
        _initialized = true;
    }

    void LogHandler::OnAssert(const AssertEvent& event) {
        auto& logger = Logger::GetInstance();

        // Determine log level based on assert type
        LogLevel level = LogLevel::Error;
        const char* typeStr = "ASSERT";

        switch (event.type) {
            case AssertType::Assert:
                level = LogLevel::Error;
                typeStr = "ASSERT";
                break;
            case AssertType::Expect:
                level = LogLevel::Warning;
                typeStr = "EXPECT";
                break;
            case AssertType::FatalError:
                level = LogLevel::Fatal;
                typeStr = "FATAL";
                break;
        }

        // Build the message
        std::string message;
        
        if (event.expression) {
            if (event.message) {
                message = fmt::format("{} failed: {} - {}", typeStr, event.expression, event.message);
            } else {
                message = fmt::format("{} failed: {}", typeStr, event.expression);
            }
        } else {
            // FATALERROR case - no expression, just message
            if (event.message) {
                message = fmt::format("{}: {}", typeStr, event.message);
            } else {
                message = fmt::format("{}: (no message)", typeStr);
            }
        }

        logger.LogRaw(level, message.c_str(), event.file, event.line);
    }

    void InitializeAssertHandlers() {
        DebugBreakHandler::GetInstance().Initialize();
        LogHandler::GetInstance().Initialize();
    }

}  // namespace Core
