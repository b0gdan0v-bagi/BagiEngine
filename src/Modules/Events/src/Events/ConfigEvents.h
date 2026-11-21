#pragma once

#include <BECore/PoolString/PoolString.h>
#include <Events/EventBase.h>

namespace BECore::ConfigEvents {

    /// The cached document of a config changed: the file was edited on disk, or SaveConfig / ReloadConfig / NotifyConfigChanged ran. An XmlNode taken before is stale.
    /// Emitted on the main thread, never while ConfigManager holds a lock
    struct ConfigChangedEvent : public EventBase<ConfigChangedEvent> {
        PoolString name;  ///< the config's name (file stem)

        explicit ConfigChangedEvent(PoolString value) : name(value) {}
    };

}  // namespace BECore::ConfigEvents
