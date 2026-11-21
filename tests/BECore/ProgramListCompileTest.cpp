// Formulas over lists, compiled from their text (Values/Programs/ProgramCompiler, Lists/CLAUDE.md): each one runs in the interpreter over seeded hands and must give what the plain C++ loop gives
// over the same objects; the folds' identities, the early exit of `any` / `all`, the out-of-range index, the register limit and the allocations of a run are checked on their own.

#include <BECore/ProgramListTypes.h>
#include <BECore/Random/Rng.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace ProgramListData;

    namespace {

        constexpr LevelIndex kHandLevel = 0;
        constexpr int kVariants = 24;
        constexpr int kTileCount = 14;
        constexpr int kMaxRank = 20;
        constexpr int kWeightSteps = 64;
        constexpr float kWeightStep = 0.125f;
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInfinity = std::numeric_limits<float>::infinity();

        /// A hand that a seed fills: the lists change length with the seed (a `SmallVector` is inline up to four, and over it from five), and `before` and `after` stand around them
        struct HandWorld {
            ContextDecl declaration;
            ProgramListHand hand;
            Context context;

            explicit HandWorld(int variant) {
                static_cast<void>(declaration.Add<ProgramListHand>("hand"));
                context = Context(declaration);
                context.Set(kHandLevel, hand);
                Fill(variant);
            }

            HandWorld(const HandWorld&) = delete;
            HandWorld& operator=(const HandWorld&) = delete;

            void Fill(int variant) {
                Rng rng(static_cast<uint64_t>(variant) + 1);
                hand.before = variant % 5 - 1;
                hand.after = 222;
                const auto suits = static_cast<int>(EnumUtils<ProgramListSuit>::Count());
                for (ProgramListTile& tile : hand.tiles) {
                    FillTile(tile, rng, suits);
                }
                for (int s = 0; s < variant % 6; ++s) {
                    ProgramListSet set;
                    set.pung = rng.UniformInt(0, 1) == 1;
                    for (ProgramListTile& tile : set.tiles) {
                        FillTile(tile, rng, suits);
                    }
                    hand.sets.push_back(set);
                }
                for (int k = 0; k < variant % 7; ++k) {
                    hand.counts.push_back(rng.UniformInt(-kMaxRank, kMaxRank));
                }
                for (int k = 0; k < variant % 9; ++k) {
                    hand.weights.push_back(Weight(rng));
                }
                for (bool& flag : hand.flags) {
                    flag = rng.UniformInt(0, 1) == 1;
                }
            }

            static float Weight(Rng& rng) {
                return static_cast<float>(rng.UniformInt(-kWeightSteps, kWeightSteps)) * kWeightStep;
            }

            static void FillTile(ProgramListTile& tile, Rng& rng, int suits) {
                tile.suit = static_cast<ProgramListSuit>(rng.UniformInt(0, suits - 1));
                tile.terminal = rng.UniformInt(0, 3) == 0;
                tile.rank = rng.UniformInt(-kMaxRank, kMaxRank);
                tile.weight = Weight(rng);
                tile.position = Vec3{Weight(rng), Weight(rng), Weight(rng)};
                tile.name = rng.UniformInt(0, 1) == 0 ? PoolString("alpha"_intern) : PoolString("beta"_intern);
            }
        };

        /// The program of @p text, compiled against the hand's declaration for the type @p T
        template <typename T>
        ProgramId Compile(const ContextDecl& declaration, StringView text, bool fuse = true) {
            CompileOptions options;
            options.fuse = fuse;
            const auto compiled = CompileExpression(text, declaration, DescribeType<T>(), options);
            EXPECT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? String{} : compiled.error().message).c_str();
            if (!compiled.has_value() || compiled->kind != CompiledKind::Program) {
                ADD_FAILURE() << String(text).c_str() << " is no program";
                return 0;
            }
            return compiled->program;
        }

        /// Runs @p text over every variant of the hand and compares it with @p reference, the loop in C++; the result of a float is the same bits, as the sums are taken in the same order
        template <typename T, typename Reference>
        void ExpectLoop(StringView text, Reference&& reference) {
            const HandWorld first(0);
            const ProgramId id = Compile<T>(first.declaration, text);
            for (int variant = 0; variant < kVariants; ++variant) {
                const HandWorld world(variant);
                EXPECT_EQ(Run<T>(ProgramPool::Get(id), world.context), reference(world.hand)) << String(text).c_str() << " variant " << variant;
            }
        }

        template <typename T>
        T RunOver(const HandWorld& world, StringView text) {
            return Run<T>(ProgramPool::Get(Compile<T>(world.declaration, text)), world.context);
        }

        /// The error of a text that must not compile
        ProgramError Rejected(StringView text, const TypeDesc& expected) {
            const HandWorld world(0);
            const auto compiled = CompileExpression(text, world.declaration, expected);
            EXPECT_FALSE(compiled.has_value()) << String(text).c_str();
            return compiled.has_value() ? ProgramError{} : compiled.error();
        }

        /// The accumulation of a fold in C++: `sum` over the tiles of one field
        template <typename T, typename Field>
        T SumOver(const ProgramListHand& hand, Field field) {
            T total{};
            for (const ProgramListTile& tile : hand.tiles) {
                total += static_cast<T>(field(tile));
            }
            return total;
        }

        /// @p depth folds, each inside the body of the one before: the lists alternate between the sets of the hand and the tiles of the set above (a set is read at the end of an odd depth)
        String Nest(int depth) {
            String text = depth % 2 == 1 ? Format("a{}.pung", depth - 1) : Format("a{}.terminal", depth - 1);
            for (int level = depth - 1; level >= 0; --level) {
                const String list = level % 2 == 0 ? String("hand.sets") : Format("a{}.tiles", level - 1);
                text = Format("count({}, a{}, {}) > 0", list, level, text);
                if (level > 0) {
                    text = Format("({})", text);
                }
            }
            return text;
        }

    }  // namespace

    TEST(ProgramListCompileTest, FoldsOverAnArrayOfObjectsGiveWhatTheLoopGives) {
        ExpectLoop<int32_t>("count(hand.tiles, t, t.terminal)",
                            [](const ProgramListHand& hand) { return static_cast<int32_t>(std::count_if(hand.tiles.begin(), hand.tiles.end(), [](const auto& t) { return t.terminal; })); });
        ExpectLoop<bool>("any(hand.tiles, t, t.rank > 17)", [](const ProgramListHand& hand) { return std::any_of(hand.tiles.begin(), hand.tiles.end(), [](const auto& t) { return t.rank > 17; }); });
        ExpectLoop<bool>("all(hand.tiles, t, t.rank > -19)", [](const ProgramListHand& hand) { return std::all_of(hand.tiles.begin(), hand.tiles.end(), [](const auto& t) { return t.rank > -19; }); });
        ExpectLoop<int32_t>("sum(hand.tiles, t, t.rank)", [](const ProgramListHand& hand) { return SumOver<int32_t>(hand, [](const auto& t) { return t.rank; }); });
        ExpectLoop<int32_t>("min(hand.tiles, t, t.rank)",
                            [](const ProgramListHand& hand) { return std::min_element(hand.tiles.begin(), hand.tiles.end(), [](const auto& a, const auto& b) { return a.rank < b.rank; })->rank; });
        ExpectLoop<int32_t>("max(hand.tiles, t, t.rank)",
                            [](const ProgramListHand& hand) { return std::max_element(hand.tiles.begin(), hand.tiles.end(), [](const auto& a, const auto& b) { return a.rank < b.rank; })->rank; });
    }

    TEST(ProgramListCompileTest, TheResultTypeIsTheTypeOfTheBody) {
        ExpectLoop<int64_t>("sum(hand.tiles, t, int64(t.rank) * 1000000000)",
                            [](const ProgramListHand& hand) { return SumOver<int64_t>(hand, [](const auto& t) { return int64_t{t.rank} * 1000000000; }); });
        ExpectLoop<int64_t>("max(hand.tiles, t, int64(t.rank))", [](const ProgramListHand& hand) {
            int64_t best = std::numeric_limits<int64_t>::lowest();
            for (const auto& t : hand.tiles) {
                best = std::max<int64_t>(best, t.rank);
            }
            return best;
        });
        ExpectLoop<float>("sum(hand.tiles, t, t.weight)", [](const ProgramListHand& hand) { return SumOver<float>(hand, [](const auto& t) { return t.weight; }); });
        ExpectLoop<float>("min(hand.tiles, t, t.weight)", [](const ProgramListHand& hand) {
            float best = kInfinity;
            for (const auto& t : hand.tiles) {
                best = std::min(best, t.weight);
            }
            return best;
        });
        ExpectLoop<double>("sum(hand.tiles, t, double(t.weight) * 0.1)",
                           [](const ProgramListHand& hand) { return SumOver<double>(hand, [](const auto& t) { return static_cast<double>(t.weight) * 0.1; }); });
        ExpectLoop<double>("max(hand.tiles, t, double(t.weight))", [](const ProgramListHand& hand) {
            double best = -std::numeric_limits<double>::infinity();
            for (const auto& t : hand.tiles) {
                best = std::max(best, static_cast<double>(t.weight));
            }
            return best;
        });
        ExpectLoop<int32_t>("sum(hand.counts, c, 1)", [](const ProgramListHand& hand) { return static_cast<int32_t>(hand.counts.size()); });
        ExpectLoop<int32_t>("sum(hand.tiles, t, t.rank * 2)", [](const ProgramListHand& hand) { return SumOver<int32_t>(hand, [](const auto& t) { return t.rank * 2; }); });
    }

    TEST(ProgramListCompileTest, EveryContainerIsWalkedByItsOwnLayout) {
        ExpectLoop<int32_t>("sum(hand.counts, c, c)", [](const ProgramListHand& hand) {
            int32_t total = 0;
            for (const int32_t value : hand.counts) {
                total += value;
            }
            return total;
        });
        ExpectLoop<int32_t>("count(hand.counts, c, c > 0)",
                            [](const ProgramListHand& hand) { return static_cast<int32_t>(std::count_if(hand.counts.begin(), hand.counts.end(), [](int32_t c) { return c > 0; })); });
        ExpectLoop<float>("sum(hand.weights, w, w)", [](const ProgramListHand& hand) {
            float total = 0.0f;
            for (const float value : hand.weights) {
                total += value;
            }
            return total;
        });
        ExpectLoop<int32_t>("count(hand.flags, f, f)", [](const ProgramListHand& hand) { return static_cast<int32_t>(std::count(hand.flags.begin(), hand.flags.end(), true)); });
        ExpectLoop<int32_t>("count(hand.sets, s, s.pung)",
                            [](const ProgramListHand& hand) { return static_cast<int32_t>(std::count_if(hand.sets.begin(), hand.sets.end(), [](const auto& s) { return s.pung; })); });
    }

    TEST(ProgramListCompileTest, AnEmptyListGivesTheIdentityOfTheFold) {
        const HandWorld world(0);
        ASSERT_TRUE(world.hand.none.empty());
        EXPECT_EQ(RunOver<int32_t>(world, "count(hand.none, t, t.terminal)"), 0);
        EXPECT_EQ(RunOver<int32_t>(world, "sum(hand.none, t, t.rank)"), 0);
        EXPECT_FALSE(RunOver<bool>(world, "any(hand.none, t, t.terminal)"));
        EXPECT_TRUE(RunOver<bool>(world, "all(hand.none, t, t.terminal)"));
        EXPECT_EQ(RunOver<int32_t>(world, "min(hand.none, t, t.rank)"), std::numeric_limits<int32_t>::max());
        EXPECT_EQ(RunOver<int32_t>(world, "max(hand.none, t, t.rank)"), std::numeric_limits<int32_t>::lowest());
        EXPECT_EQ(RunOver<int64_t>(world, "min(hand.none, t, int64(t.rank))"), std::numeric_limits<int64_t>::max());
        EXPECT_EQ(RunOver<int64_t>(world, "max(hand.none, t, int64(t.rank))"), std::numeric_limits<int64_t>::lowest());
        EXPECT_EQ(RunOver<float>(world, "min(hand.none, t, t.weight)"), kInfinity);
        EXPECT_EQ(RunOver<float>(world, "max(hand.none, t, t.weight)"), -kInfinity);
        EXPECT_EQ(RunOver<double>(world, "min(hand.none, t, double(t.weight))"), std::numeric_limits<double>::infinity());
        EXPECT_EQ(RunOver<double>(world, "max(hand.none, t, double(t.weight))"), -std::numeric_limits<double>::infinity());
        EXPECT_EQ(RunOver<int32_t>(world, "size(hand.none)"), 0);
    }

    TEST(ProgramListCompileTest, ALoopInALoopReadsTheListOfTheElement) {
        ExpectLoop<int32_t>("count(hand.sets, s, count(s.tiles, t, t.terminal) > 1)", [](const ProgramListHand& hand) {
            int32_t sets = 0;
            for (const auto& set : hand.sets) {
                sets += std::count_if(set.tiles.begin(), set.tiles.end(), [](const auto& t) { return t.terminal; }) > 1 ? 1 : 0;
            }
            return sets;
        });
        ExpectLoop<int32_t>("sum(hand.sets, s, sum(s.tiles, t, t.rank))", [](const ProgramListHand& hand) {
            int32_t total = 0;
            for (const auto& set : hand.sets) {
                for (const auto& tile : set.tiles) {
                    total += tile.rank;
                }
            }
            return total;
        });
    }

    TEST(ProgramListCompileTest, TheOuterVariableIsReadInTheInnerBodyAndAfterTheInnerLoop) {
        ExpectLoop<int32_t>("sum(hand.sets, s, count(hand.tiles, t, t.rank > s.tiles[0].rank) * s.tiles[1].rank)", [](const ProgramListHand& hand) {
            int32_t total = 0;
            for (const auto& set : hand.sets) {
                int32_t above = 0;
                for (const auto& tile : hand.tiles) {
                    above += tile.rank > set.tiles[0].rank ? 1 : 0;
                }
                total += above * set.tiles[1].rank;
            }
            return total;
        });
    }

    TEST(ProgramListCompileTest, SizeAndTheElementByIndex) {
        ExpectLoop<int32_t>("size(hand.tiles)", [](const ProgramListHand& hand) { return static_cast<int32_t>(hand.tiles.size()); });
        ExpectLoop<int32_t>("size(hand.sets)", [](const ProgramListHand& hand) { return static_cast<int32_t>(hand.sets.size()); });
        ExpectLoop<int32_t>("size(hand.sets[1].tiles) + hand.after",
                            [](const ProgramListHand& hand) { return (hand.sets.size() > 1 ? static_cast<int32_t>(hand.sets[1].tiles.size()) : 0) + hand.after; });
        ExpectLoop<int32_t>("hand.tiles[0].rank", [](const ProgramListHand& hand) { return hand.tiles[0].rank; });
        ExpectLoop<int32_t>("hand.tiles[13].rank", [](const ProgramListHand& hand) { return hand.tiles[kTileCount - 1].rank; });
        ExpectLoop<int32_t>("hand.counts[1]", [](const ProgramListHand& hand) { return hand.counts.size() > 1 ? hand.counts[1] : 0; });
        ExpectLoop<float>("hand.weights[2]", [](const ProgramListHand& hand) { return hand.weights.size() > 2 ? hand.weights[2] : 0.0f; });
        ExpectLoop<bool>("hand.flags[2]", [](const ProgramListHand& hand) { return hand.flags[2]; });
        ExpectLoop<int32_t>("hand.sets[1].tiles[2].rank", [](const ProgramListHand& hand) { return hand.sets.size() > 1 ? hand.sets[1].tiles[2].rank : 0; });
        // The index is read from the context, and `before` is -1 for some hands
        ExpectLoop<int32_t>("hand.tiles[hand.before].rank",
                            [](const ProgramListHand& hand) { return hand.before >= 0 && hand.before < kTileCount ? hand.tiles[static_cast<size_t>(hand.before)].rank : 0; });
        ExpectLoop<int32_t>("sum(hand.tiles, t, hand.tiles[t.rank].rank)", [](const ProgramListHand& hand) {
            int32_t total = 0;
            for (const auto& tile : hand.tiles) {
                total += tile.rank >= 0 && tile.rank < kTileCount ? hand.tiles[static_cast<size_t>(tile.rank)].rank : 0;
            }
            return total;
        });
    }

    TEST(ProgramListCompileTest, AnIndexOutsideTheListIsTheDefaultValue) {
        const HandWorld world(3);
        ASSERT_EQ(world.hand.tiles.size(), static_cast<size_t>(kTileCount));
        EXPECT_EQ(RunOver<int32_t>(world, "hand.tiles[-1].rank"), 0);
        EXPECT_EQ(RunOver<int32_t>(world, "hand.tiles[14].rank"), 0);
        EXPECT_EQ(RunOver<int32_t>(world, "hand.tiles[2147483647].rank"), 0);
        EXPECT_EQ(RunOver<int32_t>(world, "hand.tiles[0].rank"), world.hand.tiles[0].rank);
        EXPECT_EQ(RunOver<int32_t>(world, "hand.tiles[13].rank"), world.hand.tiles[kTileCount - 1].rank);
        EXPECT_FALSE(RunOver<bool>(world, "hand.tiles[99].terminal"));
        EXPECT_EQ(RunOver<float>(world, "hand.tiles[-5].position.x"), 0.0f);
        EXPECT_EQ(RunOver<int32_t>(world, "hand.sets[99].tiles[1].rank"), 0);
        EXPECT_EQ(RunOver<int32_t>(world, "size(hand.sets[99].tiles)"), 0);
        EXPECT_EQ(RunOver<int32_t>(world, "sum(hand.sets[99].tiles, t, t.rank)"), 0);
        EXPECT_EQ(RunOver<int32_t>(world, "hand.sets[0].tiles[7].rank"), 0) << "an index past the tiles of an element that exists";
        EXPECT_EQ(RunOver<int32_t>(world, "hand.counts[-1]"), 0);
    }

    TEST(ProgramListCompileTest, FieldsOfAnElementOfEveryKind) {
        ExpectLoop<int32_t>("count(hand.tiles, t, t.suit == Bamboo)", [](const ProgramListHand& hand) {
            return static_cast<int32_t>(std::count_if(hand.tiles.begin(), hand.tiles.end(), [](const auto& t) { return t.suit == ProgramListSuit::Bamboo; }));
        });
        ExpectLoop<int32_t>("count(hand.tiles, t, t.name == 'alpha')", [](const ProgramListHand& hand) {
            return static_cast<int32_t>(std::count_if(hand.tiles.begin(), hand.tiles.end(), [](const auto& t) { return t.name == "alpha"_intern; }));
        });
        ExpectLoop<float>("sum(hand.tiles, t, t.position.x)", [](const ProgramListHand& hand) { return SumOver<float>(hand, [](const auto& t) { return t.position.x; }); });
        ExpectLoop<float>("max(hand.tiles, t, t.position.z)", [](const ProgramListHand& hand) {
            float best = -kInfinity;
            for (const auto& t : hand.tiles) {
                best = std::max(best, t.position.z);
            }
            return best;
        });
        ExpectLoop<bool>("all(hand.tiles, t, t.suit == hand.tiles[0].suit)",
                         [](const ProgramListHand& hand) { return std::all_of(hand.tiles.begin(), hand.tiles.end(), [&](const auto& t) { return t.suit == hand.tiles[0].suit; }); });
        ExpectLoop<bool>("hand.tiles[3].suit == Winds", [](const ProgramListHand& hand) { return hand.tiles[3].suit == ProgramListSuit::Winds; });
    }

    TEST(ProgramListCompileTest, ANanElementIsSkippedByMinAndMax) {
        HandWorld world(8);
        ASSERT_EQ(world.hand.weights.size(), 8u);
        world.hand.weights[3] = kNan;
        float smallest = kInfinity;
        float largest = -kInfinity;
        for (const float weight : world.hand.weights) {
            if (!std::isnan(weight)) {
                smallest = std::min(smallest, weight);
                largest = std::max(largest, weight);
            }
        }
        EXPECT_EQ(RunOver<float>(world, "min(hand.weights, w, w)"), smallest);
        EXPECT_EQ(RunOver<float>(world, "max(hand.weights, w, w)"), largest);
        for (float& weight : world.hand.weights) {
            weight = kNan;
        }
        EXPECT_EQ(RunOver<float>(world, "min(hand.weights, w, w)"), kInfinity) << "nothing but NaN: the identity";
        EXPECT_EQ(RunOver<float>(world, "max(hand.weights, w, w)"), -kInfinity);
    }

    TEST(ProgramListCompileTest, TheEarlyExitOfAnyAndAllGivesTheAnswerOfTheFullLoop) {
        // The answer changes with the position of the one tile that decides it: every position, and a hand with none
        for (int position = -1; position < kTileCount; ++position) {
            HandWorld world(0);
            for (ProgramListTile& tile : world.hand.tiles) {
                tile.terminal = false;
                tile.rank = 1;
            }
            if (position >= 0) {
                world.hand.tiles[static_cast<size_t>(position)].terminal = true;
                world.hand.tiles[static_cast<size_t>(position)].rank = -1;
            }
            EXPECT_EQ(RunOver<bool>(world, "any(hand.tiles, t, t.terminal)"), position >= 0) << "position " << position;
            EXPECT_EQ(RunOver<bool>(world, "all(hand.tiles, t, t.rank > 0)"), position < 0) << "position " << position;
            EXPECT_EQ(RunOver<bool>(world, "any(hand.tiles, t, t.rank < 0) || all(hand.tiles, t, !t.terminal)"), true) << "position " << position;
            EXPECT_EQ(RunOver<int32_t>(world, "count(hand.tiles, t, t.terminal) + sum(hand.tiles, t, t.rank)"), (position >= 0 ? 1 : 0) + kTileCount - (position >= 0 ? 2 : 0))
                << "position " << position;
        }
        // A break of an inner loop leaves the inner loop only
        ExpectLoop<int32_t>("count(hand.sets, s, any(s.tiles, t, t.terminal) && all(s.tiles, t, t.rank < 15))", [](const ProgramListHand& hand) {
            int32_t sets = 0;
            for (const auto& set : hand.sets) {
                const bool anyTerminal = std::any_of(set.tiles.begin(), set.tiles.end(), [](const auto& t) { return t.terminal; });
                const bool allLow = std::all_of(set.tiles.begin(), set.tiles.end(), [](const auto& t) { return t.rank < 15; });
                sets += anyTerminal && allLow ? 1 : 0;
            }
            return sets;
        });
    }

    TEST(ProgramListCompileTest, AProgramReadsTheLevelOfItsList) {
        const HandWorld world(0);
        EXPECT_EQ(ProgramPool::Get(Compile<int32_t>(world.declaration, "count(hand.tiles, t, t.terminal)")).Reads(), LevelBit(kHandLevel));
        EXPECT_EQ(ProgramPool::Get(Compile<int32_t>(world.declaration, "hand.sets[0].tiles[1].rank")).Reads(), LevelBit(kHandLevel));
    }

    TEST(ProgramListCompileTest, FusionMakesNoDifferenceToAProgramWithALoop) {
        const HandWorld world(5);
        for (const StringView text : {StringView("sum(hand.tiles, t, t.rank * 2 + 1)"), StringView("count(hand.sets, s, s.pung) + hand.before * 2 + 1")}) {
            EXPECT_EQ(Compile<int32_t>(world.declaration, text, true), Compile<int32_t>(world.declaration, text, false)) << String(text).c_str();
        }
    }

    TEST(ProgramListCompileTest, ANestOfFoldsBeyondTheRegistersIsAnErrorAtAFold) {
        constexpr int kFits = 3;
        constexpr int kTooDeep = 8;
        const HandWorld world(2);
        {
            const String text = Nest(kFits);
            const auto compiled = CompileExpression(text, world.declaration, DescribeType<bool>());
            EXPECT_TRUE(compiled.has_value()) << text.c_str() << ": " << (compiled.has_value() ? "" : compiled.error().message.c_str());
        }
        const String text = Nest(kTooDeep);
        const ProgramError error = Rejected(text, DescribeType<bool>());
        ASSERT_GT(error.column, 0u);
        EXPECT_EQ(StringView(text).substr(error.column - 1, 5), "count") << error.message.c_str();
        EXPECT_NE(error.message.find("register"), String::npos) << error.message.c_str();
    }

    TEST(ProgramListCompileTest, ARunOfAProgramWithLoopsAllocatesNothing) {
        const HandWorld world(5);
        const ProgramId nested = Compile<int32_t>(world.declaration, "sum(hand.sets, s, count(s.tiles, t, t.terminal || t.rank > hand.tiles[1].rank))");
        const ProgramId reads = Compile<int32_t>(world.declaration, "hand.sets[1].tiles[2].rank + size(hand.tiles)");
        int64_t total = 0;
        ExpectNoAllocations([&] {
            for (int i = 0; i < 100; ++i) {
                total += Run<int32_t>(ProgramPool::Get(nested), world.context) + Run<int32_t>(ProgramPool::Get(reads), world.context);
            }
        });
        EXPECT_NE(total, 0);
    }

}  // namespace BECore::Tests
