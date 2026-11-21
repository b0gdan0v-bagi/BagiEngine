// The list and loop instructions of programs (Values/Programs/Lists/): the descriptor of a list against the containers it describes, and hand-assembled programs of the shapes the language of
// stage 2 lowers to (count, any, all, sum, min, max, a nested loop, the size and an element by index) against plain C++ loops over the same objects.

#include <BECore/ProgramListTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/Lists/ListKernels.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Generated/ProgramListTypes.gen.hpp>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace ProgramListData;

    namespace {

        constexpr LevelIndex kHandLevel = 0;
        constexpr int kVariants = 17;
        constexpr uint8_t kEnumBytes = sizeof(ProgramListSuit);
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

        template <typename Object, typename Field>
        uint32_t OffsetOf(const Object& object, const Field& field) {
            return static_cast<uint32_t>(reinterpret_cast<const std::byte*>(&field) - reinterpret_cast<const std::byte*>(&object));
        }

        /// The offsets of the fields are the same in every object of a type, so a default one tells them
        const ProgramListHand& HandProbe() {
            static const ProgramListHand probe{};
            return probe;
        }

        const ProgramListTile& TileProbe() {
            static const ProgramListTile probe{};
            return probe;
        }

        const ProgramListSet& SetProbe() {
            static const ProgramListSet probe{};
            return probe;
        }

        template <typename List>
        ListDescriptor DescriptorOf(const List& field) {
            return Require(MakeListDescriptor(OffsetOf(HandProbe(), field), DescribeType<List>()));
        }

        ListDescriptor TilesList() {
            return DescriptorOf(HandProbe().tiles);
        }

        ListDescriptor SetsList() {
            return DescriptorOf(HandProbe().sets);
        }

        ListDescriptor CountsList() {
            return DescriptorOf(HandProbe().counts);
        }

        ListDescriptor WeightsList() {
            return DescriptorOf(HandProbe().weights);
        }

        ListDescriptor FlagsList() {
            return DescriptorOf(HandProbe().flags);
        }

        ListDescriptor NoneList() {
            return DescriptorOf(HandProbe().none);
        }

        /// The tiles of a set, the list inside an element
        ListDescriptor SetTilesList() {
            return Require(MakeListDescriptor(OffsetOf(SetProbe(), SetProbe().tiles), DescribeType<Array<ProgramListTile, 4>>()));
        }

        PoolString NameOf(int index) {
            switch (index % 3) {
                case 0:
                    return "alpha"_intern;
                case 1:
                    return "beta"_intern;
                default:
                    return "gamma"_intern;
            }
        }

        /// A hand with the context that holds it: `before` and `after` are around the lists, so an offset that is off by a field reads them
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

            /// Variant 0 is an empty hand, 1 has every tile terminal, 2 to 15 one terminal tile at index variant - 2 and 16 none; the other lists change length with the variant (a `SmallVector` is
            /// inline up to four and over it from five)
            void Fill(int variant) {
                hand.before = 111;
                hand.after = 222;
                if (variant == 0) {
                    return;
                }
                const auto suits = static_cast<int>(EnumUtils<ProgramListSuit>::Count());
                for (int i = 0; i < static_cast<int>(hand.tiles.size()); ++i) {
                    ProgramListTile& tile = hand.tiles[static_cast<size_t>(i)];
                    tile.suit = static_cast<ProgramListSuit>((i + variant) % suits);
                    tile.terminal = variant == 1 || i == variant - 2;
                    tile.rank = i * 3 - 5;
                    tile.weight = static_cast<float>(i) * 0.25f - 1.5f;
                    tile.position = Vec3{static_cast<float>(i), static_cast<float>(-2 * i), 0.5f * static_cast<float>(i)};
                    tile.name = NameOf(i);
                }
                for (int s = 0; s < variant % 5; ++s) {
                    ProgramListSet set;
                    set.pung = s % 2 == 0;
                    for (int j = 0; j < static_cast<int>(set.tiles.size()); ++j) {
                        set.tiles[static_cast<size_t>(j)].rank = 10 * s + j + variant;
                    }
                    hand.sets.push_back(set);
                }
                for (int k = 0; k < variant % 7; ++k) {
                    hand.counts.push_back((k + 1) * (variant + 3) - 4);
                }
                for (int k = 0; k < std::min(variant % 9, 8); ++k) {
                    hand.weights.push_back(static_cast<float>(k) * 1.5f - 2.0f);
                }
                hand.flags = {variant % 2 == 0, variant % 3 == 0, variant == 1};
            }
        };

        /// Gives back the registers of a loop body when it ends: the register file is a stack
        class Temps {
        public:
            explicit Temps(ProgramBuilder& builder) : _builder(builder) {}
            Temps(const Temps&) = delete;
            Temps& operator=(const Temps&) = delete;
            ~Temps() {
                for (auto it = _registers.rbegin(); it != _registers.rend(); ++it) {
                    _builder.ReleaseRegister(*it);
                }
            }

            [[nodiscard]] uint8_t Get() {
                _registers.push_back(Require(_builder.AcquireRegister()));
                return _registers.back();
            }

            template <typename T>
            [[nodiscard]] uint8_t Const(T value) {
                const uint8_t reg = Get();
                _builder.EmitLoadConst(reg, value);
                return reg;
            }

            [[nodiscard]] uint8_t Field(ProgramType type, uint8_t element, uint32_t offset, uint8_t enumBytes = 0) {
                const uint8_t reg = Get();
                _builder.EmitLoadElem(reg, type, element, offset, enumBytes);
                return reg;
            }

        private:
            ProgramBuilder& _builder;
            Vector<uint8_t> _registers;
        };

        /// `acc = init; for element in list: body; return acc` over a list of the hand; @p init writes the accumulator, @p body gets the builder, the element and the accumulator
        template <typename Init, typename Body>
        Program Loop(ProgramType resultType, const ListDescriptor& list, Init&& init, Body&& body) {
            ProgramBuilder builder;
            const uint8_t listReg = Require(builder.AcquireRegister());
            const uint8_t element = Require(builder.AcquireRegister());
            const uint8_t acc = Require(builder.AcquireRegister());
            builder.EmitListSlot(listReg, kHandLevel, list);
            init(builder, acc);
            const size_t begin = builder.EmitLoopBegin(element, listReg);
            body(builder, element, acc);
            builder.EmitLoopEnd(begin);
            return Require(builder.Finish(resultType, acc));
        }

        /// A fold of one field: `acc = op(acc, field)` from @p identity; @p breakOn, when given, leaves the loop when the accumulator (a bool) is that
        template <typename T>
        Program FoldField(ProgramOperation op, const ListDescriptor& list, uint32_t offset, T identity, Optional<bool> breakOn = std::nullopt, uint8_t enumBytes = 0) {
            constexpr ProgramType kType = kProgramTypeOf<T>;
            return Loop(
                kType, list, [&](ProgramBuilder& builder, uint8_t acc) { builder.EmitLoadConst(acc, identity); },
                [&](ProgramBuilder& builder, uint8_t element, uint8_t acc) {
                    Temps temps(builder);
                    const uint8_t value = temps.Field(kType, element, offset, enumBytes);
                    builder.Emit(OpcodeOf(op, kType), acc, acc, value);
                    if (breakOn.has_value()) {
                        builder.EmitBreakIf(acc, *breakOn);
                    }
                });
        }

        /// How many elements the predicate is true for; @p predicate gets the builder, the temporaries and the element and returns a `bool` register
        template <typename Predicate>
        Program CountWhere(const ListDescriptor& list, Predicate&& predicate) {
            return Loop(
                ProgramType::Int32, list, [](ProgramBuilder& builder, uint8_t acc) { builder.EmitLoadConst(acc, int32_t{0}); },
                [&](ProgramBuilder& builder, uint8_t element, uint8_t acc) {
                    Temps temps(builder);
                    const uint8_t hit = predicate(builder, temps, element);
                    const uint8_t one = temps.Const(int32_t{1});
                    const uint8_t zero = temps.Const(int32_t{0});
                    const uint8_t chosen = temps.Get();
                    builder.Emit(OpcodeOf(ProgramOperation::Select, ProgramType::Int32), chosen, hit, one, zero);
                    builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, chosen);
                });
        }

        template <typename T>
        T RunHand(const Program& program, const HandWorld& world) {
            return Run<T>(program, world.context);
        }

        bool SameVec3(const Vec3& a, const Vec3& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        /// The number of iterations of the loop of `IterationProbe`: it stops after the first element the predicate settles
        struct Probe {
            bool all;
            int32_t threshold;
        };

        /// A loop over the tiles that counts its own iterations (the result, an `int32_t`) and computes `any(rank > threshold)` or `all(rank < threshold)`; with @p breaks it leaves the loop as soon
        /// as the answer is known, without it runs to the end. The count shows where it stopped, and the other program (@p returnAnswer) that the answer is the same
        Program IterationProbe(const Probe& probe, bool breaks, bool returnAnswer) {
            ProgramBuilder builder;
            const uint8_t listReg = Require(builder.AcquireRegister());
            const uint8_t element = Require(builder.AcquireRegister());
            const uint8_t answer = Require(builder.AcquireRegister());
            const uint8_t iterations = Require(builder.AcquireRegister());
            builder.EmitListSlot(listReg, kHandLevel, TilesList());
            builder.EmitLoadConst(answer, probe.all);
            builder.EmitLoadConst(iterations, int32_t{0});
            const size_t begin = builder.EmitLoopBegin(element, listReg);
            {
                Temps temps(builder);
                const uint8_t one = temps.Const(int32_t{1});
                builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), iterations, iterations, one);
                const uint8_t rank = temps.Field(ProgramType::Int32, element, OffsetOf(TileProbe(), TileProbe().rank));
                const uint8_t limit = temps.Const(probe.threshold);
                const uint8_t holds = temps.Get();
                builder.Emit(OpcodeOf(probe.all ? ProgramOperation::Lt : ProgramOperation::Gt, ProgramType::Int32), holds, rank, limit);
                builder.Emit(OpcodeOf(probe.all ? ProgramOperation::And : ProgramOperation::Or, ProgramType::Bool), answer, answer, holds);
                if (breaks) {
                    builder.EmitBreakIf(answer, !probe.all);
                }
            }
            builder.EmitLoopEnd(begin);
            return returnAnswer ? Require(builder.Finish(ProgramType::Bool, answer)) : Require(builder.Finish(ProgramType::Int32, iterations));
        }

        /// The error of a `Finish` that must refuse, with the test failing when it does not
        String Refusal(const ProgramBuilder& builder, ProgramType type, uint8_t result) {
            const auto program = builder.Finish(type, result);
            EXPECT_FALSE(program.has_value()) << "the program was accepted";
            return program.has_value() ? String() : program.error().message;
        }

        // An element type of every size next to the tile, for the layout tests
        struct Wide {
            double a = 0;
            double b = 0;
            double c = 0;
        };

        template <typename List>
        struct Holder {
            int32_t before = 0;
            List list;
            int32_t after = 0;
        };

        /// The kernel reads the container as `begin()` and `size()` do, and a descriptor from its type is the one for its layout
        template <typename List>
        void ExpectReadsLikeTheContainer(const Holder<List>& holder) {
            const ListDescriptor descriptor = Require(MakeListDescriptor(OffsetOf(holder, holder.list), DescribeType<List>()));
            const ListImage image = Kernel::ReadList(reinterpret_cast<const uint8_t*>(&holder), descriptor);
            EXPECT_EQ(image.count, holder.list.size());
            EXPECT_EQ(image.stride, sizeof(*holder.list.begin()));
            EXPECT_EQ(static_cast<const void*>(image.data), static_cast<const void*>(holder.list.begin()));
        }

        template <typename Element, typename List>
        void ExpectLayoutThroughItsLife(size_t capacity) {
            Holder<List> holder;
            for (const size_t count : {size_t{0}, size_t{1}, size_t{3}, size_t{4}, size_t{5}, capacity, size_t{2}, size_t{0}, size_t{7}}) {
                holder.list.resize(std::min(count, capacity));
                ExpectReadsLikeTheContainer(holder);
            }
        }

        template <typename Element>
        void ExpectEveryContainerOf() {
            Holder<Array<Element, 5>> inlineList;
            ExpectReadsLikeTheContainer(inlineList);
            ExpectLayoutThroughItsLife<Element, Vector<Element>>(40);
            ExpectLayoutThroughItsLife<Element, SmallVector<Element, 4>>(40);
            ExpectLayoutThroughItsLife<Element, FixedVector<Element, 8>>(8);
        }

    }  // namespace

    TEST(ProgramListTest, EveryContainerIsReadWhereItIsAtEveryStageOfItsLife) {
        ExpectEveryContainerOf<bool>();
        ExpectEveryContainerOf<int32_t>();
        ExpectEveryContainerOf<double>();
        ExpectEveryContainerOf<Vec3>();
        ExpectEveryContainerOf<ProgramListTile>();
        ExpectEveryContainerOf<ProgramListSuit>();
    }

    TEST(ProgramListTest, AnArrayIsInlineAndEveryOtherListIsOnPointers) {
        const ListDescriptor array = TilesList();
        EXPECT_EQ(array.layout, ListLayout::Inline);
        EXPECT_EQ(array.count, 14U);
        EXPECT_EQ(array.stride, sizeof(ProgramListTile));
        for (const ListDescriptor& list : {SetsList(), CountsList(), WeightsList(), NoneList()}) {
            EXPECT_EQ(list.layout, ListLayout::Pointers);
            EXPECT_EQ(list.count, 0U) << "the count of a list on pointers is read from the object";
        }
        EXPECT_EQ(CountsList().stride, sizeof(int32_t));
        EXPECT_EQ(FlagsList().count, 3U);
    }

    TEST(ProgramListTest, TheDescriptorRefusesWhatIsNoListAndWhatDoesNotFitThirtyTwoBits) {
        const auto notAList = MakeListDescriptor(0, DescribeType<int32_t>());
        ASSERT_FALSE(notAList.has_value());
        EXPECT_NE(notAList.error().find("list"), String::npos) << notAList.error().c_str();

        TypeDesc noSize = DescribeType<Vector<int32_t>>();
        noSize.elementSize = 0;
        const auto zero = MakeListDescriptor(0, noSize);
        ASSERT_FALSE(zero.has_value());
        EXPECT_NE(zero.error().find("no size"), String::npos) << zero.error().c_str();

        constexpr size_t kTooBig = size_t{1} << 32;
        const auto farOffset = MakeListDescriptor(kTooBig, DescribeType<Vector<int32_t>>());
        ASSERT_FALSE(farOffset.has_value());
        EXPECT_NE(farOffset.error().find("offset"), String::npos) << farOffset.error().c_str();

        TypeDesc hugeElement = DescribeType<Vector<int32_t>>();
        hugeElement.elementSize = kTooBig;
        EXPECT_FALSE(MakeListDescriptor(0, hugeElement).has_value());

        TypeDesc hugeArray = DescribeType<Array<int32_t, 4>>();
        hugeArray.capacity = kTooBig;
        EXPECT_FALSE(MakeListDescriptor(0, hugeArray).has_value());
    }

    TEST(ProgramListTest, TheDescriptorIsAConstantThatRoundTripsAndEqualDescriptorsAreEqualBytes) {
        for (const ListDescriptor& list : {TilesList(), SetsList(), CountsList(), WeightsList(), FlagsList(), NoneList(), SetTilesList()}) {
            const RegisterImage image = list.ToImage();
            EXPECT_EQ(ListDescriptor::FromImage(image), list);
            const RegisterImage again = list.ToImage();
            EXPECT_EQ(std::memcmp(image.bytes.data(), again.bytes.data(), image.bytes.size()), 0);
        }
        ListDescriptor extreme;
        extreme.offset = std::numeric_limits<uint32_t>::max();
        extreme.stride = std::numeric_limits<uint32_t>::max();
        extreme.count = std::numeric_limits<uint32_t>::max();
        extreme.layout = ListLayout::Pointers;
        EXPECT_EQ(ListDescriptor::FromImage(extreme.ToImage()), extreme);
        EXPECT_NE(std::memcmp(TilesList().ToImage().bytes.data(), SetsList().ToImage().bytes.data(), sizeof(RegisterImage::bytes)), 0);
    }

    TEST(ProgramListTest, CountOfABoolFieldEqualsTheCppLoop) {
        const Program program =
            CountWhere(TilesList(), [](ProgramBuilder&, Temps& temps, uint8_t element) { return temps.Field(ProgramType::Bool, element, OffsetOf(TileProbe(), TileProbe().terminal)); });
        for (int variant = 0; variant < kVariants; ++variant) {
            const HandWorld world(variant);
            int32_t expected = 0;
            for (const ProgramListTile& tile : world.hand.tiles) {
                expected += tile.terminal ? 1 : 0;
            }
            EXPECT_EQ(RunHand<int32_t>(program, world), expected) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, CountOfAnEnumFieldAndOfATextFieldEqualsTheCppLoop) {
        const Program winds = CountWhere(TilesList(), [](ProgramBuilder& builder, Temps& temps, uint8_t element) {
            const uint8_t suit = temps.Field(ProgramType::Enum, element, OffsetOf(TileProbe(), TileProbe().suit), kEnumBytes);
            const uint8_t wanted = temps.Const(ProgramListSuit::Winds);
            const uint8_t hit = temps.Get();
            builder.Emit(OpcodeOf(ProgramOperation::Eq, ProgramType::Enum), hit, suit, wanted);
            return hit;
        });
        const Program beta = CountWhere(TilesList(), [](ProgramBuilder& builder, Temps& temps, uint8_t element) {
            const uint8_t name = temps.Field(ProgramType::String, element, OffsetOf(TileProbe(), TileProbe().name));
            const uint8_t wanted = temps.Const(PoolString("beta"_intern));
            const uint8_t hit = temps.Get();
            builder.Emit(OpcodeOf(ProgramOperation::Eq, ProgramType::String), hit, name, wanted);
            return hit;
        });
        for (int variant = 0; variant < kVariants; ++variant) {
            const HandWorld world(variant);
            int32_t expectedWinds = 0;
            int32_t expectedBeta = 0;
            for (const ProgramListTile& tile : world.hand.tiles) {
                expectedWinds += tile.suit == ProgramListSuit::Winds ? 1 : 0;
                expectedBeta += tile.name == PoolString("beta"_intern) ? 1 : 0;
            }
            EXPECT_EQ(RunHand<int32_t>(winds, world), expectedWinds) << "variant " << variant;
            EXPECT_EQ(RunHand<int32_t>(beta, world), expectedBeta) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, SumsOfEveryFieldKindOverEveryContainerEqualTheCppLoop) {
        const Program ranks = FoldField(ProgramOperation::Add, TilesList(), OffsetOf(TileProbe(), TileProbe().rank), int32_t{0});
        const Program weights = FoldField(ProgramOperation::Add, TilesList(), OffsetOf(TileProbe(), TileProbe().weight), 0.0f);
        const Program positions = FoldField(ProgramOperation::Add, TilesList(), OffsetOf(TileProbe(), TileProbe().position), Vec3{});
        const Program counts = FoldField(ProgramOperation::Add, CountsList(), 0, int32_t{0});
        const Program fixedWeights = FoldField(ProgramOperation::Add, WeightsList(), 0, 0.0f);
        const Program sets = CountWhere(SetsList(), [](ProgramBuilder&, Temps& temps, uint8_t element) { return temps.Field(ProgramType::Bool, element, OffsetOf(SetProbe(), SetProbe().pung)); });
        for (int variant = 0; variant < kVariants; ++variant) {
            const HandWorld world(variant);
            int32_t expectedRanks = 0;
            float expectedWeights = 0.0f;
            Vec3 expectedPositions{};
            for (const ProgramListTile& tile : world.hand.tiles) {
                expectedRanks += tile.rank;
                expectedWeights += tile.weight;
                expectedPositions = expectedPositions + tile.position;
            }
            int32_t expectedCounts = 0;
            for (const int32_t value : world.hand.counts) {
                expectedCounts += value;
            }
            float expectedFixed = 0.0f;
            for (const float value : world.hand.weights) {
                expectedFixed += value;
            }
            int32_t expectedPungs = 0;
            for (const ProgramListSet& set : world.hand.sets) {
                expectedPungs += set.pung ? 1 : 0;
            }
            EXPECT_EQ(RunHand<int32_t>(ranks, world), expectedRanks) << "variant " << variant;
            EXPECT_EQ(RunHand<float>(weights, world), expectedWeights) << "variant " << variant;
            EXPECT_TRUE(SameVec3(RunHand<Vec3>(positions, world), expectedPositions)) << "variant " << variant;
            EXPECT_EQ(RunHand<int32_t>(counts, world), expectedCounts) << "variant " << variant << ", " << world.hand.counts.size() << " counts";
            EXPECT_EQ(RunHand<float>(fixedWeights, world), expectedFixed) << "variant " << variant;
            EXPECT_EQ(RunHand<int32_t>(sets, world), expectedPungs) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, MinAndMaxStartFromTheIdentityOfTheFoldAndSkipNan) {
        constexpr float kInf = std::numeric_limits<float>::infinity();
        const Program lowest = FoldField(ProgramOperation::Min, WeightsList(), 0, kInf);
        const Program highest = FoldField(ProgramOperation::Max, WeightsList(), 0, -kInf);
        const Program lowestRank = FoldField(ProgramOperation::Min, TilesList(), OffsetOf(TileProbe(), TileProbe().rank), std::numeric_limits<int32_t>::max());
        const Program highestRank = FoldField(ProgramOperation::Max, TilesList(), OffsetOf(TileProbe(), TileProbe().rank), std::numeric_limits<int32_t>::lowest());
        for (int variant = 0; variant < kVariants; ++variant) {
            HandWorld world(variant);
            if (variant % 4 == 3 && !world.hand.weights.empty()) {
                world.hand.weights[world.hand.weights.size() / 2] = kNan;
            }
            float expectedLow = kInf;
            float expectedHigh = -kInf;
            for (const float value : world.hand.weights) {
                if (value == value) {
                    expectedLow = std::min(expectedLow, value);
                    expectedHigh = std::max(expectedHigh, value);
                }
            }
            EXPECT_EQ(RunHand<float>(lowest, world), expectedLow) << "variant " << variant;
            EXPECT_EQ(RunHand<float>(highest, world), expectedHigh) << "variant " << variant;
            int32_t expectedLowRank = std::numeric_limits<int32_t>::max();
            int32_t expectedHighRank = std::numeric_limits<int32_t>::lowest();
            for (const ProgramListTile& tile : world.hand.tiles) {
                expectedLowRank = std::min(expectedLowRank, tile.rank);
                expectedHighRank = std::max(expectedHighRank, tile.rank);
            }
            EXPECT_EQ(RunHand<int32_t>(lowestRank, world), expectedLowRank) << "variant " << variant;
            EXPECT_EQ(RunHand<int32_t>(highestRank, world), expectedHighRank) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, AnyAndAllOfAScalarListEqualTheCppLoop) {
        const Program any = FoldField(ProgramOperation::Or, FlagsList(), 0, false);
        const Program all = FoldField(ProgramOperation::And, FlagsList(), 0, true);
        for (int variant = 0; variant < kVariants; ++variant) {
            const HandWorld world(variant);
            bool expectedAny = false;
            bool expectedAll = true;
            for (const bool flag : world.hand.flags) {
                expectedAny = expectedAny || flag;
                expectedAll = expectedAll && flag;
            }
            EXPECT_EQ(RunHand<bool>(any, world), expectedAny) << "variant " << variant;
            EXPECT_EQ(RunHand<bool>(all, world), expectedAll) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, TheEarlyExitGivesTheAnswerOfTheFullLoopAndStopsAtTheFirstHit) {
        for (const bool all : {false, true}) {
            for (const int32_t threshold : {-100, -5, -4, 0, 10, 20, 34, 35, 100}) {
                const Probe probe{all, threshold};
                const Program stops = IterationProbe(probe, true, false);
                const Program runsOn = IterationProbe(probe, false, false);
                const Program stopsAnswer = IterationProbe(probe, true, true);
                const Program runsOnAnswer = IterationProbe(probe, false, true);
                for (int variant = 0; variant < kVariants; ++variant) {
                    const HandWorld world(variant);
                    const auto settles = [&](const ProgramListTile& tile) { return all ? tile.rank >= threshold : tile.rank > threshold; };
                    int32_t expectedStop = static_cast<int32_t>(world.hand.tiles.size());
                    bool answer = all;
                    for (size_t i = 0; i < world.hand.tiles.size(); ++i) {
                        if (settles(world.hand.tiles[i])) {
                            answer = !all;
                            expectedStop = static_cast<int32_t>(i) + 1;
                            break;
                        }
                    }
                    const String where = Format("{} {} variant {}", all ? "all <" : "any >", threshold, variant);
                    EXPECT_EQ(RunHand<int32_t>(stops, world), expectedStop) << where.c_str();
                    EXPECT_EQ(RunHand<int32_t>(runsOn, world), static_cast<int32_t>(world.hand.tiles.size())) << where.c_str();
                    EXPECT_EQ(RunHand<bool>(stopsAnswer, world), answer) << where.c_str();
                    EXPECT_EQ(RunHand<bool>(runsOnAnswer, world), answer) << where.c_str();
                }
            }
        }
    }

    TEST(ProgramListTest, OverAnEmptyListCountSumAndAnyAreTheIdentityOfTheirFold) {
        const ListDescriptor none = NoneList();
        const Program count = CountWhere(none, [](ProgramBuilder&, Temps& temps, uint8_t element) { return temps.Field(ProgramType::Bool, element, OffsetOf(TileProbe(), TileProbe().terminal)); });
        const Program sum = FoldField(ProgramOperation::Add, none, OffsetOf(TileProbe(), TileProbe().rank), int32_t{0});
        const Program any = FoldField(ProgramOperation::Or, none, OffsetOf(TileProbe(), TileProbe().terminal), false, true);
        const Program all = FoldField(ProgramOperation::And, none, OffsetOf(TileProbe(), TileProbe().terminal), true, false);
        const Program lowest = FoldField(ProgramOperation::Min, none, OffsetOf(TileProbe(), TileProbe().weight), std::numeric_limits<float>::infinity());
        const Program highest = FoldField(ProgramOperation::Max, none, OffsetOf(TileProbe(), TileProbe().weight), -std::numeric_limits<float>::infinity());
        const HandWorld world(5);
        EXPECT_EQ(RunHand<int32_t>(count, world), 0);
        EXPECT_EQ(RunHand<int32_t>(sum, world), 0);
        EXPECT_FALSE(RunHand<bool>(any, world));
        EXPECT_TRUE(RunHand<bool>(all, world));
        EXPECT_EQ(RunHand<float>(lowest, world), std::numeric_limits<float>::infinity());
        EXPECT_EQ(RunHand<float>(highest, world), -std::numeric_limits<float>::infinity());
        const HandWorld empty(0);
        EXPECT_EQ(RunHand<int32_t>(FoldField(ProgramOperation::Add, SetsList(), 0, int32_t{0}), empty), 0) << "a loop over a list that is empty on pointers";
    }

    TEST(ProgramListTest, ANestedLoopOverTheTilesOfEverySetEqualsTheCppLoop) {
        ProgramBuilder builder;
        const uint8_t outerList = Require(builder.AcquireRegister());
        const uint8_t set = Require(builder.AcquireRegister());
        const uint8_t acc = Require(builder.AcquireRegister());
        const uint8_t innerList = Require(builder.AcquireRegister());
        const uint8_t tile = Require(builder.AcquireRegister());
        builder.EmitListSlot(outerList, kHandLevel, SetsList());
        builder.EmitLoadConst(acc, int32_t{0});
        const size_t outer = builder.EmitLoopBegin(set, outerList);
        builder.EmitListElem(innerList, set, SetTilesList());
        const size_t inner = builder.EmitLoopBegin(tile, innerList);
        {
            Temps temps(builder);
            const uint8_t rank = temps.Field(ProgramType::Int32, tile, OffsetOf(TileProbe(), TileProbe().rank));
            builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, rank);
        }
        builder.EmitLoopEnd(inner);
        builder.EmitLoopEnd(outer);
        const Program program = Require(builder.Finish(ProgramType::Int32, acc));
        for (int variant = 0; variant < kVariants; ++variant) {
            const HandWorld world(variant);
            int32_t expected = 0;
            for (const ProgramListSet& each : world.hand.sets) {
                for (const ProgramListTile& each2 : each.tiles) {
                    expected += each2.rank;
                }
            }
            EXPECT_EQ(RunHand<int32_t>(program, world), expected) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, ABreakLeavesOnlyTheInnerLoop) {
        // any set has a tile of rank above 12: the inner loop stops at the first, the outer goes on; the sum of the sets that have one is compared
        ProgramBuilder builder;
        const uint8_t outerList = Require(builder.AcquireRegister());
        const uint8_t set = Require(builder.AcquireRegister());
        const uint8_t acc = Require(builder.AcquireRegister());
        const uint8_t found = Require(builder.AcquireRegister());
        const uint8_t innerList = Require(builder.AcquireRegister());
        const uint8_t tile = Require(builder.AcquireRegister());
        builder.EmitListSlot(outerList, kHandLevel, SetsList());
        builder.EmitLoadConst(acc, int32_t{0});
        const size_t outer = builder.EmitLoopBegin(set, outerList);
        builder.EmitLoadConst(found, false);
        builder.EmitListElem(innerList, set, SetTilesList());
        const size_t inner = builder.EmitLoopBegin(tile, innerList);
        {
            Temps temps(builder);
            const uint8_t rank = temps.Field(ProgramType::Int32, tile, OffsetOf(TileProbe(), TileProbe().rank));
            const uint8_t limit = temps.Const(int32_t{12});
            const uint8_t above = temps.Get();
            builder.Emit(OpcodeOf(ProgramOperation::Gt, ProgramType::Int32), above, rank, limit);
            builder.Emit(OpcodeOf(ProgramOperation::Or, ProgramType::Bool), found, found, above);
            builder.EmitBreakIf(found, true);
        }
        builder.EmitLoopEnd(inner);
        {
            Temps temps(builder);
            const uint8_t one = temps.Const(int32_t{1});
            const uint8_t zero = temps.Const(int32_t{0});
            const uint8_t chosen = temps.Get();
            builder.Emit(OpcodeOf(ProgramOperation::Select, ProgramType::Int32), chosen, found, one, zero);
            builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, chosen);
        }
        builder.EmitLoopEnd(outer);
        const Program program = Require(builder.Finish(ProgramType::Int32, acc));
        for (int variant = 0; variant < kVariants; ++variant) {
            const HandWorld world(variant);
            int32_t expected = 0;
            for (const ProgramListSet& each : world.hand.sets) {
                bool hasOne = false;
                for (const ProgramListTile& each2 : each.tiles) {
                    hasOne = hasOne || each2.rank > 12;
                }
                expected += hasOne ? 1 : 0;
            }
            EXPECT_EQ(RunHand<int32_t>(program, world), expected) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, TheSizeOfEveryListIsItsCountAsAnInt32) {
        const ListDescriptor lists[] = {TilesList(), SetsList(), CountsList(), WeightsList(), FlagsList(), NoneList()};
        for (const ListDescriptor& list : lists) {
            ProgramBuilder builder;
            const uint8_t listReg = Require(builder.AcquireRegister());
            const uint8_t dst = Require(builder.AcquireRegister());
            builder.EmitListSlot(listReg, kHandLevel, list);
            builder.EmitListSize(dst, listReg);
            const Program program = Require(builder.Finish(ProgramType::Int32, dst));
            for (int variant = 0; variant < kVariants; ++variant) {
                const HandWorld world(variant);
                const Array<int32_t, 6> expected{static_cast<int32_t>(world.hand.tiles.size()),   static_cast<int32_t>(world.hand.sets.size()),  static_cast<int32_t>(world.hand.counts.size()),
                                                 static_cast<int32_t>(world.hand.weights.size()), static_cast<int32_t>(world.hand.flags.size()), static_cast<int32_t>(world.hand.none.size())};
                const size_t which = static_cast<size_t>(&list - lists);
                EXPECT_EQ(RunHand<int32_t>(program, world), expected[which]) << "list " << which << " variant " << variant;
            }
        }
    }

    TEST(ProgramListTest, AnElementByIndexIsTheElementInRangeAndTheDefaultOutsideIt) {
        constexpr int32_t kIndexes[] = {0, 1, 7, 13, 14, 15, 100, -1, -14, std::numeric_limits<int32_t>::lowest(), std::numeric_limits<int32_t>::max()};
        for (const int32_t index : kIndexes) {
            ProgramBuilder builder;
            const uint8_t listReg = Require(builder.AcquireRegister());
            const uint8_t where = Require(builder.AcquireRegister());
            const uint8_t element = Require(builder.AcquireRegister());
            const uint8_t rank = Require(builder.AcquireRegister());
            const uint8_t terminal = Require(builder.AcquireRegister());
            const uint8_t suit = Require(builder.AcquireRegister());
            const uint8_t position = Require(builder.AcquireRegister());
            builder.EmitListSlot(listReg, kHandLevel, TilesList());
            builder.EmitLoadConst(where, index);
            builder.EmitListAt(element, listReg, where);
            builder.EmitLoadElem(rank, ProgramType::Int32, element, OffsetOf(TileProbe(), TileProbe().rank));
            builder.EmitLoadElem(terminal, ProgramType::Bool, element, OffsetOf(TileProbe(), TileProbe().terminal));
            builder.EmitLoadElem(suit, ProgramType::Enum, element, OffsetOf(TileProbe(), TileProbe().suit), kEnumBytes);
            builder.EmitLoadElem(position, ProgramType::Vec3, element, OffsetOf(TileProbe(), TileProbe().position));
            const Program rankOf = Require(builder.Finish(ProgramType::Int32, rank));
            const Program terminalOf = Require(builder.Finish(ProgramType::Bool, terminal));
            const Program suitOf = Require(builder.Finish(ProgramType::Enum, suit));
            const Program positionOf = Require(builder.Finish(ProgramType::Vec3, position));
            for (int variant = 0; variant < kVariants; variant += 3) {
                const HandWorld world(variant);
                const bool inside = index >= 0 && index < static_cast<int32_t>(world.hand.tiles.size());
                const ProgramListTile expected = inside ? world.hand.tiles[static_cast<size_t>(index)] : ProgramListTile{};
                const String where2 = Format("index {} variant {}", index, variant);
                EXPECT_EQ(RunHand<int32_t>(rankOf, world), expected.rank) << where2.c_str();
                EXPECT_EQ(RunHand<bool>(terminalOf, world), expected.terminal) << where2.c_str();
                EXPECT_EQ(RunHand<ProgramListSuit>(suitOf, world), expected.suit) << where2.c_str();
                EXPECT_TRUE(SameVec3(RunHand<Vec3>(positionOf, world), expected.position)) << where2.c_str();
            }
        }
    }

    TEST(ProgramListTest, AnElementOfAScalarListIsReadAtOffsetZeroAndAnEmptyListHasNone) {
        const auto first = [](const ListDescriptor& list) {
            ProgramBuilder builder;
            const uint8_t listReg = Require(builder.AcquireRegister());
            const uint8_t where = Require(builder.AcquireRegister());
            const uint8_t element = Require(builder.AcquireRegister());
            const uint8_t value = Require(builder.AcquireRegister());
            builder.EmitListSlot(listReg, kHandLevel, list);
            builder.EmitLoadConst(where, int32_t{0});
            builder.EmitListAt(element, listReg, where);
            builder.EmitLoadElem(value, ProgramType::Int32, element, 0);
            return Require(builder.Finish(ProgramType::Int32, value));
        };
        const Program ofCounts = first(CountsList());
        const Program ofNone = first(NoneList());
        for (int variant = 0; variant < kVariants; ++variant) {
            const HandWorld world(variant);
            EXPECT_EQ(RunHand<int32_t>(ofCounts, world), world.hand.counts.empty() ? 0 : world.hand.counts[0]) << "variant " << variant;
            EXPECT_EQ(RunHand<int32_t>(ofNone, world), 0) << "variant " << variant;
        }
    }

    TEST(ProgramListTest, TheListOfAMissingElementIsEmptyAndANestedLoopOverItDoesNotRun) {
        // sets[index].tiles summed: an index outside the sets has no element, so its list is empty and the sum is 0
        for (const int32_t index : {0, 1, 3, 4, 9, -1}) {
            ProgramBuilder builder;
            const uint8_t setsReg = Require(builder.AcquireRegister());
            const uint8_t where = Require(builder.AcquireRegister());
            const uint8_t set = Require(builder.AcquireRegister());
            const uint8_t acc = Require(builder.AcquireRegister());
            const uint8_t tilesReg = Require(builder.AcquireRegister());
            const uint8_t tile = Require(builder.AcquireRegister());
            builder.EmitListSlot(setsReg, kHandLevel, SetsList());
            builder.EmitLoadConst(where, index);
            builder.EmitListAt(set, setsReg, where);
            builder.EmitLoadConst(acc, int32_t{0});
            builder.EmitListElem(tilesReg, set, SetTilesList());
            const size_t loop = builder.EmitLoopBegin(tile, tilesReg);
            {
                Temps temps(builder);
                const uint8_t rank = temps.Field(ProgramType::Int32, tile, OffsetOf(TileProbe(), TileProbe().rank));
                builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, rank);
            }
            builder.EmitLoopEnd(loop);
            const Program program = Require(builder.Finish(ProgramType::Int32, acc));
            for (int variant = 0; variant < kVariants; ++variant) {
                const HandWorld world(variant);
                int32_t expected = 0;
                if (index >= 0 && index < static_cast<int32_t>(world.hand.sets.size())) {
                    for (const ProgramListTile& each : world.hand.sets[static_cast<size_t>(index)].tiles) {
                        expected += each.rank;
                    }
                }
                EXPECT_EQ(RunHand<int32_t>(program, world), expected) << "set " << index << " variant " << variant;
            }
        }
    }

    TEST(ProgramListTest, ARunOfALoopProgramAllocatesNothing) {
        const Program nested = FoldField(ProgramOperation::Add, TilesList(), OffsetOf(TileProbe(), TileProbe().weight), 0.0f);
        const HandWorld world(9);
        float total = 0.0f;
        ExpectNoAllocations([&] {
            for (int i = 0; i < 100; ++i) {
                total += RunHand<float>(nested, world);
            }
        });
        EXPECT_NE(total, 0.0f);
    }

    TEST(ProgramListTest, FinishRefusesAJumpOutOfTheCode) {
        ProgramBuilder builder;
        const uint8_t list = Require(builder.AcquireRegister());
        const uint8_t element = Require(builder.AcquireRegister());
        builder.EmitListSlot(list, kHandLevel, TilesList());
        builder.Emit(Opcode::LoopBegin, element, list, 0, 0, 99);
        const String message = Refusal(builder, ProgramType::Int32, element);
        EXPECT_NE(message.find("instruction 1 (LoopBegin)"), String::npos) << message.c_str();
    }

    TEST(ProgramListTest, FinishRefusesALoopBeginWithoutItsLoopEnd) {
        ProgramBuilder builder;
        const uint8_t list = Require(builder.AcquireRegister());
        const uint8_t element = Require(builder.AcquireRegister());
        builder.EmitListSlot(list, kHandLevel, TilesList());
        static_cast<void>(builder.EmitLoopBegin(element, list));
        EXPECT_NE(Refusal(builder, ProgramType::Int32, element).find("LoopBegin"), String::npos) << "never closed: the jump was never patched";

        ProgramBuilder other;
        const uint8_t list2 = Require(other.AcquireRegister());
        const uint8_t element2 = Require(other.AcquireRegister());
        other.EmitListSlot(list2, kHandLevel, TilesList());
        other.Emit(Opcode::LoopBegin, element2, list2, 0, 0, 3);
        other.EmitLoadConst(Require(other.AcquireRegister()), int32_t{1});
        other.EmitLoadConst(Require(other.AcquireRegister()), int32_t{1});
        const String message = Refusal(other, ProgramType::Int32, 2);
        EXPECT_NE(message.find("not the LoopEnd"), String::npos) << message.c_str();
    }

    TEST(ProgramListTest, FinishRefusesALoopEndThatClosesNoLoop) {
        ProgramBuilder builder;
        const uint8_t list = Require(builder.AcquireRegister());
        const uint8_t element = Require(builder.AcquireRegister());
        builder.EmitListSlot(list, kHandLevel, TilesList());
        builder.Emit(Opcode::LoopEnd, element, list, 0, 0, 1);
        const String message = Refusal(builder, ProgramType::Int32, element);
        EXPECT_NE(message.find("closes no loop"), String::npos) << message.c_str();
    }

    TEST(ProgramListTest, FinishRefusesLoopsThatCross) {
        ProgramBuilder builder;
        const uint8_t list = Require(builder.AcquireRegister());
        const uint8_t first = Require(builder.AcquireRegister());
        const uint8_t second = Require(builder.AcquireRegister());
        builder.EmitListSlot(list, kHandLevel, TilesList());
        builder.Emit(Opcode::LoopBegin, first, list, 0, 0, 4);
        builder.Emit(Opcode::LoopBegin, second, list, 0, 0, 5);
        builder.Emit(Opcode::LoopEnd, first, list, 0, 0, 2);
        builder.Emit(Opcode::LoopEnd, second, list, 0, 0, 3);
        const String message = Refusal(builder, ProgramType::Int32, first);
        EXPECT_NE(message.find("loops must nest"), String::npos) << message.c_str();
    }

    TEST(ProgramListTest, FinishRefusesABreakOutsideALoopAndOneToTheWrongTarget) {
        ProgramBuilder outside;
        const uint8_t flag = Require(outside.AcquireRegister());
        outside.EmitLoadConst(flag, true);
        outside.Emit(Opcode::BreakIf, 0, flag, 1, 0, 0);
        EXPECT_NE(Refusal(outside, ProgramType::Bool, flag).find("outside"), String::npos);

        ProgramBuilder wrong;
        const uint8_t list = Require(wrong.AcquireRegister());
        const uint8_t element = Require(wrong.AcquireRegister());
        const uint8_t answer = Require(wrong.AcquireRegister());
        wrong.EmitListSlot(list, kHandLevel, TilesList());
        wrong.EmitLoadConst(answer, false);
        const size_t begin = wrong.EmitLoopBegin(element, list);
        wrong.Emit(Opcode::BreakIf, 0, answer, 1, 0, 3);
        wrong.EmitLoopEnd(begin);
        const String message = Refusal(wrong, ProgramType::Bool, answer);
        EXPECT_NE(message.find("BreakIf"), String::npos) << message.c_str();
        EXPECT_NE(message.find("ends before"), String::npos) << message.c_str();
    }

    TEST(ProgramListTest, FinishRefusesABodyThatWritesTheElementOrTheListOfItsLoop) {
        for (const bool writeElement : {true, false}) {
            ProgramBuilder builder;
            const uint8_t list = Require(builder.AcquireRegister());
            const uint8_t element = Require(builder.AcquireRegister());
            builder.EmitListSlot(list, kHandLevel, TilesList());
            const size_t begin = builder.EmitLoopBegin(element, list);
            builder.EmitLoadConst(writeElement ? element : list, int32_t{1});
            builder.EmitLoopEnd(begin);
            const String message = Refusal(builder, ProgramType::Int32, element);
            EXPECT_NE(message.find(writeElement ? "element register" : "list register"), String::npos) << message.c_str();
        }
    }

    TEST(ProgramListTest, FinishRefusesADescriptorThatIsMissingOrIsNoDescriptor) {
        ProgramBuilder missing;
        const uint8_t list = Require(missing.AcquireRegister());
        missing.Emit(Opcode::ListSlot, list, kHandLevel, 0, 0, 77);
        EXPECT_NE(Refusal(missing, ProgramType::Int32, list).find("constant 77"), String::npos);

        ListDescriptor badLayout = TilesList();
        badLayout.layout = static_cast<ListLayout>(EnumUtils<ListLayout>::Count());
        ProgramBuilder layout;
        const uint8_t list2 = Require(layout.AcquireRegister());
        layout.Emit(Opcode::ListSlot, list2, kHandLevel, 0, 0, layout.AddConstant(badLayout.ToImage()));
        EXPECT_NE(Refusal(layout, ProgramType::Int32, list2).find("layout"), String::npos);

        ListDescriptor noStride = TilesList();
        noStride.stride = 0;
        ProgramBuilder stride;
        const uint8_t list3 = Require(stride.AcquireRegister());
        stride.Emit(Opcode::ListElem, list3, list3, 0, 0, stride.AddConstant(noStride.ToImage()));
        EXPECT_NE(Refusal(stride, ProgramType::Int32, list3).find("stride"), String::npos);
    }

    TEST(ProgramListTest, FinishRefusesARegisterThatWasNeverAcquiredAndALevelThatIsNoLevel) {
        ProgramBuilder registers;
        const uint8_t dst = Require(registers.AcquireRegister());
        registers.Emit(Opcode::ListSize, dst, 30);
        EXPECT_NE(Refusal(registers, ProgramType::Int32, dst).find("r30"), String::npos);

        ProgramBuilder element;
        const uint8_t dst2 = Require(element.AcquireRegister());
        element.Emit(OpcodeOf(ProgramOperation::LoadElem, ProgramType::Int32), dst2, 30, 0, 0, 0);
        EXPECT_NE(Refusal(element, ProgramType::Int32, dst2).find("r30"), String::npos);

        ProgramBuilder level;
        const uint8_t list = Require(level.AcquireRegister());
        level.Emit(Opcode::ListSlot, list, static_cast<uint8_t>(kMaxContextLevels), 0, 0, level.AddConstant(TilesList().ToImage()));
        EXPECT_NE(Refusal(level, ProgramType::Int32, list).find("level"), String::npos);

        ProgramBuilder enumBytes;
        const uint8_t dst3 = Require(enumBytes.AcquireRegister());
        const uint8_t list3 = Require(enumBytes.AcquireRegister());
        enumBytes.EmitListSlot(list3, kHandLevel, TilesList());
        enumBytes.Emit(Opcode::LoadElemEnum, dst3, list3, 3, 0, 0);
        EXPECT_FALSE(enumBytes.Finish(ProgramType::Enum, dst3).has_value()) << "an enum field of three bytes";
    }

    TEST(ProgramListTest, AProgramReadsTheLevelOfItsListAndDisassemblesEveryNewInstruction) {
        const Program program = Loop(
            ProgramType::Int32, TilesList(), [](ProgramBuilder& builder, uint8_t acc) { builder.EmitLoadConst(acc, int32_t{0}); },
            [](ProgramBuilder& builder, uint8_t element, uint8_t acc) {
                Temps temps(builder);
                const uint8_t rank = temps.Field(ProgramType::Int32, element, OffsetOf(TileProbe(), TileProbe().rank));
                builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, rank);
                const uint8_t flag = temps.Const(true);
                builder.EmitBreakIf(flag, true);
            });
        EXPECT_EQ(program.Reads(), LevelBit(kHandLevel));
        const String text = program.Disassemble();
        for (const char* name : {"ListSlot", "LoopBegin", "LoadElemInt32", "LoopEnd", "BreakIf"}) {
            EXPECT_NE(text.find(name), String::npos) << name << "\n" << text.c_str();
        }

        ProgramBuilder builder;
        const uint8_t list = Require(builder.AcquireRegister());
        const uint8_t where = Require(builder.AcquireRegister());
        const uint8_t element = Require(builder.AcquireRegister());
        const uint8_t size = Require(builder.AcquireRegister());
        const uint8_t inner = Require(builder.AcquireRegister());
        builder.EmitListSlot(list, 2, SetsList());
        builder.EmitLoadConst(where, int32_t{0});
        builder.EmitListAt(element, list, where);
        builder.EmitListElem(inner, element, SetTilesList());
        builder.EmitListSize(size, inner);
        const Program sizes = Require(builder.Finish(ProgramType::Int32, size));
        EXPECT_EQ(sizes.Reads(), LevelBit(2));
        const String sizeText = sizes.Disassemble();
        for (const char* name : {"ListAt", "ListElem", "ListSize"}) {
            EXPECT_NE(sizeText.find(name), String::npos) << name << "\n" << sizeText.c_str();
        }
    }

    TEST(ProgramListTest, TwoProgramsOfTheSameCodeAreOneProgramInThePool) {
        const auto make = [](const ListDescriptor& list) { return FoldField(ProgramOperation::Add, list, 0, int32_t{0}); };
        const ProgramId first = Require(ProgramPool::Intern(make(CountsList())));
        const ProgramId again = Require(ProgramPool::Intern(make(CountsList())));
        const ProgramId other = Require(ProgramPool::Intern(make(SetsList())));
        EXPECT_EQ(first, again);
        EXPECT_NE(first, other) << "another list is another constant, so another program";
    }

    TEST(ProgramListTest, FusionLeavesALoopProgramAsItIs) {
        const auto build = [](bool fuse) {
            ProgramBuilder builder;
            const uint8_t list = Require(builder.AcquireRegister());
            const uint8_t element = Require(builder.AcquireRegister());
            const uint8_t acc = Require(builder.AcquireRegister());
            builder.EmitListSlot(list, kHandLevel, TilesList());
            builder.EmitLoadConst(acc, 0.0f);
            const size_t begin = builder.EmitLoopBegin(element, list);
            {
                Temps temps(builder);
                const uint8_t weight = temps.Field(ProgramType::Float, element, OffsetOf(TileProbe(), TileProbe().weight));
                const uint8_t factor = temps.Const(2.0f);
                const uint8_t scaled = temps.Get();
                builder.Emit(OpcodeOf(ProgramOperation::Mul, ProgramType::Float), scaled, weight, factor);
                builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Float), acc, acc, scaled);
            }
            builder.EmitLoopEnd(begin);
            if (fuse) {
                builder.Fuse(acc);
            }
            return Require(builder.Finish(ProgramType::Float, acc));
        };
        const Program plain = build(false);
        const Program fused = build(true);
        EXPECT_EQ(plain.Code().size(), fused.Code().size());
        EXPECT_EQ(plain.Disassemble(), fused.Disassemble());
    }

}  // namespace BECore::Tests
