#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Format/Format.h>
#include <BECore/Logger/Logger.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/New.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/StringUtils.h>
#include <cstdint>
#include <expected>

namespace BECore::Tests {

    /*
     * The page "the C++ rules" of the guide for people (`docs/guide/cpp-rules.md`) excerpts the code below, one unit for each rule it explains. It
     * is here and not in the page so that it is compiled and run: a change of an engine type or macro that breaks a rule's example fails
     * `GuideCppRulesExampleTest` before it misleads a reader. The class names are used by this example only, because the reflection registry
     * refuses two classes of one name.
     */
    namespace GuideCppRulesExample {

        /// Coins by player name. The lookup takes a view of text owned elsewhere, so asking costs no `String`
        class Wallet {
        public:
            void Set(String name, int32_t coins) {
                _coins[std::move(name)] = coins;
            }

            [[nodiscard]] Optional<int32_t> Find(StringView name) const {
                const auto found = _coins.find(name);
                if (found == _coins.end()) {
                    return std::nullopt;
                }
                return found->second;
            }

        private:
            HashMap<String, int32_t> _coins;
        };

        /// The names this example's code knows. One text has one place, so a typo is a compile error
        struct Ids {
            static constexpr auto level = "level"_intern;
            static constexpr auto coins = "coins"_intern;
        };

        /// A field name read from a file is a name from data: it is interned once, and then compared as a pointer
        [[nodiscard]] inline bool IsLevelField(StringView nameFromData) {
            return PoolString::Intern(nameFromData) == Ids::level;
        }

        /// A thing that sets a flag when it is destroyed, so a test can see when the last owner let go
        class Treasure : public RefCounted {
        public:
            explicit Treasure(bool& destroyed) : _destroyed(destroyed) {}

            ~Treasure() override {
                _destroyed = true;
            }

        private:
            bool& _destroyed;
        };

        /// Owns a share of a treasure: the pointer keeps it alive. A function that only looks takes `const Treasure&` and owns nothing
        class Pouch {
        public:
            explicit Pouch(IntrusivePtr<Treasure> treasure) : _treasure(std::move(treasure)) {}

        private:
            IntrusivePtr<Treasure> _treasure;
        };

        CORE_ENUM(ParseError, uint8_t, Empty, NotANumber, OutOfRange)

        constexpr int32_t kFirstLevel = 1;
        constexpr int32_t kLastLevel = 50;

        /// A level written by a person: the failure is a value the caller reads, not an exception
        [[nodiscard]] inline std::expected<int32_t, ParseError> ParseLevel(StringView text) {
            if (text.empty()) {
                return std::unexpected(ParseError::Empty);
            }
            const Optional<int32_t> number = StringUtils::ParseNumber<int32_t>(text);
            if (!number) {
                return std::unexpected(ParseError::NotANumber);
            }
            if (*number < kFirstLevel || *number > kLastLevel) {
                return std::unexpected(ParseError::OutOfRange);
            }
            return *number;
        }

        /// A bad text is the player's or the file's mistake, so it is logged with its reason and the game goes on from the first level
        [[nodiscard]] inline int32_t LevelOrFirst(StringView text) {
            const auto level = ParseLevel(text);
            if (!level) {
                LOG_WARNING("Cannot read level '{}': {}", text, level.error());
                return kFirstLevel;
            }
            return *level;
        }

        /// A zero whole is a bug of the caller, not an input: it stops a debug build and costs nothing in a release one
        [[nodiscard]] inline int32_t PercentOf(int32_t part, int32_t whole) {
            ASSERT(whole > 0, "PercentOf needs a positive whole");
            return part * 100 / whole;
        }

        [[nodiscard]] inline String DescribeCoins(StringView name, int32_t coins) {
            return Format("{} has {} coins", name, coins);
        }

        /// Appends to a string the caller keeps, so a long text is built in one buffer
        inline void AppendCoins(String& text, StringView name, int32_t coins) {
            AppendFormat(text, "{} has {} coins", name, coins);
        }

        /// The score as text for a label drawn every frame. The text is a member and is rebuilt only when the score moved
        class ScoreLabel {
        public:
            [[nodiscard]] const String& Text(int32_t score) {
                if (_shown != score) {
                    _text.clear();
                    AppendFormat(_text, "Score: {}", score);
                    _shown = score;
                }
                return _text;
            }

        private:
            String _text;
            Optional<int32_t> _shown;
        };

    }  // namespace GuideCppRulesExample

}  // namespace BECore::Tests
