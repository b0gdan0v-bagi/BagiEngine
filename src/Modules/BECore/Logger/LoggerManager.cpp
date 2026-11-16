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

        // The deserializer has called Initialize() of each sink in config order already. Publish calls the last connected listener first, so
        // connecting again from the highest priority number to the lowest makes the lowest receive a line first.
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
