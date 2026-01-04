#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <type_traits>

namespace Core {

    namespace Impl {

        namespace ranges = std::ranges;
        using std::array;
        using std::string_view;

        template <class Ty>
        inline constexpr bool hasStrings = false;

        template <class Ty>
        inline constexpr Ty Tokens = Ty{};

        template <auto EnumVal>
        static consteval auto enumName() {
#ifdef _MSC_VER
            // MSVC: auto __cdecl Impl::enumName<align::CM>(void)
            constexpr string_view sv{__FUNCSIG__};  // NOLINT(clang-diagnostic-language-extension-token)
            constexpr size_t last = sv.find_last_of(">");
#else
            // clang: auto Impl::name() [E = align::CM]
            // gcc: consteval auto Impl::name() [with auto E = align::CM]
            constexpr string_view sv{__PRETTY_FUNCTION__};
            constexpr auto last = sv.find_last_of("]");
#endif
            constexpr size_t first = sv.find_last_of(':', last) + 1;
            array<char, last - first + 1> buf{};  // +1 '\0' terminated c_str
            ranges::copy(string_view{sv.data() + first, last - first}, buf.begin());
            return buf;
        }

        template <auto EnumVal>
        inline constexpr auto ENameArr{enumName<EnumVal>()};
        template <auto EnumVal>
        inline constexpr string_view EnumName{ENameArr<EnumVal>.data(), ENameArr<EnumVal>.size() - 1};

        template <typename Enum, auto... Enums>
        class Tokenizer {
            struct Data {
                string_view name;
                Enum value;
            };
            static constexpr array tokens{Data{EnumName<Enums>, Enums}...};
            using Ty = std::underlying_type_t<Enum>;
            static constexpr Enum errorValue{static_cast<Enum>(std::numeric_limits<Ty>::max())};

        public:
            static constexpr auto toString(Enum e) noexcept {
                auto it = ranges::find(tokens, e, &Data::value);
                return it == tokens.end() ? string_view{""} : it->name;
            }
            static constexpr Enum toEnum(string_view str) noexcept {
                auto it = ranges::find(tokens, str, &Data::name);
                return it == tokens.end() ? errorValue : it->value;
            }
        };

    }  // namespace Impl

#define CORE_ENUM(Enum, macType, ...)                                                                                                                                                                  \
    enum class Enum : macType { __VA_ARGS__ };                                                                                                                                                         \
    inline auto operator+(Enum e) noexcept {                                                                                                                                                           \
        return std::underlying_type_t<Enum>(e);                                                                                                                                                        \
    }                                                                                                                                                                                                  \
    namespace Impl {                                                                                                                                                                                   \
        template <>                                                                                                                                                                                    \
        inline constexpr auto hasStrings<Enum> = true;                                                                                                                                                 \
        template <>                                                                                                                                                                                    \
        inline constexpr auto Tokens<Enum> = [] {                                                                                                                                                      \
            using enum Enum;                                                                                                                                                                           \
            return Tokenizer<Enum, __VA_ARGS__>{};                                                                                                                                                     \
        }();                                                                                                                                                                                           \
    }

    template <typename E, typename Enum = std::remove_cvref_t<E>>
    requires Impl::hasStrings<Enum> inline constexpr auto EnumToString(E e) {
        return Impl::Tokens<Enum>.toString(e);
    }

    template <typename E, typename Enum = std::remove_cvref_t<E>>
    requires Impl::hasStrings<Enum> inline constexpr E StringToEnum(std::string_view str) {
        return Impl::Tokens<Enum>.toEnum(str);
    }

    template <typename E, typename Enum = std::remove_cvref_t<E>>
    requires Impl::hasStrings<Enum> inline constexpr std::optional<Enum> EnumCast(std::string_view str) {
        auto result = StringToEnum<Enum>(str);
        using Ty = std::underlying_type_t<Enum>;
        constexpr Enum errorValue{static_cast<Enum>(std::numeric_limits<Ty>::max())};
        if (result == errorValue) {
            return std::nullopt;
        }
        return result;
    }

}  // namespace Core
