#pragma once

#include <EASTL/array.h>
#include <EASTL/string_view.h>

namespace BECore {

    namespace Impl {

        namespace ranges = std::ranges;

        template <class Ty>
        inline constexpr bool hasStrings = false;

        template <class Ty>
        inline constexpr Ty Tokens = Ty{};

        template <auto EnumVal>
        static consteval auto enumName() {
#ifdef _MSC_VER
            // MSVC: auto __cdecl Impl::enumName<align::CM>(void)
            constexpr std::string_view sv{__FUNCSIG__};  // NOLINT(clang-diagnostic-language-extension-token)
            constexpr size_t last = sv.find_last_of(">");
#else
            // clang: auto Impl::name() [E = align::CM]
            // gcc: consteval auto Impl::name() [with auto E = align::CM]
            constexpr std::string_view sv{__PRETTY_FUNCTION__};
            constexpr auto last = sv.find_last_of("]");
#endif
            constexpr size_t first = sv.find_last_of(':', last) + 1;
            std::array<char, last - first + 1> buf{};  // +1 '\0' terminated c_str
            ranges::copy(std::string_view{sv.data() + first, last - first}, buf.begin());
            return buf;
        }

        template <auto EnumVal>
        inline constexpr auto ENameArr{enumName<EnumVal>()};

        template <auto EnumVal>
        inline constexpr eastl::string_view EnumName{ENameArr<EnumVal>.data(), ENameArr<EnumVal>.size() - 1};

        template <typename Enum, auto... Enums>
        class Tokenizer {
            static constexpr size_t Count = sizeof...(Enums);

            struct Data {
                eastl::string_view name;
                Enum value;
            };

            static constexpr eastl::array<Data, Count> tokens{Data{EnumName<Enums>, Enums}...};
            static constexpr eastl::array<eastl::string_view, Count> names{EnumName<Enums>...};
            static constexpr eastl::array<Enum, Count> values{Enums...};

            using Ty = std::underlying_type_t<Enum>;
            static constexpr Enum errorValue{static_cast<Enum>(std::numeric_limits<Ty>::max())};

            // Runtime PoolString cache
            struct RuntimeCache {
                eastl::array<PoolString, Count> poolStrings{};
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

            static constexpr eastl::string_view ToString(Enum e) noexcept {
                auto it = ranges::find(tokens, e, &Data::value);
                return it == tokens.end() ? eastl::string_view{} : it->name;
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

            static constexpr Enum ToEnum(eastl::string_view str) noexcept {
                auto it = ranges::find(tokens, str, &Data::name);
                return it == tokens.end() ? errorValue : it->value;
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
            static const eastl::array<PoolString, Count>& GetPoolStrings() noexcept {
                return GetCache().poolStrings;
            }

            // Access to compile-time string_view array
            static constexpr const eastl::array<eastl::string_view, Count>& GetNames() noexcept {
                return names;
            }

            // Access to compile-time values array
            static constexpr const eastl::array<Enum, Count>& GetValues() noexcept {
                return values;
            }
        };

    }  // namespace Impl

#define CORE_ENUM(Enum, macType, ...)                                      \
    enum class Enum : macType { __VA_ARGS__ };                             \
    inline auto operator+(Enum e) noexcept {                               \
        return std::underlying_type_t<Enum>(e);                            \
    }                                                                      \
    namespace Impl {                                                       \
        template <>                                                        \
        inline constexpr auto hasStrings<Enum> = true;                     \
        template <>                                                        \
        inline constexpr auto Tokens<Enum> = [] {                          \
            using enum Enum;                                               \
            return Tokenizer<Enum, __VA_ARGS__>{};                         \
        }();                                                               \
    }

    // Returns constexpr eastl::string_view
    template <typename E, typename Enum = std::remove_cvref_t<E>>
        requires Impl::hasStrings<Enum>
    inline constexpr eastl::string_view EnumToString(E e) noexcept {
        return Impl::Tokens<Enum>.ToString(e);
    }

    // Returns PoolString (runtime, from cached array)
    template <typename E, typename Enum = std::remove_cvref_t<E>>
        requires Impl::hasStrings<Enum>
    inline PoolString EnumToPoolString(E e) noexcept {
        return Impl::Tokens<Enum>.ToPoolString(e);
    }

    // Parse eastl::string_view to enum
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr Enum StringToEnum(eastl::string_view str) noexcept {
        return Impl::Tokens<Enum>.ToEnum(str);
    }

    // Parse std::string_view to enum
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr Enum StringToEnum(std::string_view str) noexcept {
        return Impl::Tokens<Enum>.ToEnum(eastl::string_view{str.data(), str.size()});
    }

    // Parse const char* to enum (resolves ambiguity)
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr Enum StringToEnum(const char* str) noexcept {
        return Impl::Tokens<Enum>.ToEnum(eastl::string_view{str});
    }

    // Parse PoolString to enum
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline Enum PoolStringToEnum(PoolString ps) noexcept {
        return Impl::Tokens<Enum>.FromPoolString(ps);
    }

    // Returns std::optional<Enum> for safe parsing (eastl::string_view)
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr std::optional<Enum> EnumCast(eastl::string_view str) noexcept {
        auto result = StringToEnum<Enum>(str);
        using Ty = std::underlying_type_t<Enum>;
        constexpr Enum errorValue{static_cast<Enum>(std::numeric_limits<Ty>::max())};
        if (result == errorValue) {
            return std::nullopt;
        }
        return result;
    }

    // Returns std::optional<Enum> for safe parsing (std::string_view)
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr std::optional<Enum> EnumCast(std::string_view str) noexcept {
        return EnumCast<Enum>(eastl::string_view{str.data(), str.size()});
    }

    // Returns std::optional<Enum> for safe parsing (const char*)
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr std::optional<Enum> EnumCast(const char* str) noexcept {
        return EnumCast<Enum>(eastl::string_view{str});
    }

    // Get count of enum values
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr size_t EnumCount() noexcept {
        return Impl::Tokens<Enum>.EnumCount;
    }

    // Get all PoolStrings for enum (cached array)
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline const auto& EnumPoolStrings() noexcept {
        return Impl::Tokens<Enum>.GetPoolStrings();
    }

    // Get all string_views for enum (compile-time)
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr const auto& EnumNames() noexcept {
        return Impl::Tokens<Enum>.GetNames();
    }

    // Get all values for enum (compile-time)
    template <typename Enum>
        requires Impl::hasStrings<Enum>
    inline constexpr const auto& EnumValues() noexcept {
        return Impl::Tokens<Enum>.GetValues();
    }

}  // namespace BECore
