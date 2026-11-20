#pragma once

#include <utility>

namespace BECore {

    /**
     * @brief A T that is constructed in place and never destroyed, for a `constinit` static whose users may outlive the static destructors.
     *
     * A static destructor may free a pooled object or take a lock after the T would have been destroyed; the union keeps the T's destructor from running.
     */
    template <typename T>
    union Immortal {
        T value;

        template <typename... TArgs>
        constexpr explicit Immortal(TArgs&&... args) : value(std::forward<TArgs>(args)...) {}
        ~Immortal() {}  // NOLINT: the union must not destroy the value
    };

}  // namespace BECore
