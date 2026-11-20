// The Mahjong proof of the formulas over lists (Values/Programs/Lists/CLAUDE.md): classic patterns of the scoring of a hand written as expressions (`kNativeMahjongBoolFormulas` and
// `kNativeMahjongIntFormulas`, the texts the native set also compiles) over a Mahjong-shaped hand (ValueListMahjongTypes.h) agree with plain C++ predicates over every combination of a
// small alphabet and over seeded random hands, in the interpreter and in the generated code.
//
// What this proves is the language: a pattern means what its C++ loop means, empty lists and missing elements included. What it does not prove is the scoring of the game: `ScoreSeat`
// and its tests live in the game (BagiEngine-Madjong); the agreement with them is the entry `mahjong-scoring-in-data`.

#include <BECore/Random/Rng.h>
#include <BECore/ValueListMahjongTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <NativePrograms/NativeProgramFormulas.h>
#include <NativePrograms/NativeProgramSet.h>
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueListMahjongData;

    namespace {

        constexpr uint64_t kSeed = 20260401;
        constexpr int kRandomHands = 3000;
        constexpr int kMostSets = 4;
        constexpr int kMostRandomSets = 7;
        constexpr int kMostRandomYaku = 6;
        constexpr int kMostFu = 12;
        constexpr int kMostHan = 5;
        constexpr double kFlagChance = 0.3;
        constexpr double kPungChance = 0.7;
        constexpr double kDragonSetChance = 0.6;
        constexpr double kYakuOnChance = 0.5;
        constexpr size_t kTilesWithFlags = 4;

        /// Sets the generated code on or off for the length of a scope and puts the state back
        class NativeSwitch {
        public:
            explicit NativeSwitch(bool enabled) : _was(NativePrograms::IsEnabled()) {
                NativePrograms::SetEnabled(enabled);
            }
            ~NativeSwitch() {
                NativePrograms::SetEnabled(_was);
            }
            NativeSwitch(const NativeSwitch&) = delete;
            NativeSwitch& operator=(const NativeSwitch&) = delete;

        private:
            bool _was = false;
        };

        constexpr ValueListSuit kSuitA = ValueListSuit::Man;
        constexpr ValueListSuit kSuitB = ValueListSuit::Pin;

        /// A set the alphabet of the exhaustive hands builds from its three attributes; `fu` follows them so that the sum of fu tells the sets apart
        ValueListSet MakeSet(bool pung, bool dragon, bool second) {
            ValueListSet set;
            set.pung = pung;
            set.dragon = dragon;
            set.suit = second ? kSuitB : kSuitA;
            set.fu = (pung ? 4 : 1) + (dragon ? 2 : 0) + (second ? 8 : 0);
            return set;
        }

        constexpr int kSetAlphabet = 8;

        ValueListSet SetOfCode(int code) {
            return MakeSet((code & 1) != 0, (code & 2) != 0, (code & 4) != 0);
        }

        /// Every list of 0 to `kMostSets` sets over the alphabet of `kSetAlphabet` sets, the rest of the hand as it is
        void AddEverySetList(Vector<ValueListHand>& hands) {
            for (int length = 0; length <= kMostSets; ++length) {
                int total = 1;
                for (int i = 0; i < length; ++i) {
                    total *= kSetAlphabet;
                }
                for (int code = 0; code < total; ++code) {
                    ValueListHand hand;
                    int rest = code;
                    for (int i = 0; i < length; ++i) {
                        hand.sets.push_back(SetOfCode(rest % kSetAlphabet));
                        rest /= kSetAlphabet;
                    }
                    hands.push_back(hand);
                }
            }
        }

        constexpr int kSuitCount = 5;

        /// Every first suit with no tile that differs from it and with one tile at every position that has every other suit: `all(.., t.suit == tiles[0].suit)` is true for the first and
        /// false for the rest
        void AddEveryOneSuitHand(Vector<ValueListHand>& hands) {
            for (int first = 0; first < kSuitCount; ++first) {
                ValueListHand hand;
                for (ValueListTile& tile : hand.tiles) {
                    tile.suit = static_cast<ValueListSuit>(first);
                }
                hands.push_back(hand);
                for (size_t position = 0; position < hand.tiles.size(); ++position) {
                    for (int other = 0; other < kSuitCount; ++other) {
                        ValueListHand changed = hand;
                        changed.tiles[position].suit = static_cast<ValueListSuit>(other);
                        hands.push_back(changed);
                    }
                }
            }
        }

        /// Every combination of the flags of the first `kTilesWithFlags` tiles (honor, terminal)
        void AddEveryFlagHand(Vector<ValueListHand>& hands) {
            constexpr int kFlagsPerTile = 2;
            for (int code = 0; code < (1 << (kFlagsPerTile * static_cast<int>(kTilesWithFlags))); ++code) {
                ValueListHand hand;
                for (size_t i = 0; i < kTilesWithFlags; ++i) {
                    hand.tiles[i].honor = ((code >> (kFlagsPerTile * static_cast<int>(i))) & 1) != 0;
                    hand.tiles[i].terminal = ((code >> (kFlagsPerTile * static_cast<int>(i) + 1)) & 1) != 0;
                }
                hands.push_back(hand);
            }
        }

        /// Every list of 0 to `kMostYaku` yaku, each on or off with a han of 1 or 2
        void AddEveryYakuList(Vector<ValueListHand>& hands) {
            constexpr int kMostYaku = 4;
            constexpr int kYakuAlphabet = 4;
            for (int length = 0; length <= kMostYaku; ++length) {
                int total = 1;
                for (int i = 0; i < length; ++i) {
                    total *= kYakuAlphabet;
                }
                for (int code = 0; code < total; ++code) {
                    ValueListHand hand;
                    int rest = code;
                    for (int i = 0; i < length; ++i) {
                        ValueListYaku yaku;
                        yaku.on = (rest & 1) != 0;
                        yaku.han = (rest & 2) != 0 ? 2 : 1;
                        hand.yaku.push_back(yaku);
                        rest /= kYakuAlphabet;
                    }
                    hands.push_back(hand);
                }
            }
        }

        void AddRandomHands(Vector<ValueListHand>& hands) {
            Rng rng(kSeed);
            const auto suit = [&rng] { return static_cast<ValueListSuit>(rng.UniformInt(0, kSuitCount - 1)); };
            for (int n = 0; n < kRandomHands; ++n) {
                ValueListHand hand;
                // A third of the hands are of one suit so that the pattern that needs it is true as often as it is false
                const ValueListSuit pure = suit();
                const bool onePure = n % 3 == 0;
                for (ValueListTile& tile : hand.tiles) {
                    tile.suit = onePure ? pure : suit();
                    tile.honor = rng.Chance(kFlagChance);
                    tile.terminal = rng.Chance(kFlagChance);
                    tile.dragon = rng.Chance(kFlagChance);
                }
                const int sets = rng.UniformInt(0, kMostRandomSets);
                for (int s = 0; s < sets; ++s) {
                    ValueListSet set;
                    set.pung = rng.Chance(kPungChance);
                    // Often a dragon set, so that three pungs of dragons happen
                    set.dragon = rng.Chance(kDragonSetChance);
                    set.suit = suit();
                    set.fu = rng.UniformInt(0, kMostFu);
                    hand.sets.push_back(set);
                }
                const int yaku = rng.UniformInt(0, kMostRandomYaku);
                for (int y = 0; y < yaku; ++y) {
                    ValueListYaku entry;
                    entry.on = rng.Chance(kYakuOnChance);
                    entry.han = rng.UniformInt(1, kMostHan);
                    hand.yaku.push_back(entry);
                }
                hands.push_back(hand);
            }
        }

        // The patterns as C++: the reference is plain loops, so a mistake of the language and not of a shared helper shows

        bool ThreePungsOfDragons(const ValueListHand& hand) {
            int count = 0;
            for (const ValueListSet& set : hand.sets) {
                count += (set.pung && set.dragon) ? 1 : 0;
            }
            return count == 3;
        }

        bool OneSuit(const ValueListHand& hand) {
            for (const ValueListTile& tile : hand.tiles) {
                if (tile.suit != hand.tiles[0].suit) {
                    return false;
                }
            }
            return true;
        }

        bool AnyYakuOn(const ValueListHand& hand) {
            for (const ValueListYaku& yaku : hand.yaku) {
                if (yaku.on) {
                    return true;
                }
            }
            return false;
        }

        /// A missing set reads as a default one: the suit of a default set is the first suit
        bool PairOfSetsOfOneSuit(const ValueListHand& hand) {
            const auto suitAt = [&hand](size_t index) { return index < hand.sets.size() ? hand.sets[index].suit : ValueListSet{}.suit; };
            return suitAt(0) == suitAt(1);
        }

        int32_t Terminals(const ValueListHand& hand) {
            return static_cast<int32_t>(std::count_if(hand.tiles.begin(), hand.tiles.end(), [](const ValueListTile& tile) { return tile.terminal; }));
        }

        int32_t Honors(const ValueListHand& hand) {
            return static_cast<int32_t>(std::count_if(hand.tiles.begin(), hand.tiles.end(), [](const ValueListTile& tile) { return tile.honor; }));
        }

        int32_t TerminalsOrHonors(const ValueListHand& hand) {
            return static_cast<int32_t>(std::count_if(hand.tiles.begin(), hand.tiles.end(), [](const ValueListTile& tile) { return tile.terminal || tile.honor; }));
        }

        int32_t SumOfFu(const ValueListHand& hand) {
            int32_t total = 0;
            for (const ValueListSet& set : hand.sets) {
                total += set.fu;
            }
            return total;
        }

        int32_t HanOfYakuThatAreOn(const ValueListHand& hand) {
            int32_t total = 0;
            for (const ValueListYaku& yaku : hand.yaku) {
                total += yaku.on ? yaku.han : 0;
            }
            return total;
        }

        using BoolPredicate = bool (*)(const ValueListHand&);
        using IntPredicate = int32_t (*)(const ValueListHand&);

        /// The predicates in the order of the two tables of texts
        constexpr BoolPredicate kBoolPredicates[] = {ThreePungsOfDragons, OneSuit, AnyYakuOn, PairOfSetsOfOneSuit};
        constexpr IntPredicate kIntPredicates[] = {Terminals, Honors, TerminalsOrHonors, SumOfFu, HanOfYakuThatAreOn};
        static_assert(std::size(kBoolPredicates) == std::size(kNativeMahjongBoolFormulas));
        static_assert(std::size(kIntPredicates) == std::size(kNativeMahjongIntFormulas));

        /// Counts the hands the predicate says yes to, so a test can tell that the data held both answers
        struct Tally {
            size_t yes = 0;
            size_t no = 0;
        };

        /// Evaluates @p text over every hand with the generated code off and on and expects the predicate's answer; the generated function must be bound to the program, or the second pass
        /// would run the interpreter again
        template <typename T, typename TPredicate>
        void ExpectAgreement(StringView text, TPredicate predicate, const Vector<ValueListHand>& hands, Tally* tally) {
            const auto compiled = CompileValue<T>(text, NativeProgramSetDeclaration());
            ASSERT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? "" : compiled.error().message.c_str());
            const Value<T>& value = *compiled;
            ASSERT_EQ(value.Kind(), ValueKind::Program) << String(text).c_str();

            Context context(NativeProgramSetDeclaration());
            for (const bool generated : {false, true}) {
                const NativeSwitch mode(generated);
                if (generated) {
                    ASSERT_NE(ProgramPool::Native(value.Id()), nullptr) << String(text).c_str() << ": no generated function is bound, the set in tests/NativePrograms lacks it";
                }
                size_t mismatches = 0;
                for (const ValueListHand& hand : hands) {
                    context.Set(kMahjongLevel, hand);
                    const T expected = predicate(hand);
                    if (value.Evaluate(context) != expected) {
                        EXPECT_EQ(value.Evaluate(context), expected)
                            << String(text).c_str() << (generated ? " (generated code)" : " (interpreter)") << ", hand with " << hand.sets.size() << " sets and " << hand.yaku.size() << " yaku";
                        if (++mismatches > 3) {
                            return;
                        }
                    }
                    if (tally != nullptr && !generated) {
                        (expected != T{}) ? ++tally->yes : ++tally->no;
                    }
                }
            }
        }

        void ExpectEveryPatternAgrees(const Vector<ValueListHand>& hands) {
            for (size_t i = 0; i < std::size(kNativeMahjongBoolFormulas); ++i) {
                ExpectAgreement<bool>(kNativeMahjongBoolFormulas[i], kBoolPredicates[i], hands, nullptr);
            }
            for (size_t i = 0; i < std::size(kNativeMahjongIntFormulas); ++i) {
                ExpectAgreement<int32_t>(kNativeMahjongIntFormulas[i], kIntPredicates[i], hands, nullptr);
            }
        }

    }  // namespace

    TEST(ValueListMahjongTest, EveryPatternAgreesWithCppOverEveryListOfSetsOfASmallAlphabet) {
        Vector<ValueListHand> hands;
        AddEverySetList(hands);
        ASSERT_GT(hands.size(), 4000U);
        ExpectEveryPatternAgrees(hands);
    }

    TEST(ValueListMahjongTest, EveryPatternAgreesWithCppOverEveryOneSuitHandAndEveryHandThatDiffersInOneTile) {
        Vector<ValueListHand> hands;
        AddEveryOneSuitHand(hands);
        ExpectEveryPatternAgrees(hands);
    }

    TEST(ValueListMahjongTest, EveryPatternAgreesWithCppOverEveryCombinationOfTheFlagsOfFourTiles) {
        Vector<ValueListHand> hands;
        AddEveryFlagHand(hands);
        ExpectEveryPatternAgrees(hands);
    }

    TEST(ValueListMahjongTest, EveryPatternAgreesWithCppOverEveryListOfYaku) {
        Vector<ValueListHand> hands;
        AddEveryYakuList(hands);
        ExpectEveryPatternAgrees(hands);
    }

    TEST(ValueListMahjongTest, EveryPatternAgreesWithCppOverSeededRandomHands) {
        Vector<ValueListHand> hands;
        AddRandomHands(hands);
        ExpectEveryPatternAgrees(hands);
    }

    TEST(ValueListMahjongTest, ThePatternsThatAreBooleanAreTrueForSomeHandsAndFalseForOthersOverTheRandomHands) {
        // A proof against a predicate that is always false would pass for a formula that is always false
        Vector<ValueListHand> hands;
        AddRandomHands(hands);
        for (size_t i = 0; i < std::size(kNativeMahjongBoolFormulas); ++i) {
            Tally tally;
            ExpectAgreement<bool>(kNativeMahjongBoolFormulas[i], kBoolPredicates[i], hands, &tally);
            EXPECT_GT(tally.yes, 0U) << String(kNativeMahjongBoolFormulas[i]).c_str();
            EXPECT_GT(tally.no, 0U) << String(kNativeMahjongBoolFormulas[i]).c_str();
        }
    }

    TEST(ValueListMahjongTest, ThePairOfSetsReadsTheDefaultSuitOfASetThatIsMissing) {
        // The first suit is the default: no sets and one set of the first suit are a pair of one suit, one set of another suit is not
        const auto compiled = CompileValue<bool>(kNativeMahjongBoolFormulas[3], NativeProgramSetDeclaration());
        ASSERT_TRUE(compiled.has_value());
        Context context(NativeProgramSetDeclaration());
        ValueListHand hand;
        context.Set(kMahjongLevel, hand);
        EXPECT_TRUE(compiled->Evaluate(context));
        hand.sets.push_back(MakeSet(false, false, false));
        EXPECT_TRUE(compiled->Evaluate(context));
        hand.sets[0] = MakeSet(false, false, true);
        EXPECT_FALSE(compiled->Evaluate(context));
    }

}  // namespace BECore::Tests
