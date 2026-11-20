// The steps of a script and `RunEffects` (Values/Effects/EffectRunner.h), assembled by hand: what a store writes for each field type and width, the jumps, the commands and
// what `Finish` refuses. The text of a script is another test.

#include <BECore/EffectsToys.h>
#include <BECore/ProgramDomainTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Effects/EffectProgramBuilder.h>
#include <BECore/Values/Effects/EffectRunner.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <atomic>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;
    using ProgramDomainData::DomainBits256;
    using ProgramDomainData::DomainBoard;
    using ProgramDomainData::DomainDirection;
    using ProgramDomainData::DomainPair;

    namespace {

        constexpr LevelIndex kCasterLevel = 0;
        constexpr LevelIndex kCastLevel = 1;
        constexpr LevelIndex kBoardLevel = 2;
        constexpr LevelIndex kWidthsLevel = 3;
        constexpr size_t kRoomyBuffer = 16;

        /// The objects of a run: the caster, the board and the enum widths are writable, the cast is not
        struct World {
            ContextDecl declaration;
            ValueUnitStats caster;
            ValueCastInfo cast;
            DomainBoard board;
            ValueEnumWidths widths;
            uint64_t casterVersion = 0;
            Context context{declaration};
            EmitBufferStorage<kRoomyBuffer> storage;

            World() {
                (void)EffectsData::RegisterEffectToys();
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
                EXPECT_TRUE(declaration.Add<ValueEnumWidths>("widths").has_value());
                context.SetMutable(kCasterLevel, caster, &casterVersion);
                context.Set(kCastLevel, cast);
                context.SetMutable(kBoardLevel, board);
                context.SetMutable(kWidthsLevel, widths);
            }

            World(const World&) = delete;
            World& operator=(const World&) = delete;

            [[nodiscard]] SlotRef SlotOf(StringView path) const {
                return Require(LinkField(declaration, path)).slot;
            }

            [[nodiscard]] EffectOperand Formula(StringView text, const TypeDesc& expected, uint8_t bytes = 0) const {
                return EffectOperand::Of(Require(CompileExpression(text, declaration, expected)), bytes);
            }

            EmitBuffer& Buffer() {
                return storage.Buffer();
            }
        };

        template <typename T>
        RegisterImage ImageOf(const T& value) {
            RegisterImage image{};
            std::memcpy(image.bytes.data(), &value, sizeof(T));
            return image;
        }

        RegisterImage EnumImage(uint32_t index) {
            return ImageOf(ProgramEnum{index});
        }

        EffectOperand BoolConstant(bool value) {
            return EffectOperand::OfConstant(ProgramType::Bool, ImageOf(value));
        }

        template <typename T>
        EffectOperand ConstantOf(ProgramType type, const T& value) {
            return EffectOperand::OfConstant(type, ImageOf(value));
        }

        /// The script, or the refusal as a failure and an empty script, so that a case goes on without a null pointer
        IntrusivePtr<const EffectProgram> Build(const EffectProgramBuilder& builder) {
            auto script = builder.Finish();
            if (!script.has_value()) {
                ADD_FAILURE() << script.error().message.c_str();
                return Require(EffectProgramBuilder{}.Finish());
            }
            return *script;
        }

        String Refusal(const EffectProgramBuilder& builder) {
            const auto script = builder.Finish();
            EXPECT_FALSE(script.has_value());
            return script.has_value() ? String() : script.error().message;
        }

        EffectResult RunScript(World& world, const EffectProgramBuilder& builder) {
            const auto script = Build(builder);
            return RunEffects(*script, world.context, world.Buffer());
        }

        /// Stores the constant @p value of @p type into the field @p path, runs and reads the field back through @p field
        template <typename T>
        void ExpectStoreOf(World& world, StringView path, ProgramType type, uint8_t bytes, const RegisterImage& image, const T& expected, const T& field) {
            EffectProgramBuilder builder;
            builder.AddStore(world.SlotOf(path), type, bytes, EffectOperand::OfConstant(type, image));
            const EffectResult result = RunScript(world, builder);
            EXPECT_EQ(result.stores, 1U) << String(path).c_str();
            EXPECT_EQ(result.changed, 1U) << String(path).c_str();
            EXPECT_EQ(std::memcmp(&field, &expected, sizeof(T)), 0) << String(path).c_str();
        }

        std::atomic<int32_t> g_nativeAddend{0};
        std::atomic<int32_t> g_nativeCalls{0};

        /// What a generated function of `caster.attack + <addend>` writes
        void AddendNative(const Context& context, void* result) {
            const auto* caster = static_cast<const ValueUnitStats*>(context.Object(kCasterLevel));
            const int32_t value = caster->attack + g_nativeAddend.load();
            std::memcpy(result, &value, sizeof(value));
            g_nativeCalls.fetch_add(1);
        }

        class NativeGuard {
        public:
            NativeGuard() = default;
            NativeGuard(const NativeGuard&) = delete;
            NativeGuard& operator=(const NativeGuard&) = delete;

            ~NativeGuard() {
                NativePrograms::SetEnabled(true);
                NativePrograms::SetWarnOnMiss(false);
            }
        };

        void InstallAddend(ProgramId id) {
            const Program& program = ProgramPool::Get(id);
            static NativeProgramEntry entry;
            entry = NativeProgramEntry{program.CodeHash(), program.ResultType(), program.ResultRegister(), program.RegisterCount(),
                                       program.Reads(),    program.Code(),       program.Constants(),      program.Source().ToStringView(),
                                       &AddendNative};
            NativePrograms::Install(Span<const NativeProgramEntry>(&entry, 1));
        }

    }  // namespace

    TEST(EffectRunnerTest, AStoreWritesEveryProgramTypeIntoItsField) {
        World world;
        ExpectStoreOf<bool>(world, "caster.alive", ProgramType::Bool, 0, ImageOf(false), false, world.caster.alive);
        ExpectStoreOf<int32_t>(world, "caster.attack", ProgramType::Int32, 0, ImageOf(int32_t{-5}), -5, world.caster.attack);
        ExpectStoreOf<int64_t>(world, "caster.score", ProgramType::Int64, 0, ImageOf(int64_t{-9000000000}), -9000000000, world.caster.score);
        ExpectStoreOf<float>(world, "caster.speed", ProgramType::Float, 0, ImageOf(0.75f), 0.75f, world.caster.speed);
        ExpectStoreOf<double>(world, "caster.precise", ProgramType::Double, 0, ImageOf(-2.5), -2.5, world.caster.precise);
        const Vec3 velocity{4.0f, 5.0f, 6.0f};
        ExpectStoreOf<Vec3>(world, "caster.velocity", ProgramType::Vec3, 0, ImageOf(velocity), velocity, world.caster.velocity);
        const Color tint{1, 2, 3, 4};
        ExpectStoreOf<Color>(world, "caster.tint", ProgramType::Color, 0, ImageOf(tint), tint, world.caster.tint);
        const PoolString title = "effects-runner-title"_intern;
        ExpectStoreOf<PoolString>(world, "caster.title", ProgramType::String, 0, ImageOf(title), title, world.caster.title);
    }

    TEST(EffectRunnerTest, AStoreOfAnEnumWritesTheWidthOfItsField) {
        World world;
        // The fields sit side by side: a write wider than the field would change the next one
        const ValueShortEnum shortBefore = world.widths.wide16;
        const ValueWideEnum wideBefore = world.widths.wide32;
        EffectProgramBuilder narrow;
        narrow.AddStore(world.SlotOf("widths.narrow"), ProgramType::Enum, sizeof(ValueNarrowEnum), EffectOperand::OfConstant(ProgramType::Enum, EnumImage(0)));
        EXPECT_EQ(RunScript(world, narrow).changed, 1U);
        EXPECT_EQ(world.widths.narrow, ValueNarrowEnum::Low);
        EXPECT_EQ(world.widths.wide16, shortBefore);
        EXPECT_EQ(world.widths.wide32, wideBefore);

        EffectProgramBuilder both;
        both.AddStore(world.SlotOf("widths.wide16"), ProgramType::Enum, sizeof(ValueShortEnum), EffectOperand::OfConstant(ProgramType::Enum, EnumImage(2)));
        both.AddStore(world.SlotOf("widths.wide32"), ProgramType::Enum, sizeof(ValueWideEnum), EffectOperand::OfConstant(ProgramType::Enum, EnumImage(0)));
        EXPECT_EQ(RunScript(world, both).changed, 2U);
        EXPECT_EQ(world.widths.narrow, ValueNarrowEnum::Low);
        EXPECT_EQ(world.widths.wide16, ValueShortEnum::High);
        EXPECT_EQ(world.widths.wide32, ValueWideEnum::Low);

        EffectProgramBuilder element;
        element.AddStore(world.SlotOf("caster.element"), ProgramType::Enum, sizeof(ValueElement), EffectOperand::OfConstant(ProgramType::Enum, EnumImage(2)));
        EXPECT_EQ(RunScript(world, element).changed, 1U);
        EXPECT_EQ(world.caster.element, ValueElement::Earth);
    }

    TEST(EffectRunnerTest, AnEnumFieldIsReadAtItsWidthAndStoredAtAnother) {
        World world;
        world.widths.wide16 = ValueShortEnum::High;
        world.widths.wide32 = ValueWideEnum::Low;
        EffectProgramBuilder builder;
        builder.AddStore(world.SlotOf("widths.wide32"), ProgramType::Enum, sizeof(ValueWideEnum), EffectOperand::OfSlot(ProgramType::Enum, world.SlotOf("widths.wide16"), sizeof(ValueShortEnum)));
        EXPECT_EQ(RunScript(world, builder).changed, 1U);
        EXPECT_EQ(world.widths.wide32, ValueWideEnum::High);
    }

    TEST(EffectRunnerTest, AStoreOfARegisteredTypeWritesItsSizeAndNoMore) {
        World world;
        DomainBits256 walls;
        walls.words = {1, 2, 3, 4};
        const DomainPair scoreBefore = world.board.score;
        ExpectStoreOf<DomainBits256>(world, "board.walls", ProgramType::Domain, sizeof(DomainBits256), ImageOf(walls), walls, world.board.walls);
        EXPECT_EQ(world.board.score, scoreBefore);

        const DomainPair pair{-7, 9};
        ExpectStoreOf<DomainPair>(world, "board.score", ProgramType::Domain, sizeof(DomainPair), ImageOf(pair), pair, world.board.score);
        EXPECT_EQ(world.board.walls, walls);
    }

    TEST(EffectRunnerTest, ARegisteredTypeCopiesFromOneFieldToAnother) {
        World world;
        world.board.walls.words = {11, 22, 33, 44};
        EffectProgramBuilder builder;
        builder.AddStore(world.SlotOf("board.walls"), ProgramType::Domain, sizeof(DomainBits256), EffectOperand::OfConstant(ProgramType::Domain, RegisterImage{}));
        builder.AddStore(world.SlotOf("board.score"), ProgramType::Domain, sizeof(DomainPair), EffectOperand::OfSlot(ProgramType::Domain, world.SlotOf("board.score"), sizeof(DomainPair)));
        const EffectResult result = RunScript(world, builder);
        EXPECT_EQ(result.changed, 1U) << "the copy of a field onto itself changes nothing";
        EXPECT_EQ(world.board.walls, DomainBits256{});
    }

    TEST(EffectRunnerTest, AStoreAfterAnotherSeesTheNewValue) {
        World world;
        world.caster.attack = 12;
        const EffectOperand next = world.Formula("caster.attack + 1", DescribeType<int32_t>());
        ASSERT_EQ(next.kind, CompiledKind::Program);
        EffectProgramBuilder builder;
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, next);
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, next);
        EXPECT_EQ(RunScript(world, builder).changed, 2U);
        EXPECT_EQ(world.caster.attack, 14);
    }

    TEST(EffectRunnerTest, AStoreThatChangesNothingBumpsNoCounter) {
        World world;
        EffectProgramBuilder builder;
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{50}));
        const uint64_t start = world.casterVersion;
        EffectResult result = RunScript(world, builder);
        EXPECT_EQ(result.changed, 1U);
        EXPECT_EQ(world.casterVersion, start + 1);

        result = RunScript(world, builder);
        EXPECT_EQ(result.stores, 1U);
        EXPECT_EQ(result.changed, 0U);
        EXPECT_EQ(world.casterVersion, start + 1);

        EffectProgramBuilder two;
        two.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{1}));
        two.AddStore(world.SlotOf("caster.count"), ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{2}));
        EXPECT_EQ(RunScript(world, two).changed, 2U);
        EXPECT_EQ(world.casterVersion, start + 3) << "one bump per changed store";
    }

    TEST(EffectRunnerTest, AStoreToAReadOnlyLevelIsSkippedAndCounted) {
        World world;
        const float powerBefore = world.cast.power;
        EffectProgramBuilder builder;
        builder.AddStore(world.SlotOf("cast.power"), ProgramType::Float, 0, ConstantOf(ProgramType::Float, 9.0f));
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{3}));
        int asserts = 0;
        EffectResult result;
        {
            const AssertBridge::Capture capture([&asserts](const AssertEvent&) { ++asserts; });
            result = RunScript(world, builder);
        }
        EXPECT_EQ(result.refused, 1U);
        EXPECT_EQ(result.stores, 1U);
        EXPECT_EQ(world.cast.power, powerBefore);
        EXPECT_EQ(world.caster.attack, 3) << "the next step still runs";
#if ENGINE_ENABLE_ASSERTS
        EXPECT_EQ(asserts, 1);
#else
        EXPECT_EQ(asserts, 0);
#endif
    }

    TEST(EffectRunnerTest, AnIfElseTakesOneBranch) {
        World world;
        EffectProgramBuilder builder;
        const uint32_t branch = builder.AddJumpIfNot(world.Formula("caster.alive", DescribeType<bool>(), 1));
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{1}));
        const uint32_t skip = builder.AddJump();
        builder.PatchJump(branch, builder.NextStep());
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{2}));
        builder.PatchJump(skip, builder.NextStep());

        world.caster.alive = true;
        EXPECT_EQ(RunScript(world, builder).stores, 1U);
        EXPECT_EQ(world.caster.attack, 1);
        world.caster.alive = false;
        EXPECT_EQ(RunScript(world, builder).stores, 1U);
        EXPECT_EQ(world.caster.attack, 2);
    }

    TEST(EffectRunnerTest, NestedIfsReachTheirOwnEnds) {
        World world;
        // if (attack > 5) { if (attack > 10) score = 1 else score = 2 } else score = 3; count = 9
        EffectProgramBuilder builder;
        const uint32_t outer = builder.AddJumpIfNot(world.Formula("caster.attack > 5", DescribeType<bool>()));
        const uint32_t inner = builder.AddJumpIfNot(world.Formula("caster.attack > 10", DescribeType<bool>()));
        builder.AddStore(world.SlotOf("caster.score"), ProgramType::Int64, 0, ConstantOf(ProgramType::Int64, int64_t{1}));
        const uint32_t innerEnd = builder.AddJump();
        builder.PatchJump(inner, builder.NextStep());
        builder.AddStore(world.SlotOf("caster.score"), ProgramType::Int64, 0, ConstantOf(ProgramType::Int64, int64_t{2}));
        builder.PatchJump(innerEnd, builder.NextStep());
        const uint32_t outerEnd = builder.AddJump();
        builder.PatchJump(outer, builder.NextStep());
        builder.AddStore(world.SlotOf("caster.score"), ProgramType::Int64, 0, ConstantOf(ProgramType::Int64, int64_t{3}));
        builder.PatchJump(outerEnd, builder.NextStep());
        builder.AddStore(world.SlotOf("caster.count"), ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{9}));

        const Array<int32_t, 3> attacks{12, 7, 1};
        const Array<int64_t, 3> scores{1, 2, 3};
        for (size_t i = 0; i < attacks.size(); ++i) {
            world.caster.attack = attacks[i];
            world.caster.count = 0;
            RunScript(world, builder);
            EXPECT_EQ(world.caster.score, scores[i]) << "attack " << attacks[i];
            EXPECT_EQ(world.caster.count, 9U) << "the step after the ifs runs on every path";
        }
    }

    TEST(EffectRunnerTest, ACommandCarriesItsArguments) {
        World world;
        const EffectsData::EffectToys& toys = EffectsData::RegisterEffectToys();
        DomainBits256 mask;
        mask.words = {5, 6, 7, 8};
        world.board.walls = mask;
        world.caster.attack = 41;

        EffectProgramBuilder builder;
        builder.AddEmit(toys.ping, {});
        const Array<EffectOperand, 1> count{world.Formula("caster.attack + 1", DescribeType<int32_t>())};
        builder.AddEmit(toys.count, Span<const EffectOperand>(count.data(), count.size()));
        const Array<EffectOperand, 3> mixed{ConstantOf(ProgramType::Int32, int32_t{-3}), EffectOperand::OfConstant(ProgramType::Enum, EnumImage(2)),
                                            EffectOperand::OfSlot(ProgramType::Domain, world.SlotOf("board.walls"), sizeof(DomainBits256))};
        builder.AddEmit(toys.mixed, Span<const EffectOperand>(mixed.data(), mixed.size()));

        const EffectResult result = RunScript(world, builder);
        EXPECT_EQ(result.emitted, 3U);
        EXPECT_EQ(result.dropped, 0U);
        const Span<const EmittedCommand> commands = world.Buffer().Commands();
        ASSERT_EQ(commands.size(), 3U);
        EXPECT_EQ(commands[0].id, toys.ping);
        EXPECT_EQ(commands[0].count, 0U);
        EXPECT_EQ(commands[1].id, toys.count);
        EXPECT_EQ(commands[1].Argument<int32_t>(0), 42);
        EXPECT_EQ(commands[2].id, toys.mixed);
        EXPECT_EQ(commands[2].Argument<int32_t>(0), -3);
        EXPECT_EQ(commands[2].Argument<DomainDirection>(1), DomainDirection::South);
        EXPECT_EQ(commands[2].Argument<DomainBits256>(2), mask);
    }

    TEST(EffectRunnerTest, AFullBufferDropsTheNewestAndTheResultCountsThem) {
        World world;
        const EffectsData::EffectToys& toys = EffectsData::RegisterEffectToys();
        constexpr size_t kCapacity = 2;
        constexpr size_t kEmits = 5;
        Array<EmittedCommand, kCapacity> storage{};
        EmitBuffer small{Span<EmittedCommand>(storage.data(), storage.size())};
        EffectProgramBuilder builder;
        for (int32_t i = 0; i < static_cast<int32_t>(kEmits); ++i) {
            const Array<EffectOperand, 1> argument{ConstantOf(ProgramType::Int32, i)};
            builder.AddEmit(toys.count, Span<const EffectOperand>(argument.data(), argument.size()));
        }
        const auto script = Build(builder);
        const EffectResult result = RunEffects(*script, world.context, small);
        EXPECT_EQ(result.emitted, kCapacity);
        EXPECT_EQ(result.dropped, kEmits - kCapacity);
        EXPECT_EQ(small.Dropped(), kEmits - kCapacity);
        ASSERT_EQ(small.Count(), kCapacity);
        EXPECT_EQ(small.Commands()[0].Argument<int32_t>(0), 0);
        EXPECT_EQ(small.Commands()[1].Argument<int32_t>(0), 1);
    }

    TEST(EffectRunnerTest, ReadsAndWritesNameTheLevelsTheScriptTouches) {
        World world;
        EffectProgramBuilder builder;
        // The program reads the caster, the slot reads the cast; the stores write the board and the widths
        builder.AddStore(world.SlotOf("board.score"), ProgramType::Domain, sizeof(DomainPair), EffectOperand::OfConstant(ProgramType::Domain, ImageOf(DomainPair{})));
        const uint32_t branch = builder.AddJumpIfNot(world.Formula("caster.attack > 5", DescribeType<bool>()));
        builder.AddStore(world.SlotOf("widths.narrow"), ProgramType::Enum, sizeof(ValueNarrowEnum), EffectOperand::OfConstant(ProgramType::Enum, EnumImage(1)));
        builder.PatchJump(branch, builder.NextStep());
        builder.AddStore(world.SlotOf("board.turn"), ProgramType::Int32, 0, world.Formula("cast.level", DescribeType<int32_t>()));
        const auto script = Build(builder);
        EXPECT_EQ(script->Reads(), LevelBit(kCasterLevel) | LevelBit(kCastLevel));
        EXPECT_EQ(script->Writes(), LevelBit(kBoardLevel) | LevelBit(kWidthsLevel));
    }

    TEST(EffectRunnerTest, AGeneratedFunctionAndTheInterpreterStoreTheSame) {
        const NativeGuard guard;
        World world;
        static std::atomic<int32_t> freshAddend{2'000'000};
        const int32_t addend = freshAddend.fetch_add(1);
        const EffectOperand operand = world.Formula(Format("caster.attack + {}", addend), DescribeType<int32_t>());
        ASSERT_EQ(operand.kind, CompiledKind::Program);
        g_nativeAddend = addend;
        InstallAddend(operand.program);
        ASSERT_NE(ProgramPool::Native(operand.program), nullptr);

        EffectProgramBuilder builder;
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, operand);
        world.caster.attack = 5;
        const int32_t callsBefore = g_nativeCalls.load();
        RunScript(world, builder);
        EXPECT_EQ(world.caster.attack, 5 + addend);
        EXPECT_EQ(g_nativeCalls.load(), callsBefore + 1) << "the bound function ran";

        NativePrograms::SetEnabled(false);
        world.caster.attack = 5;
        RunScript(world, builder);
        EXPECT_EQ(world.caster.attack, 5 + addend);
        EXPECT_EQ(g_nativeCalls.load(), callsBefore + 1) << "the interpreter ran";
    }

    TEST(EffectRunnerTest, ARunAllocatesNothing) {
        World world;
        const EffectsData::EffectToys& toys = EffectsData::RegisterEffectToys();
        EffectProgramBuilder builder;
        const uint32_t branch = builder.AddJumpIfNot(world.Formula("caster.attack > 5", DescribeType<bool>()));
        builder.AddStore(world.SlotOf("caster.score"), ProgramType::Int64, 0, ConstantOf(ProgramType::Int64, int64_t{4}));
        builder.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, world.Formula("caster.attack * 2 - 1", DescribeType<int32_t>()));
        builder.PatchJump(branch, builder.NextStep());
        const Array<EffectOperand, 1> argument{world.Formula("caster.attack", DescribeType<int32_t>(), 0)};
        builder.AddEmit(toys.count, Span<const EffectOperand>(argument.data(), argument.size()));
        const auto script = Build(builder);
        ExpectNoAllocations([&] {
            for (int32_t i = 0; i < 4; ++i) {
                world.Buffer().Clear();
                RunEffects(*script, world.context, world.Buffer());
            }
        });
    }

    TEST(EffectRunnerTest, FinishRefusesAJumpThatDoesNotGoForward) {
        EffectProgramBuilder back;
        back.AddJump();
        back.PatchJump(0, 0);
        EXPECT_NE(Refusal(back).find("forward"), String::npos);

        EffectProgramBuilder outside;
        outside.AddJump();
        outside.PatchJump(0, 5);
        EXPECT_NE(Refusal(outside).find("the script has 1"), String::npos);

        EffectProgramBuilder unpatched;
        unpatched.AddJump();
        EXPECT_NE(Refusal(unpatched).find("never given a target"), String::npos);

        EffectProgramBuilder toTheEnd;
        toTheEnd.AddJump();
        toTheEnd.PatchJump(0, 1);
        EXPECT_TRUE(toTheEnd.Finish().has_value()) << "a jump to the end of the script is allowed";
    }

    TEST(EffectRunnerTest, FinishRefusesWhatARunCouldNotDo) {
        World world;
        const EffectsData::EffectToys& toys = EffectsData::RegisterEffectToys();

        EffectProgramBuilder level;
        level.AddStore(SlotRef{kMaxContextLevels, 0}, ProgramType::Int32, 0, ConstantOf(ProgramType::Int32, int32_t{1}));
        EXPECT_NE(Refusal(level).find("level"), String::npos);

        EffectProgramBuilder readLevel;
        readLevel.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, EffectOperand::OfSlot(ProgramType::Int32, SlotRef{kMaxContextLevels, 0}));
        EXPECT_NE(Refusal(readLevel).find("reads level"), String::npos);

        EffectProgramBuilder mismatch;
        mismatch.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, ConstantOf(ProgramType::Float, 1.0f));
        EXPECT_NE(Refusal(mismatch).find("Float"), String::npos);

        EffectProgramBuilder width;
        width.AddStore(world.SlotOf("widths.narrow"), ProgramType::Enum, 3, EffectOperand::OfConstant(ProgramType::Enum, EnumImage(0)));
        EXPECT_NE(Refusal(width).find("3 bytes"), String::npos);

        EffectProgramBuilder domainWidth;
        domainWidth.AddStore(world.SlotOf("board.walls"), ProgramType::Domain, kProgramRegisterBytes + 1, EffectOperand::OfConstant(ProgramType::Domain, RegisterImage{}));
        EXPECT_NE(Refusal(domainWidth).find("bytes"), String::npos);

        EffectProgramBuilder condition;
        const uint32_t jump = condition.AddJumpIfNot(ConstantOf(ProgramType::Int32, int32_t{1}));
        condition.PatchJump(jump, 1);
        EXPECT_NE(Refusal(condition).find("condition"), String::npos);

        EffectProgramBuilder unknown;
        unknown.AddEmit(static_cast<CommandId>(EffectCommands::Count() + 100), {});
        EXPECT_NE(Refusal(unknown).find("not registered"), String::npos);

        EffectProgramBuilder count;
        const Array<EffectOperand, 2> two{ConstantOf(ProgramType::Int32, int32_t{1}), ConstantOf(ProgramType::Int32, int32_t{2})};
        count.AddEmit(toys.count, Span<const EffectOperand>(two.data(), two.size()));
        EXPECT_NE(Refusal(count).find("takes 1"), String::npos);

        EffectProgramBuilder type;
        const Array<EffectOperand, 1> wrong{ConstantOf(ProgramType::Float, 1.0f)};
        type.AddEmit(toys.count, Span<const EffectOperand>(wrong.data(), wrong.size()));
        EXPECT_NE(Refusal(type).find("argument 1"), String::npos);

        EffectProgramBuilder missingProgram;
        missingProgram.AddStore(world.SlotOf("caster.attack"), ProgramType::Int32, 0, EffectOperand::OfProgram(ProgramType::Int32, static_cast<ProgramId>(ProgramPool::Count() + 7)));
        EXPECT_NE(Refusal(missingProgram).find("not in the pool"), String::npos);
    }

    TEST(EffectRunnerTest, FinishRefusesMoreStepsThanTheMost) {
        EffectProgramBuilder builder;
        for (size_t i = 0; i < kMaxEffectSteps; ++i) {
            builder.AddStore(SlotRef{kCasterLevel, 0}, ProgramType::Bool, 0, BoolConstant(true));
        }
        EXPECT_TRUE(builder.Finish().has_value()) << "the most steps are allowed";
        builder.AddStore(SlotRef{kCasterLevel, 0}, ProgramType::Bool, 0, BoolConstant(true));
        EXPECT_NE(Refusal(builder).find("the most is"), String::npos);
    }

    TEST(EffectRunnerTest, AnEmptyScriptDoesNothing) {
        World world;
        const EffectProgramBuilder builder;
        const EffectResult result = RunScript(world, builder);
        EXPECT_EQ(result.stores + result.changed + result.refused + result.emitted + result.dropped, 0U);
    }

}  // namespace BECore::Tests
