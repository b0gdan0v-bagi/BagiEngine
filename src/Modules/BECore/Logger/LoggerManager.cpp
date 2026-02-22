#include "LoggerManager.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <EASTL/sort.h>
#include <Generated/EnumLogSink.gen.hpp>
#include <Generated/ILogSink.gen.hpp>
#include <Generated/LoggerManager.gen.hpp>

namespace BECore {

    void LoggerManager::Initialize() {
        if (_initialized) {
            return;
        }

        auto root = CoreManager::GetConfigManager().GetConfig("LoggerConfig");
        if (root) {
            XmlDeserializer d;
            d.LoadFromXmlNode(root);
            Deserialize(d);
        }

        if (_sinks.empty()) {
            // Fallback: create a default console sink if config not found or empty
            _sinks.push_back(BECore::New<ConsoleSink>());
        }

        SortSinksByPriority();

        for (auto& sink : _sinks) {
            sink->Initialize();
        }

        _initialized = true;
    }

    void LoggerManager::SortSinksByPriority() {
        eastl::sort(_sinks.begin(), _sinks.end(), [](auto&& a, auto&& b) { return a->GetPriority() < b->GetPriority(); });
    }

}  // namespace BECore
