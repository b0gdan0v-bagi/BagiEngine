#include "LoggerManager.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <EASTL/sort.h>
#include <Generated/LoggerManager.gen.hpp>

namespace BECore {

    void LoggerManager::Initialize() {
        if (_initialized) {
            return;
        }

        if (_sinks.empty()) {
            LOG_WARNING("LoggerConfig yields no sinks, falling back to a ConsoleSink");
            _sinks.push_back(BECore::New<ConsoleSink>());
        }

        SortSinksByPriority();

        // Publish calls the last connected listener first, and a disconnect moves the last entry into the freed slot, so the order the deserializer left
        // depends on the config. Disconnect every sink first, then connect from the highest priority number to the lowest: the lowest gets a line first.
        for (const auto& sink : _sinks) {
            sink->Disconnect();
        }
        for (size_t pos = _sinks.size(); pos > 0; --pos) {
            _sinks[pos - 1]->Initialize();
        }

        _initialized = true;
    }

    void LoggerManager::SortSinksByPriority() {
        // Insertion sort is stable (sinks of one priority keep the order of the config) and allocates nothing
        eastl::insertion_sort(_sinks.begin(), _sinks.end(), [](auto&& a, auto&& b) { return a->GetPriority() < b->GetPriority(); });
    }

}  // namespace BECore
