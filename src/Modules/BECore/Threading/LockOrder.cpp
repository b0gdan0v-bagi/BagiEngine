#include "LockOrder.h"

#if BE_LOCK_ORDER_CHECK

#include <cstdio>

namespace BECore::LockOrder {

    namespace {

        constexpr size_t kMessageSize = 192;

        struct HeldStack {
            const LockSite* sites[kMaxHeld];
            size_t count;
        };

        constinit thread_local HeldStack t_held{};

    }  // namespace

    void BeforeAcquire(const LockSite& site) {
        if (site.level == 0) {
            return;
        }
        const LockSite* highest = nullptr;
        for (size_t i = 0; i < t_held.count; ++i) {
            if (highest == nullptr || t_held.sites[i]->level > highest->level) {
                highest = t_held.sites[i];
            }
        }
        const bool inOrder = highest == nullptr || site.level > highest->level;
        if (!inOrder) {
            char message[kMessageSize];
            std::snprintf(message, sizeof(message), "Lock order: taking '%s' (level %u) while holding '%s' (level %u)", site.name, site.level, highest->name, highest->level);
            ASSERT(inOrder, message);
        }
    }

    void Acquired(const LockSite& site) {
        if (site.level == 0) {
            return;
        }
        const bool fits = t_held.count < kMaxHeld;
        ASSERT(fits, "Lock order: a thread holds more ordered locks than LockOrder::kMaxHeld");
        if (fits) {
            t_held.sites[t_held.count++] = &site;
        }
    }

    void Released(const LockSite& site) noexcept {
        if (site.level == 0) {
            return;
        }
        for (size_t i = t_held.count; i > 0; --i) {
            if (t_held.sites[i - 1] == &site) {
                for (size_t j = i; j < t_held.count; ++j) {
                    t_held.sites[j - 1] = t_held.sites[j];
                }
                --t_held.count;
                return;
            }
        }
    }

    size_t HeldCount() noexcept {
        return t_held.count;
    }

}  // namespace BECore::LockOrder

#endif
