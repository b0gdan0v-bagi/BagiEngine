#include "LoggerManager.h"

#include <BECore/Config/XmlConfig.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/RefCounted/New.h>

// Include generated factory - provides ILogSinkType enum and ILogSinkFactory
#include <Generated/ILogSink.gen.hpp>
#include <Generated/EnumLogSink.gen.hpp>

#include <EASTL/sort.h>

namespace BECore {

    void LoggerManager::Initialize() {
        if (_initialized) {
            return;
        }

        const XmlConfig config = XmlConfig::Create();
        constexpr std::string_view configPath = "config/LoggerConfig.xml";

        if (!config.LoadFromVirtualPath(configPath)) {
            // Fallback: create default sinks if config not found
            auto consoleSink = BECore::New<ConsoleSink>();
            consoleSink->SetPriority(0);
            _sinks.push_back(consoleSink);
        } else {
            const auto rootNode = config.GetRoot();
            if (rootNode) {
                const auto sinksNode = rootNode.GetChild("sinks");
                if (sinksNode) {
                    for (const auto sinkNode : sinksNode.Children()) {
                        if (sinkNode.Name() != "sink") {
                            continue;
                        }

                        // Check if sink is enabled (default: true)
                        auto enabled = sinkNode.ParseAttribute<bool>("enabled");
                        if (enabled.has_value() && !enabled.value()) {
                            continue;
                        }

                        auto sinkType = sinkNode.ParseAttribute<LogSinkType>("type");
                        if (!sinkType) {
                            continue;
                        }

                        auto sink = CreateSinkByType(*sinkType);
                        if (!sink) {
                            continue;
                        }

                        auto priority = sinkNode.ParseAttribute<int>("priority");
                        if (priority.has_value()) {
                            sink->SetPriority(*priority);
                        }

                        auto minLevel = sinkNode.ParseAttribute<LogLevel>("minLevel");
                        if (minLevel.has_value()) {
                            sink->SetMinLevel(*minLevel);
                        }

                        sink->Configure(sinkNode);

                        _sinks.push_back(sink);
                    }
                }
            }
        }

        // Sort sinks by priority (lower first)
        SortSinksByPriority();

        // Initialize all sinks
        for (auto& sink : _sinks) {
            sink->Initialize();
        }

        // Subscribe to FlushEvent
        FlushLogsEvent::Subscribe<&LoggerManager::OnFlushEvent>(this);

        _initialized = true;
    }

    void LoggerManager::OnFlushEvent() {
        for (auto& sink : _sinks) {
            sink->Flush();
        }
    }

    IntrusivePtr<ILogSink> LoggerManager::CreateSinkByType(LogSinkType type) {
        return LogSinkFactory::Create(type);
    }

    void LoggerManager::SortSinksByPriority() {
        eastl::sort(_sinks.begin(), _sinks.end(),
            [](const IntrusivePtr<ILogSink>& a, const IntrusivePtr<ILogSink>& b) {
                return a->GetPriority() < b->GetPriority();
            });
    }

}  // namespace BECore
