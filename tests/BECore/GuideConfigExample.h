#pragma once

#include <BECore/Config/ConfigManager.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/RefCounted/New.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <cstdint>

namespace BECore::Tests {

    /*
     * The page "config and reflection" of the guide for people (`docs/guide/config-and-reflection.md`) excerpts the code below and the file
     * `docs/guide/examples/config/config/GuideSpawnerConfig.xml` that it reads. It is here and not in the page so that it is compiled and run: a
     * change of how a config is read or followed fails `GuideConfigExampleTest` before it misleads a reader. The class names are used by this
     * example only, because the reflection registry refuses two classes of one name.
     */
    namespace GuideConfigExample {

        struct GuideSpawnerNames {
            static constexpr auto config = "GuideSpawnerConfig"_intern;
        };

        CORE_ENUM(SpawnShape, uint8_t, Ring, Line, Scatter)

        /// The settings of a spawner: the root attributes of `config/GuideSpawnerConfig.xml`, the file's name without `.xml` is the config's name
        struct GuideSpawnerConfig : RefCounted {
            BE_CLASS(GuideSpawnerConfig)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0.1, 30), step(0.1), tooltip("Seconds between two spawns")) float _interval = 1.0f;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 100), tooltip("How many pieces one spawn makes")) int32_t _count = 5;
            BE_REFLECT_FIELD bool _enabled = true;
            BE_REFLECT_FIELD SpawnShape _shape = SpawnShape::Ring;
            BE_REFLECT_FIELD String _label;
        };

        /// Reads the config by its name from @p configs. This is what `MakeFromConfig<GuideSpawnerConfig>(name)` does over the engine's own
        /// manager; the manager is a parameter so that a test can read a folder of its own. The findings (a typo, a value out of `range`) go to the log
        inline IntrusivePtr<GuideSpawnerConfig> ReadGuideSpawnerConfig(const ConfigManager& configs) {
            auto config = New<GuideSpawnerConfig>();
            if (auto deserializer = configs.CreateDeserializer(GuideSpawnerNames::config)) {
                config->Deserialize(*deserializer);
                deserializer->FinishLoad();
                LogDeserializeErrors(GuideSpawnerNames::config.View(), *deserializer);
            }
            return config;
        }

        /// Keeps the settings of a spawner and reads the file again whenever its config changed (a save, a reload, a change on disk). The
        /// event comes on the main thread after the manager released its lock, so reading again here is allowed
        class GuideSpawnerSettings : public SubscriptionHolder {
        public:
            explicit GuideSpawnerSettings(const ConfigManager& configs) : _configs(configs), _config(ReadGuideSpawnerConfig(configs)) {
                Subscribe<ConfigEvents::ConfigChangedEvent, &GuideSpawnerSettings::OnConfigChanged>(this);
            }

            [[nodiscard]] const GuideSpawnerConfig& Get() const {
                return *_config;
            }

        private:
            void OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event) {
                if (event.name == GuideSpawnerNames::config) {
                    _config = ReadGuideSpawnerConfig(_configs);  // a node or a deserializer taken before this event is stale
                }
            }

            const ConfigManager& _configs;
            IntrusivePtr<GuideSpawnerConfig> _config;
        };

    }  // namespace GuideConfigExample

}  // namespace BECore::Tests
