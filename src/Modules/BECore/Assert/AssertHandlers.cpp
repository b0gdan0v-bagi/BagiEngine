#include "AssertHandlers.h"

#include <BECore/Assert/PlatformDebug.h>
#include <BECore/Assert/StackTrace.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <EASTL/sort.h>
#include <Generated/AssertHandlers.gen.hpp>

namespace BECore {

    void DebugBreakHandler::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<AssertEvent, &DebugBreakHandler::OnAssert>(this);
        _initialized = true;
    }

    void DebugBreakHandler::OnAssert(const AssertEvent& event) {
        if (!_enabled) {
            return;
        }

        if (event.type == AssertType::FatalError || event.type == AssertType::Assert || event.type == AssertType::Expect) {
            ENGINE_DEBUG_BREAK();
        }
    }

    void LogHandler::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<AssertEvent, &LogHandler::OnAssert>(this);
        _initialized = true;
    }

    void LogHandler::OnAssert(const AssertEvent& event) {
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

        String message;

        if (event.expression) {
            if (event.message) {

                message = Format("{} failed: {} - {}", typeStr, event.expression, event.message);
            } else {
                message = Format("{} failed: {}", typeStr, event.expression);
            }
        } else {
            // FATALERROR case - no expression, just message
            if (event.message) {
                message = Format("{}: {}", typeStr, event.message);
            } else {
                message = Format("{}: (no message)", typeStr);
            }
        }

        LogEvent::Emit(level, message);
    }

    void StackTraceHandler::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<AssertEvent, &StackTraceHandler::OnAssert>(this);
        _initialized = true;
    }

    void StackTraceHandler::OnAssert(const AssertEvent& event) {
        fprintf(stderr, "\n[ASSERT] Stack trace for %s:%d\n", event.file, event.line);
        if (event.expression) {
            fprintf(stderr, "  Expression: %s\n", event.expression);
        }
        if (event.message) {
            fprintf(stderr, "  Message: %s\n", event.message);
        }

        // Skip 1 frame (this function itself)
        CaptureAndPrintStackTrace(1);
    }

    void AssertHandlerManager::Initialize() {
        if (_initialized) {
            return;
        }

        if (_handlers.empty()) {
            // Fallback: create default handlers if config not found or empty
            auto logHandler = BECore::New<LogHandler>();
            _handlers.push_back(logHandler);

            auto debugHandler = BECore::New<DebugBreakHandler>();
            debugHandler->SetPriority(100);
            _handlers.push_back(debugHandler);
        }

        SortHandlersByPriority();

        for (const auto& handler : _handlers) {
            handler->Initialize();
        }

        _initialized = true;
    }

    void AssertHandlerManager::SortHandlersByPriority() {
        eastl::sort(_handlers.begin(), _handlers.end(), [](auto&& a, auto&& b) { return a->GetPriority() < b->GetPriority(); });
    }

}  // namespace BECore
