#include "LoggerManager.h"

#include <BECore/Config/XmlConfig.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/RefCounted/New.h>

#include <EASTL/sort.h>

namespace BECore {

    void LoggerManager::Initialize() {
        if (_initialized) {
            return;
        }

        XmlConfig config = XmlConfig::Create();
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

                        // Set priority from config (default: 0)
                        auto priority = sinkNode.ParseAttribute<int>("priority");
                        if (priority.has_value()) {
                            sink->SetPriority(*priority);
                        }

                        // Set min level from config (default: Debug)
                        auto minLevel = sinkNode.ParseAttribute<LogLevel>("minLevel");
                        if (minLevel.has_value()) {
                            sink->SetMinLevel(*minLevel);
                        }

                        // Configure type-specific options
                        if (*sinkType == LogSinkType::Console) {
                            if (auto* consoleSink = dynamic_cast<ConsoleSink*>(sink.Get())) {
                                auto colorEnabled = sinkNode.ParseAttribute<bool>("colorEnabled");
                                if (colorEnabled.has_value()) {
                                    consoleSink->SetColorEnabled(*colorEnabled);
                                }
                            }
                        } else if (*sinkType == LogSinkType::File) {
                            if (auto* fileSink = dynamic_cast<FileSink*>(sink.Get())) {
                                auto filename = sinkNode.ParseAttribute<std::string_view>("filename");
                                if (filename.has_value()) {
                                    fileSink->SetFilename(std::string(*filename));
                                }
                                auto append = sinkNode.ParseAttribute<bool>("append");
                                if (append.has_value()) {
                                    fileSink->SetAppend(*append);
                                }
                            }
                        }

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

        _initialized = true;
    }

    void LoggerManager::Log(LogLevel level, const char* message, const char* file, int line) {
        for (auto& sink : _sinks) {
            sink->Write(level, message, file, line);
        }

        // Auto-flush on fatal errors
        if (level == LogLevel::Fatal) {
            Flush();
        }
    }

    void LoggerManager::Flush() {
        for (auto& sink : _sinks) {
            sink->Flush();
        }
    }

    IntrusivePtr<ILogSink> LoggerManager::CreateSinkByType(LogSinkType type) {
        switch (type) {
            case LogSinkType::Console:
                return BECore::New<ConsoleSink>();
            case LogSinkType::File:
                return BECore::New<FileSink>();
            case LogSinkType::Output:
                return BECore::New<OutputSink>();
            default:
                return {};
        }
    }

    void LoggerManager::SortSinksByPriority() {
        eastl::sort(_sinks.begin(), _sinks.end(),
            [](const IntrusivePtr<ILogSink>& a, const IntrusivePtr<ILogSink>& b) {
                return a->GetPriority() < b->GetPriority();
            });
    }

}  // namespace BECore
