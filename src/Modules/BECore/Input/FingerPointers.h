#pragma once

#include <BECore/Input/PointerId.h>
#include <BECore/Types/Types.h>
#include <SDL3/SDL_touch.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The fingers that are down and the pointer id (the `which`) each one has.
     *
     * A finger is a pointer of its own for the whole touch: kFingerPointerBase plus the lowest free place, so two fingers hold two
     * presses and two hovers in UIInput, and a place is reused once its finger is up. Only kMaxFingerPointers fingers at a time; a
     * further one is refused and gets no events. It holds its places inside the object and never allocates.
     */
    class FingerPointers {
    public:
        /// A finger that is down. A finger already known keeps its pointer id (SDL cancels a repeated down first, so this is a safeguard)
        /// @return its `which`, or nothing when every place is taken
        Optional<uint32_t> Begin(SDL_FingerID finger) {
            if (const Optional<uint32_t> known = Find(finger)) {
                return known;
            }
            for (uint32_t place = 0; place < kMaxFingerPointers; ++place) {
                if (!IsTaken(place)) {
                    _fingers.push_back(Entry{finger, place});
                    return kFingerPointerBase + place;
                }
            }
            return std::nullopt;
        }

        /// @return the `which` of a finger that is down, nothing for an unknown one
        [[nodiscard]] Optional<uint32_t> Find(SDL_FingerID finger) const {
            for (const Entry& entry : _fingers) {
                if (entry.finger == finger) {
                    return kFingerPointerBase + entry.place;
                }
            }
            return std::nullopt;
        }

        /// The finger is up or cancelled; its place is free again
        /// @return the `which` it had, nothing for an unknown finger
        Optional<uint32_t> End(SDL_FingerID finger) {
            for (auto it = _fingers.begin(); it != _fingers.end(); ++it) {
                if (it->finger == finger) {
                    const uint32_t which = kFingerPointerBase + it->place;
                    _fingers.erase(it);
                    return which;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] size_t Count() const {
            return _fingers.size();
        }

    private:
        struct Entry {
            SDL_FingerID finger = 0;
            uint32_t place = 0;
        };

        [[nodiscard]] bool IsTaken(uint32_t place) const {
            for (const Entry& entry : _fingers) {
                if (entry.place == place) {
                    return true;
                }
            }
            return false;
        }

        FixedVector<Entry, kMaxFingerPointers> _fingers;
    };

}  // namespace BECore
