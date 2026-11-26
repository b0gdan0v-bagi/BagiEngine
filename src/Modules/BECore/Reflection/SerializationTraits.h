#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <EASTL/array.h>

namespace BECore {

    namespace Detail {

        /// Puts @p text into any string type and returns how many bytes did not fit: a FixedString that cannot overflow keeps the
        /// part that fits (a longer text from a file is no assertion), any other string takes it all and the result is 0
        template <typename Text>
        size_t AssignText(Text& value, StringView text) {
            size_t dropped = 0;
            if constexpr (requires { Text::can_overflow(); }) {
                const size_t capacity = static_cast<size_t>(value.max_size());
                if (!Text::can_overflow() && text.size() > capacity) {
                    dropped = text.size() - capacity;
                    text = text.substr(0, capacity);
                }
            }
            value.assign(text.data(), text.size());
            return dropped;
        }

    }  // namespace Detail

    // =========================================================================
    // DefaultMaker<T> -- resets a value to its default state
    // =========================================================================

    template <typename T, typename = void>
    struct DefaultMaker {
        static constexpr bool HasMakeDefault = false;
        static void MakeDefault(T&) {}
    };

    template <typename T>
    struct DefaultMaker<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(T& v) {
            v = T{};
        }
    };

    template <typename T>
    struct DefaultMaker<T, std::enable_if_t<std::is_enum_v<T>>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(T& v) {
            v = T{};
        }
    };

    // Containers with .clear() (eastl::string, eastl::vector, eastl::unordered_map, etc.)
    template <typename T>
        struct DefaultMaker < T,
        std::enable_if_t < !std::is_arithmetic_v<T> && !std::is_enum_v<T> && requires(T & t) {
        t.clear();
    } >> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(T & v) {
            v.clear();
        }
    };

    template <>
    struct DefaultMaker<PoolString> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(PoolString& v) {
            v = PoolString{};
        }
    };

    template <typename T>
    struct DefaultMaker<IntrusivePtr<T>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(IntrusivePtr<T>& v) {
            v.Reset();
        }
    };

    template <typename T>
    struct DefaultMaker<std::optional<T>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(std::optional<T>& v) {
            v = std::nullopt;
        }
    };

    template <typename T, size_t N>
    struct DefaultMaker<eastl::array<T, N>> {
        static constexpr bool HasMakeDefault = DefaultMaker<T>::HasMakeDefault;
        static void MakeDefault(eastl::array<T, N>& arr) {
            for (auto& v : arr)
                DefaultMaker<T>::MakeDefault(v);
        }
    };

}  // namespace BECore
