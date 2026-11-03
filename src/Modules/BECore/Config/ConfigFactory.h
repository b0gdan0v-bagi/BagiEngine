#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>

namespace BECore {

    /**
     * @brief Create a RefCounted object and immediately deserialize it from a named XML config.
     *
     * Replaces the pattern:
     * @code
     *   auto obj = BECore::New<T>();
     *   if (auto d = CoreManager::GetConfigManager().CreateDeserializer("MyConfig")) { obj->Deserialize(*d); }
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
     * LogDeserializeErrors() (declared in OpenDeserializer.h, included here); the object keeps what could be read. A message logged before LoggerManager has its
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
        if (auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(configName)) {
            obj->Deserialize(*deserializer);
            if (deserializer->HasErrors()) {
                LogDeserializeErrors(configName, *deserializer);
            }
        }
        return obj;
    }

}  // namespace BECore
