#pragma once

#include <BECore/Logger/Logger.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Save/SaveGame.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::Tests {

    /*
     * The page "a save" of the guide for people (`docs/guide/save-game.md`) excerpts the code below. It is here and not in the page so that it is
     * compiled and run: a change of how a save is written, read or refused fails `GuideSaveExampleTest` before it misleads a reader. The engine
     * ships no game with a save, so the example lives with the tests. The class names are used by this example only, because the reflection
     * registry refuses two classes of one name.
     */
    namespace GuideSaveExample {

        /// What the game keeps about the player. Only the `BE_REFLECT_FIELD` members are saved
        struct GuideProfileState {
            BE_CLASS(GuideProfileState)

            BE_REFLECT_FIELD int32_t _level = 1;
            BE_REFLECT_FIELD String _name;
            BE_REFLECT_FIELD Vector<int32_t> _unlocked;
            BE_REFLECT_FIELD int32_t _coins = 10;
        };

        /// The same state as version 1 of the game wrote it: no coins, and a level counted from 0. The game no longer has this type; it is here
        /// so that a test can write the file of an older game
        struct GuideProfileStateV1 {
            BE_CLASS(GuideProfileStateV1)

            BE_REFLECT_FIELD int32_t _level = 0;
            BE_REFLECT_FIELD String _name;
            BE_REFLECT_FIELD Vector<int32_t> _unlocked;
        };

        /// Version 2 counts levels from 1; a file of version 1 counts from 0
        constexpr uint32_t kLevelsFromOneVersion = 2;

        /// The game writes version 2 and still reads version 1
        constexpr SaveSchema kProfileSchema{"GuideGame", kLevelsFromOneVersion, 1};

        CORE_ENUM(LoadOutcome, uint8_t, Loaded, NewGame, Refused)

        /// The player's profile and its slot in a save folder
        class GuideProfile {
        public:
            explicit GuideProfile(SaveGameService& saves) : _saves(saves) {}

            [[nodiscard]] GuideProfileState& State() {
                return _state;
            }

            [[nodiscard]] const GuideProfileState& State() const {
                return _state;
            }

            [[nodiscard]] bool Save(StringView slot) {
                const auto saved = _saves.Save(slot, kProfileSchema, _state);
                if (!saved) {
                    LOG_ERROR("Cannot save '{}': {}", slot, saved.error().detail);
                }
                return saved.has_value();
            }

            /// Loads into a temporary and replaces the live state only after a success: a refused file changes nothing and is logged once. A slot
            /// without a file is not an error, it is a new game
            LoadOutcome Load(StringView slot) {
                GuideProfileState loaded;
                const auto info = _saves.Load(slot, kProfileSchema, loaded);
                if (!info) {
                    if (info.error().kind == SaveErrorKind::NotFound) {
                        return LoadOutcome::NewGame;
                    }
                    LOG_ERROR("Cannot load '{}': {}", slot, info.error().detail);
                    return LoadOutcome::Refused;
                }
                if (info->version < kLevelsFromOneVersion) {
                    ++loaded._level;
                }
                _state = std::move(loaded);
                return LoadOutcome::Loaded;
            }

        private:
            SaveGameService& _saves;
            GuideProfileState _state;
        };

    }  // namespace GuideSaveExample

}  // namespace BECore::Tests
