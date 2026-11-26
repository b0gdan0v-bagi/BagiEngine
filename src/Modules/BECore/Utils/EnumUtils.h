#pragma once

#include <BECore/Format/Formatter.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <EASTL/array.h>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <mutex>
#include <type_traits>

namespace BECore {

    namespace Impl {

        namespace ranges = std::ranges;

        template <auto EnumVal>
        static consteval auto enumName() {
#if defined(__clang__)
            // Clang (including ClangCL on Windows): auto Impl::enumName() [EnumVal = align::CM]
            constexpr std::string_view sv{__PRETTY_FUNCTION__};  //-NoStd: eastl::string_view::find_last_of is not constexpr
            constexpr auto last = sv.find_last_of("]");
#elif defined(_MSC_VER)
            // MSVC: auto __cdecl Impl::enumName<align::CM>(void)
            constexpr std::string_view sv{__FUNCSIG__};  //-NoStd: as above  // NOLINT(clang-diagnostic-language-extension-token)
            constexpr size_t last = sv.find_last_of(">");
#else
            // GCC: consteval auto Impl::enumName() [with auto EnumVal = align::CM]
            constexpr std::string_view sv{__PRETTY_FUNCTION__};  //-NoStd: eastl::string_view::find_last_of is not constexpr
            constexpr auto last = sv.find_last_of("]");
#endif
            constexpr size_t first = sv.find_last_of(':', last) + 1;
            Array<char, last - first + 1> buf{};                                           // +1 '\0' terminated c_str
            ranges::copy(std::string_view{sv.data() + first, last - first}, buf.begin());  //-NoStd: as above
            return buf;
        }

        template <auto EnumVal>
        inline constexpr auto ENameArr{enumName<EnumVal>()};

        template <auto EnumVal>
        inline constexpr StringView EnumName{ENameArr<EnumVal>.data(), ENameArr<EnumVal>.size() - 1};

        template <typename Enum, auto... Enums>
        class Tokenizer {
            static constexpr size_t Count = sizeof...(Enums);

            struct Data {
                StringView name;
                Enum value;
            };

            static constexpr Array<Data, Count> tokens{Data{EnumName<Enums>, Enums}...};
            static constexpr Array<StringView, Count> names{EnumName<Enums>...};
            static constexpr Array<Enum, Count> values{Enums...};

            using Ty = std::underlying_type_t<Enum>;
            static constexpr Enum errorValue{static_cast<Enum>(std::numeric_limits<Ty>::max())};

            struct RuntimeCache {
                Array<PoolString, Count> poolStrings{};
                std::once_flag initFlag{};

                void Initialize() {
                    for (size_t i = 0; i < Count; ++i) {
                        poolStrings[i] = PoolString::Intern(names[i]);
                    }
                }
            };

            static RuntimeCache& GetCache() {
                static RuntimeCache cache;
                std::call_once(cache.initFlag, [&] { cache.Initialize(); });
                return cache;
            }

        public:
            static constexpr size_t EnumCount = Count;

            static constexpr StringView ToString(Enum e) noexcept {
                auto it = ranges::find(tokens, e, &Data::value);
                return it == tokens.end() ? StringView{} : it->name;
            }

            static PoolString ToPoolString(Enum e) noexcept {
                auto& cache = GetCache();
                for (size_t i = 0; i < Count; ++i) {
                    if (values[i] == e) {
                        return cache.poolStrings[i];
                    }
                }
                return PoolString{};
            }

            static constexpr Enum ToEnum(StringView str) noexcept {
                auto it = ranges::find(tokens, str, &Data::name);
                return it == tokens.end() ? errorValue : it->value;
            }

            static constexpr Enum ErrorValue() noexcept {
                return errorValue;
            }

            static Enum FromPoolString(PoolString ps) noexcept {
                auto& cache = GetCache();
                for (size_t i = 0; i < Count; ++i) {
                    if (cache.poolStrings[i] == ps) {
                        return values[i];
                    }
                }
                return errorValue;
            }

            static const Array<PoolString, Count>& GetPoolStrings() noexcept {
                return GetCache().poolStrings;
            }

            static constexpr const Array<StringView, Count>& GetNames() noexcept {
                return names;
            }

            static constexpr const Array<Enum, Count>& GetValues() noexcept {
                return values;
            }
        };

    }  // namespace Impl

    /// Conversion between the values of a CORE_ENUM enum, their names and PoolStrings.
    template <typename Enum>
    class EnumUtils {
        static constexpr const auto& Tok() {
            return *EnumTokenizerPtr(Enum{});  // ADL finds the friend function
        }

    public:
        static constexpr size_t Count() noexcept {
            return Tok().EnumCount;
        }

        static constexpr StringView ToString(Enum e) noexcept {
            return Tok().ToString(e);
        }

        /// The interned name, cached on first use.
        static PoolString ToPoolString(Enum e) noexcept {
            return Tok().ToPoolString(e);
        }

        /// The value of a name; an unknown name gives the largest value of the underlying type (Cast tells it from a real value).
        static constexpr Enum FromString(StringView str) noexcept {
            return Tok().ToEnum(str);
        }

        static constexpr Enum FromString(const char* str) noexcept {
            return Tok().ToEnum(StringView{str});
        }

        static Enum FromPoolString(PoolString ps) noexcept {
            return Tok().FromPoolString(ps);
        }

        /// Like FromString, with nullopt for an unknown name.
        static constexpr Optional<Enum> Cast(StringView str) noexcept {
            auto result = FromString(str);
            if (result == Tok().ErrorValue()) {
                return std::nullopt;
            }
            return result;
        }

        static constexpr Optional<Enum> Cast(const char* str) noexcept {
            return Cast(StringView{str});
        }

        static constexpr const auto& Names() noexcept {
            return Tok().GetNames();
        }

        static constexpr const auto& Values() noexcept {
            return Tok().GetValues();
        }

        static const auto& PoolStrings() noexcept {
            return Tok().GetPoolStrings();
        }
    };

    /// T is an enum defined with CORE_ENUM (an ADL-found EnumTokenizerPtr exists).
    template <typename T>
    concept IsReflectedEnum = std::is_enum_v<T> && requires {
        {EnumTokenizerPtr(T{})};
    };

    /// Formats a CORE_ENUM value as its name; a string spec works (Format/CLAUDE.md). Other enums are left to fmt.
    template <IsReflectedEnum Enum>
    struct Formatter<Enum> : TextFormatter {
        FormatContext::iterator Format(Enum e, FormatContext& ctx) const {
            return FormatText(EnumUtils<Enum>::ToString(e), ctx);
        }
    };

}  // namespace BECore

/**
 * @brief Defines `enum class Enum : macType` with names for EnumUtils, and `+e` for the underlying value; usable in any namespace.
 *
 * @example
 * namespace MyGame {
 *     CORE_ENUM(Direction, uint8_t, North, South, East, West)
 * }
 */
#define CORE_ENUM(Enum, macType, ...)                                                                                                                                                                  \
    enum class Enum : macType { __VA_ARGS__ };                                                                                                                                                         \
    inline auto operator+(Enum e) noexcept {                                                                                                                                                           \
        return std::underlying_type_t<Enum>(e);                                                                                                                                                        \
    }                                                                                                                                                                                                  \
    namespace EnumImpl_##Enum {                                                                                                                                                                        \
        inline constexpr auto tokenizer = [] {                                                                                                                                                         \
            using enum Enum;                                                                                                                                                                           \
            return ::BECore::Impl::Tokenizer<Enum, __VA_ARGS__>{};                                                                                                                                     \
        }();                                                                                                                                                                                           \
    }                                                                                                                                                                                                  \
    consteval const auto* EnumTokenizerPtr(Enum) {                                                                                                                                                     \
        return &EnumImpl_##Enum::tokenizer;                                                                                                                                                            \
    }
