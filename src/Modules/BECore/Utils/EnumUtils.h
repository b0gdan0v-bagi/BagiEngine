#pragma once

#include <BECore/Format/Formatter.h>
#include <EASTL/array.h>

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

            // Runtime PoolString cache
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

            // Access to the cached PoolString array
            static const Array<PoolString, Count>& GetPoolStrings() noexcept {
                return GetCache().poolStrings;
            }

            // Access to compile-time string_view array
            static constexpr const Array<StringView, Count>& GetNames() noexcept {
                return names;
            }

            // Access to compile-time values array
            static constexpr const Array<Enum, Count>& GetValues() noexcept {
                return values;
            }
        };

    }  // namespace Impl

    /**
     * @brief Utility class for enum reflection and conversion
     *
     * Provides compile-time and runtime conversion between enum values,
     * strings, and PoolStrings. Works with enums defined using CORE_ENUM macro.
     *
     * @tparam Enum The enum type (must be registered with CORE_ENUM)
     *
     * @example
     * // Define enum in any namespace:
     * namespace MyGame {
     *     CORE_ENUM(Direction, uint8_t, North, South, East, West)
     * }
     *
     * // Usage:
     * using BECore::EnumUtils;
     * constexpr auto name = EnumUtils<MyGame::Direction>::ToString(MyGame::Direction::North);
     * auto dir = EnumUtils<MyGame::Direction>::FromString("South");
     */
    template <typename Enum>
    class EnumUtils {
        static constexpr const auto& Tok() {
            return *EnumTokenizerPtr(Enum{});  // ADL finds the friend function
        }

    public:
        /// Returns the number of enum values
        static constexpr size_t Count() noexcept {
            return Tok().EnumCount;
        }

        /// Convert enum to string_view (constexpr)
        static constexpr StringView ToString(Enum e) noexcept {
            return Tok().ToString(e);
        }

        /// Convert enum to PoolString (runtime, cached)
        static PoolString ToPoolString(Enum e) noexcept {
            return Tok().ToPoolString(e);
        }

        /// Parse eastl::string_view to enum
        static constexpr Enum FromString(StringView str) noexcept {
            return Tok().ToEnum(str);
        }

        /// Parse std::string_view to enum
        static constexpr Enum FromString(std::string_view str) noexcept {  //-NoStd: the callers that hold a std view
            return Tok().ToEnum(FromStd(str));
        }

        /// Parse const char* to enum
        static constexpr Enum FromString(const char* str) noexcept {
            return Tok().ToEnum(StringView{str});
        }

        /// Parse PoolString to enum
        static Enum FromPoolString(PoolString ps) noexcept {
            return Tok().FromPoolString(ps);
        }

        /// Safe cast from eastl::string_view to enum (returns nullopt on failure)
        static constexpr Optional<Enum> Cast(StringView str) noexcept {
            auto result = FromString(str);
            if (result == Tok().ErrorValue()) {
                return std::nullopt;
            }
            return result;
        }

        /// Safe cast from std::string_view to enum (returns nullopt on failure)
        static constexpr Optional<Enum> Cast(std::string_view str) noexcept {  //-NoStd: the callers that hold a std view
            return Cast(FromStd(str));
        }

        /// Safe cast from const char* to enum (returns nullopt on failure)
        static constexpr Optional<Enum> Cast(const char* str) noexcept {
            return Cast(StringView{str});
        }

        /// Get all enum names as constexpr array
        static constexpr const auto& Names() noexcept {
            return Tok().GetNames();
        }

        /// Get all enum values as constexpr array
        static constexpr const auto& Values() noexcept {
            return Tok().GetValues();
        }

        /// Get all PoolStrings for enum values (cached)
        static const auto& PoolStrings() noexcept {
            return Tok().GetPoolStrings();
        }
    };

    /**
     * @brief Concept: T is an enum registered with CORE_ENUM (has ADL-discoverable EnumTokenizerPtr).
     */
    template <typename T>
    concept IsReflectedEnum = std::is_enum_v<T> && requires {
        {EnumTokenizerPtr(T{})};
    };

}  // namespace BECore

// Formatting support for EnumUtils-enabled enums

namespace BECore::Detail {
    // Helper trait to check if EnumUtils is available for a type
    template <typename T, typename = void>
    struct HasEnumUtils : std::false_type {};

    template <typename T>
    struct HasEnumUtils<T, std::enable_if_t<std::is_enum_v<T>>> : std::true_type {};
}  // namespace BECore::Detail

namespace BECore {

    /**
     * @brief Formatter specialization for enums with EnumUtils support (Format/CLAUDE.md)
     *
     * Enables formatting for any enum type that supports EnumUtils::ToString(): the text is the name of the value,
     * and a string spec works (`{:>8}`, `{:<7}|`).
     */
    template <typename Enum>
    struct Formatter<Enum, std::enable_if_t<Detail::HasEnumUtils<Enum>::value>> : TextFormatter {
        FormatContext::iterator Format(Enum e, FormatContext& ctx) const {
            return FormatText(EnumUtils<Enum>::ToString(e), ctx);
        }
    };

}  // namespace BECore

/**
 * @brief Macro to define an enum with reflection support
 *
 * Creates an enum class with automatic string conversion capabilities.
 * Can be used in any namespace.
 *
 * @param Enum The enum name
 * @param macType The underlying type (e.g., uint8_t)
 * @param ... Enum values
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
