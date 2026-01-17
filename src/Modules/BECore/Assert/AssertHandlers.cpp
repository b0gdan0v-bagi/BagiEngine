#include "AssertHandlers.h"

#include <BECore/Assert/PlatformDebug.h>
#include <BECore/Config/XmlConfig.h>
#include <BECore/Logger/Logger.h>

#include <fmt/core.h>
#include <EASTL/sort.h>

namespace BECore {

    // DebugBreakHandler implementation

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

    // AssertLogHandler implementation

    void AssertLogHandler::Initialize() {
        if (_initialized) {
            return;
        }

        AssertEvent::Subscribe<&AssertLogHandler::OnAssert>(this);
        _initialized = true;
    }

    void AssertLogHandler::OnAssert(const AssertEvent& event) {
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

    // AssertHandlerManager implementation

    void AssertHandlerManager::Initialize() {
        if (_initialized) {
            return;
        }

        XmlConfig config = XmlConfig::Create();
        constexpr std::string_view configPath = "config/AssertHandlersConfig.xml";

        if (!config.LoadFromVirtualPath(configPath)) {
            // Fallback: create default handlers if config not found
            auto debugHandler = BECore::New<DebugBreakHandler>();
            debugHandler->SetPriority(100);
            _handlers.push_back(debugHandler);

            auto logHandler = BECore::New<AssertLogHandler>();
            logHandler->SetPriority(0);
            _handlers.push_back(logHandler);
        } else {
            const auto rootNode = config.GetRoot();
            if (rootNode) {
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
                            // Use dynamic_cast to set priority on concrete types
                            if (auto* debugHandler = dynamic_cast<DebugBreakHandler*>(handler.Get())) {
                                debugHandler->SetPriority(*priority);
                            } else if (auto* logHandler = dynamic_cast<AssertLogHandler*>(handler.Get())) {
                                logHandler->SetPriority(*priority);
                            }
                        }

                        _handlers.push_back(handler);
                    }
                }
            }
        }

        // Sort handlers by priority (lower first)
        SortHandlersByPriority();

        // Initialize all handlers
        for (auto& handler : _handlers) {
            handler->Initialize();
        }

        _initialized = true;
    }

    IntrusivePtr<IAssertHandler> AssertHandlerManager::CreateHandlerByType(AssertHandlerType type) {
        switch (type) {
            case AssertHandlerType::DebugBreak:
                return BECore::New<DebugBreakHandler>();
            case AssertHandlerType::Log:
                return BECore::New<AssertLogHandler>();
            default:
                return {};
        }
    }

    void AssertHandlerManager::SortHandlersByPriority() {
        eastl::sort(_handlers.begin(), _handlers.end(),
            [](const IntrusivePtr<IAssertHandler>& a, const IntrusivePtr<IAssertHandler>& b) {
                return a->GetPriority() < b->GetPriority();
            });
    }

}  // namespace BECore
