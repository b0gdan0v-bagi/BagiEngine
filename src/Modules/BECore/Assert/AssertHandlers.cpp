#include "AssertHandlers.h"

#include <BECore/Assert/PlatformDebug.h>
#include <BECore/Assert/StackTrace.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <EASTL/sort.h>
#include <Generated/AssertHandlers.gen.hpp>

namespace BECore {

    void DebugBreakHandler::Initialize() {
        Disconnect();
        Subscribe<AssertEvent, &DebugBreakHandler::OnAssert>(this);
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
        Disconnect();
        Subscribe<AssertEvent, &LogHandler::OnAssert>(this);
    }

    void LogHandler::OnAssert(const AssertEvent& event) {
        const char* typeStr = "ASSERT";

        switch (event.type) {
            case AssertType::Assert:
                typeStr = "ASSERT";
                break;
            case AssertType::Expect:
                typeStr = "EXPECT";
                break;
            case AssertType::FatalError:
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

        const LogSite site = MakeLogSite(event.file, event.line);

        // A failed assert may be followed by a trap or an abort, so its line has to reach the sinks first; an Expect is a warning and the frame flush takes it
        switch (event.type) {
            case AssertType::Assert:
                Logger::Error(message, site);
                Logger::FlushOnMainThread();
                break;
            case AssertType::Expect:
                Logger::Warning(message, site);
                break;
            case AssertType::FatalError:
                Logger::Fatal(message, site);  // flushes on the main thread itself
                break;
        }
    }

    void StackTraceHandler::Initialize() {
        Disconnect();
        Subscribe<AssertEvent, &StackTraceHandler::OnAssert>(this);
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

        // Publish calls the last connected listener first, and a disconnect moves the last entry into the freed slot, so the order the deserializer left
        // depends on the config. Disconnect every handler first, then connect from the highest priority number to the lowest: the lowest runs first.
        for (const auto& handler : _handlers) {
            handler->Disconnect();
        }
        for (size_t pos = _handlers.size(); pos > 0; --pos) {
            _handlers[pos - 1]->Initialize();
        }

        _initialized = true;
    }

    void AssertHandlerManager::SortHandlersByPriority() {
        // Insertion sort is stable (handlers of one priority keep the order of the config) and allocates nothing
        eastl::insertion_sort(_handlers.begin(), _handlers.end(), [](auto&& a, auto&& b) { return a->GetPriority() < b->GetPriority(); });
    }

}  // namespace BECore
