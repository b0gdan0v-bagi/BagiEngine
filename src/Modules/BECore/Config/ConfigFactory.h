#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Types/Types.h>
#include <utility>

namespace BECore {

    /**
     * @brief Create a RefCounted object and deserialize it from a named XML config
     *
     * An unknown config gives the object with default field values. Errors the deserializer reports (an unknown factory
     * `type`, a malformed value) and its findings (an unknown key, a missing `required` field, a value outside `range`:
     * ConfigManager::GetDiagnostics) go to the log through LogDeserializeErrors(); the object keeps what could be read. A
     * message logged before LoggerManager has its sinks waits in the log ring buffer, so this also works for LoggerConfig.
     *
     * @note Include from .cpp files only: it pulls in CoreManager.h.
     *
     * @tparam T Must have BE_CLASS and inherit from RefCounted or RefCountedAtomic
     * @param configName Config name without extension
     * @param args Forwarded to the constructor of T
     */
    template <typename T, typename... Args>
    auto MakeFromConfig(StringView configName, Args&&... args) {
        auto obj = BECore::New<T>(std::forward<Args>(args)...);
        if (auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(configName)) {
            obj->Deserialize(*deserializer);
            deserializer->FinishLoad();
            LogDeserializeErrors(configName, *deserializer);
        }
        return obj;
    }

}  // namespace BECore
