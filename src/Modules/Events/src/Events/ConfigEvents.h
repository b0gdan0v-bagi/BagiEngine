#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <Events/EventBase.h>

namespace BECore::ConfigEvents {

    /// The cached document of a config changed: the file was edited on disk, or SaveConfig / ReloadConfig / NotifyConfigChanged ran. An XmlNode taken before is stale.
    /// Emitted on the main thread, never while ConfigManager holds a lock
    struct ConfigChangedEvent : public EventBase<ConfigChangedEvent> {
        PoolString name;  ///< the config's name (file stem)

        explicit ConfigChangedEvent(PoolString value) : name(value) {}
    };

    /// The file of a loaded config changed on disk and could not be read again (it does not parse, is gone or unreadable): the old document is kept and no
    /// ConfigChangedEvent follows. Emitted on the main thread, never while ConfigManager holds a lock. A ReloadConfig call by hand does not emit it: it returns false
    struct ConfigReloadFailedEvent : public EventBase<ConfigReloadFailedEvent> {
        PoolString name;  ///< the config's name (file stem)
        String reason;    ///< one short line for a panel's error banner; a String so the pool does not grow with it

        ConfigReloadFailedEvent(PoolString value, String text) : name(value), reason(std::move(text)) {}
    };

}  // namespace BECore::ConfigEvents
