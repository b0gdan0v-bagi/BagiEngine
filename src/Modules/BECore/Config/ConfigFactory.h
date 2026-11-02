#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    /**
     * @brief Write every error the deserializer collected to the log, one LOG_ERROR per error.
     *
     * Form: `Config 'LoggerConfig': sinks[0].type: Unknown factory type: 'Console'`. The path is left out when the
     * error has none (a document that does not parse), and ` (line N)` is appended when it has a line.
     *
     * @param configName    Name of the config the deserializer read, for the message.
     * @param deserializer  Deserializer that finished (or failed) reading the config.
     */
    void LogDeserializeErrors(eastl::string_view configName, const IDeserializer& deserializer);

    /**
     * @brief Create a RefCounted object and immediately deserialize it from a named XML config.
     *
     * Replaces the pattern:
     * @code
     *   auto obj = BECore::New<T>();
     *   auto root = CoreManager::GetConfigManager().GetConfig("MyConfig");
     *   if (root) { XmlDeserializer d; d.LoadFromXmlNode(root); obj->Deserialize(d); }
     * @endcode
     *
     * With:
     * @code
     *   auto obj = BECore::MakeFromConfig<T>("MyConfig");
     *   obj->Initialize(); // hand-written logic only
     * @endcode
     *
     * If the config is not found, the object is returned with default field values.
     * Errors the deserializer reports (an unknown factory `type`, a malformed value) are written to the log with
     * LogDeserializeErrors(); the object keeps what could be read. A message logged before LoggerManager has its
     * sinks stays in the log ring buffer and reaches them later, so this also works for LoggerConfig itself.
     *
     * @note Include this header from .cpp files only — it pulls in CoreManager.h.
     *
     * @tparam T  Type to create; must have BE_CLASS and inherit from RefCounted or RefCountedAtomic.
     * @param configName  Name of the XML config file (without extension).
     * @param args        Optional constructor arguments forwarded to T.
     * @return IntrusivePtrAtomic<T> or IntrusivePtrNonAtomic<T> (deduced from T's base).
     */
    template <typename T, typename... Args>
    auto MakeFromConfig(eastl::string_view configName, Args&&... args) {
        auto obj = BECore::New<T>(std::forward<Args>(args)...);
        auto root = CoreManager::GetConfigManager().GetConfig(configName);
        if (root) {
            XmlDeserializer d;
            d.LoadFromXmlNode(root);
            obj->Deserialize(d);
            if (d.HasErrors()) {
                LogDeserializeErrors(configName, d);
            }
        }
        return obj;
    }

}  // namespace BECore
