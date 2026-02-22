#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

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
        }
        return obj;
    }

}  // namespace BECore
