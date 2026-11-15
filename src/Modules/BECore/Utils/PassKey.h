#pragma once

#include <concepts>

namespace BECore {
    /**
     * Limits a method or constructor to T and its descendants: it takes a `PassKey<T>` (`void Open(PassKey<Manager>)`), an allowed caller
     * passes `{}` (`obj.Open({})`), any other caller does not compile. The key moves and is never copied.
     */
    template <typename T>
    class PassKey {
    protected:
        // Protected, so that only T (a friend) and its descendants can construct a key.
        constexpr PassKey() noexcept = default;

    private:
        friend T;

        template <typename U>
        friend class PassKey;

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
