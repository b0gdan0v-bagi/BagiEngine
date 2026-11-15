#pragma once

#include <utility>

namespace BECore::Tests {

    /// A move-only capture that counts its own end: a moved-from one counts nothing, so the tally of a callable that holds one is 1 once the
    /// callable is gone, never 0 (leaked) or 2 (destroyed twice)
    class DestroyTally {
    public:
        explicit DestroyTally(int& tally) : _tally(&tally) {}
        DestroyTally(DestroyTally&& other) noexcept : _tally(std::exchange(other._tally, nullptr)) {}
        DestroyTally& operator=(DestroyTally&& other) noexcept {
            if (this != &other) {
                End();
                _tally = std::exchange(other._tally, nullptr);
            }
            return *this;
        }
        DestroyTally(const DestroyTally&) = delete;
        DestroyTally& operator=(const DestroyTally&) = delete;
        ~DestroyTally() {
            End();
        }

    private:
        void End() {
            if (_tally != nullptr) {
                ++*_tally;
            }
        }

        int* _tally;
    };

}  // namespace BECore::Tests
