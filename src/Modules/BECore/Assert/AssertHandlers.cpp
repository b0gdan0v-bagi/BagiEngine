#include "AssertHandlers.h"

#include <BECore/Assert/PlatformDebug.h>
#include <BECore/Assert/StackTrace.h>
#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>

#include <EASTL/sort.h>

namespace BECore {

    // DebugBreakHandler implementation

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

        // Always break on fatal errors and asserts
        // Expects also break if enabled
        if (event.type == AssertType::FatalError || 
            event.type == AssertType::Assert ||
            event.type == AssertType::Expect) {
            ENGINE_DEBUG_BREAK();
        }
    }

    // AssertLogHandler implementation

    void AssertLogHandler::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<AssertEvent, &AssertLogHandler::OnAssert>(this);
        _initialized = true;
    }

    void AssertLogHandler::OnAssert(const AssertEvent& event) {
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
        eastl::string message;
        
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

    // StackTraceHandler implementation

    void StackTraceHandler::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<AssertEvent, &StackTraceHandler::OnAssert>(this);
        _initialized = true;
    }

    void StackTraceHandler::OnAssert(const AssertEvent& event) {
        // Print stack trace for all assertion types
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

    // AssertHandlerManager implementation

    void AssertHandlerManager::Initialize() {
        if (_initialized) {
            return;
        }

        // Получаем конфиг через ConfigManager
        const auto rootNode = CoreManager::GetConfigManager().GetConfig("AssertHandlersConfig"_intern);

        if (!rootNode) {
            // Fallback: create default handlers if config not found
            auto debugHandler = BECore::New<DebugBreakHandler>();
            debugHandler->SetPriority(100);
            _handlers.push_back(debugHandler);

            auto logHandler = BECore::New<AssertLogHandler>();
            logHandler->SetPriority(0);
            _handlers.push_back(logHandler);
        } else {
            const auto handlersNode = rootNode.GetChild("handlers");
            if (handlersNode) {
                for (const auto handlerNode : handlersNode.Children()) {
                    if (handlerNode.Name() != "handler") {
                        continue;
                    }

                    // Check if handler is enabled (default: true)
                    auto enabled = handlerNode.ParseAttribute<bool>("enabled");
                    if (enabled.has_value() && !enabled.value()) {
                        continue;
                    }

                    auto handlerType = handlerNode.ParseAttribute<AssertHandlerType>("type");
                    if (!handlerType) {
                        continue;
                    }

                    auto handler = CreateHandlerByType(*handlerType);
                    if (!handler) {
                        continue;
                    }

                    // Set priority from config (default: 0)
                    auto priority = handlerNode.ParseAttribute<int>("priority");
                    if (priority.has_value()) {
                        handler->SetPriority(*priority);
                    }

                    _handlers.push_back(handler);
                }
            }
        }

        // Sort handlers by priority (lower first)
        SortHandlersByPriority();

        // Initialize all handlers
        for (const auto& handler : _handlers) {
            handler->Initialize();
        }

        _initialized = true;
    }

    IntrusivePtrAtomic<IAssertHandler> AssertHandlerManager::CreateHandlerByType(AssertHandlerType type) {

        switch (type) {
            case AssertHandlerType::DebugBreak:
                return BECore::New<DebugBreakHandler>();
            case AssertHandlerType::Log:
                return BECore::New<AssertLogHandler>();
            case AssertHandlerType::StackTrace:
                return BECore::New<StackTraceHandler>();
            default:
                return {};
        }
    }

    void AssertHandlerManager::SortHandlersByPriority() {
        eastl::sort(_handlers.begin(), _handlers.end(),
            [](auto && a, auto && b) {
                return a->GetPriority() < b->GetPriority();
            });
    }

}  // namespace BECore
