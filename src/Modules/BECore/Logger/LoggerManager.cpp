#include "LoggerManager.h"

#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/RefCounted/New.h>

#include <Generated/ILogSink.gen.hpp>
#include <Generated/EnumLogSink.gen.hpp>

#include <EASTL/sort.h>

namespace BECore {

    void LoggerManager::Initialize() {
        if (_initialized) {
            return;
        }

        // Получаем конфиг через ConfigManager
        const auto rootNode = CoreManager::GetConfigManager().GetConfig("LoggerConfig"_intern);

        if (!rootNode) {
            // Fallback: create default sinks if config not found
            auto consoleSink = BECore::New<ConsoleSink>();
            consoleSink->SetPriority(0);
            _sinks.push_back(consoleSink);
        } else {
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

        // Sort sinks by priority (lower first)
        SortSinksByPriority();

        // Initialize all sinks
        for (auto& sink : _sinks) {
            sink->Initialize();
        }

        _initialized = true;
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
