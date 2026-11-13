#pragma once

#include <Stress/StressSettings.h>

namespace BECore::Stress {

    /**
     * @brief What the app shows now: the scene, the object count and the language. Plain data, no scene, no renderer.
     *
     * The components and the control adapter read it and rebuild what they show when Version() moved; a change to an equal
     * value changes nothing, so a controller that repeats itself does not make the scene rebuild.
     */
    class StressState {
    public:
        StressState() = default;
        explicit StressState(const StressSettings& settings) : _scene(settings.scene), _count(settings.count), _language(settings.language) {}

        StressScene GetScene() const {
            return _scene;
        }

        /// 0..kMaxCount (the command line asks for at least 1, but a scene shows nothing for 0)
        uint32_t GetCount() const {
            return _count;
        }

        StressLanguage GetLanguage() const {
            return _language;
        }

        /// Grows with every change and never goes back; starts at 1, so a component's own 0 means "built nothing yet"
        uint64_t Version() const {
            return _version;
        }

        void SetScene(StressScene scene);
        /// A count above kMaxCount is kMaxCount
        void SetCount(uint32_t count);
        void SetLanguage(StressLanguage language);

    private:
        StressScene _scene = StressScene::Sprites;
        uint32_t _count = kDefaultCount;
        StressLanguage _language = StressLanguage::En;
        uint64_t _version = 1;
    };

}  // namespace BECore::Stress
