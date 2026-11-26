#pragma once

#include <concepts>

namespace BECore {
    /**
     * Limits a method or constructor to T and its descendants: it takes a `PassKey<T>` (`void Open(PassKey<Manager>)`), T passes `{}`
     * (`obj.Open({})`), a class derived from T passes its own key (`obj.Open(PassKey<Derived>{})`), which converts, and any other
     * caller does not compile. The key moves and is never copied.
     */
    template <typename T>
    class PassKey final {
    private:
        // T is the only friend and the class is final: with any other way to the constructor a stranger could make the key.
        friend T;

        constexpr PassKey() noexcept = default;

        PassKey(const PassKey&) = delete;
        PassKey& operator=(const PassKey&) = delete;
        PassKey& operator=(PassKey&&) = delete;

    public:
        constexpr PassKey(PassKey&&) noexcept = default;

        // A PassKey<Derived> passes where PassKey<Base> is asked.
        template <typename U>
        requires(std::derived_from<U, T> || std::same_as<U, T>) constexpr PassKey(PassKey<U>&&) noexcept {}
    };
}  // namespace BECore
