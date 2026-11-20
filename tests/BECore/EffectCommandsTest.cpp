// The commands of a script (Values/Effects/CLAUDE.md, "Commands" and "The emit buffer"): the registry a game fills and the buffer a run pushes into.

#include <BECore/EffectsToys.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace EffectsData;
    using namespace ProgramDomainData;

    namespace {

        template <typename T>
        [[nodiscard]] RegisterImage ImageOf(const T& value) {
            RegisterImage image{};
            if constexpr (IsReflectedEnum<T>) {
                const ProgramEnum index{static_cast<uint32_t>(value)};
                std::memcpy(image.bytes.data(), &index, sizeof(index));
            } else {
                std::memcpy(image.bytes.data(), &value, sizeof(T));
            }
            return image;
        }

        [[nodiscard]] EmittedCommand Emitted(CommandId id, int32_t first) {
            EmittedCommand command;
            command.id = id;
            command.count = 1;
            command.arguments[0] = ImageOf(first);
            return command;
        }

        void ExpectRefused(const std::expected<CommandId, String>& result, StringView text, size_t countBefore) {
            ASSERT_FALSE(result.has_value());
            EXPECT_TRUE(Contains(result.error(), text)) << result.error().c_str();
            EXPECT_EQ(EffectCommands::Count(), countBefore);
        }

    }  // namespace

    TEST(EffectCommandsTest, CompileTimeChecks) {
        static_assert(kMaxCommandArguments == kMaxDomainArguments);
        static_assert(std::is_trivially_copyable_v<EmittedCommand>);
        static_assert(std::is_nothrow_invocable_v<decltype(&EmitBuffer::Push), EmitBuffer&, const EmittedCommand&>);
    }

    TEST(EffectCommandsTest, ACommandIsFoundByNameWithItsSignature) {
        const EffectToys& toys = RegisterEffectToys();
        EXPECT_EQ(EffectCommands::Find("FxPing"), toys.ping);
        EXPECT_EQ(EffectCommands::Find("FxMixed"), toys.mixed);
        EXPECT_FALSE(EffectCommands::Find("FxNoSuchCommand").has_value());
        EXPECT_FALSE(EffectCommands::Find("").has_value());

        EXPECT_EQ(EffectCommands::Command(toys.ping).parameterCount, 0U);
        const EffectCommand count = EffectCommands::Command(toys.count);
        EXPECT_EQ(count.name, "FxCount");
        ASSERT_EQ(count.parameterCount, 1U);
        EXPECT_EQ(count.parameters[0].type, ProgramType::Int32);

        const EffectCommand mixed = EffectCommands::Command(toys.mixed);
        ASSERT_EQ(mixed.parameterCount, 3U);
        EXPECT_EQ(mixed.parameters[0].type, ProgramType::Int32);
        EXPECT_EQ(mixed.parameters[1].type, ProgramType::Enum);
        EXPECT_NE(mixed.parameters[1].enumType, nullptr);
        EXPECT_EQ(mixed.parameters[2].type, ProgramType::Domain);
        EXPECT_TRUE(mixed.parameters[2].IsDomain());
    }

    TEST(EffectCommandsTest, IdsAreTheOrderOfRegistrationAndNeverMove) {
        const EffectToys& toys = RegisterEffectToys();
        EXPECT_LT(toys.ping, toys.count);
        EXPECT_LT(toys.count, toys.face);
        EXPECT_LT(toys.face, toys.mask);
        EXPECT_LT(toys.mask, toys.mixed);
        EXPECT_EQ(toys.count, toys.ping + 1);

        // The registry lives for the process: a repeated run finds the probe of the first one and adds nothing
        constexpr StringView kProbe = "FxIdsOrderProbe";
        const size_t before = EffectCommands::Count();
        const Optional<CommandId> known = EffectCommands::Find(kProbe);
        const auto added = known ? std::expected<CommandId, String>(*known) : EffectCommands::Register<float>(kProbe);
        ASSERT_TRUE(added.has_value());
        EXPECT_EQ(*added, known ? *known : before);
        EXPECT_EQ(EffectCommands::Count(), known ? before : before + 1);
        EXPECT_EQ(EffectCommands::Find("FxPing"), toys.ping);
        EXPECT_EQ(EffectCommands::Command(toys.count).name, "FxCount");

        const Vector<String> names = EffectCommands::Names();
        ASSERT_EQ(names.size(), EffectCommands::Count());
        EXPECT_EQ(names[toys.mixed], "FxMixed");
        EXPECT_EQ(names[*added], "FxIdsOrderProbe");
    }

    TEST(EffectCommandsTest, ARefusedRegistrationChangesNothing) {
        (void)RegisterEffectToys();
        const size_t before = EffectCommands::Count();
        const Vector<String> namesBefore = EffectCommands::Names();

        ExpectRefused(EffectCommands::Register<int32_t>("FxCount"), "FxCount", before);
        ExpectRefused(EffectCommands::Register<float>("FxCount"), "registered already", before);
        ExpectRefused(EffectCommands::Register<>(""), "not an identifier", before);
        ExpectRefused(EffectCommands::Register<>("9lives"), "not an identifier", before);
        ExpectRefused(EffectCommands::Register<>("with space"), "not an identifier", before);
        ExpectRefused(EffectCommands::Register<>("a.b"), "not an identifier", before);
        ExpectRefused(EffectCommands::Register<int32_t, int32_t, int32_t, int32_t>("FxFourParameters"), "the most is 3", before);
        ExpectRefused(EffectCommands::Register<int32_t, DomainUnregistered>("FxUnregistered"), "parameter 2 has a type that is not registered", before);
        ExpectRefused(EffectCommands::Register<String>("FxStringParameter"), "parameter 1 has a type that is not registered", before);

        EXPECT_FALSE(EffectCommands::Find("FxFourParameters").has_value());
        EXPECT_FALSE(EffectCommands::Find("FxUnregistered").has_value());
        EXPECT_EQ(EffectCommands::Names(), namesBefore);
    }

    TEST(EffectCommandsTest, AnArgumentIsReadAsItsCppType) {
        EmittedCommand command;
        command.count = 3;
        DomainBits256 mask;
        mask.words = {1, 2, 3, 4};
        command.arguments[0] = ImageOf(int32_t{-77});
        command.arguments[1] = ImageOf(DomainDirection::South);
        command.arguments[2] = ImageOf(mask);
        EXPECT_EQ(command.Argument<int32_t>(0), -77);
        EXPECT_EQ(command.Argument<DomainDirection>(1), DomainDirection::South);
        EXPECT_EQ(command.Argument<DomainBits256>(2), mask);

        EmittedCommand others;
        others.count = 3;
        others.arguments[0] = ImageOf(2.5);
        others.arguments[1] = ImageOf(true);
        others.arguments[2] = ImageOf(Vec3{1.0f, 2.0f, 3.0f});
        EXPECT_EQ(others.Argument<double>(0), 2.5);
        EXPECT_TRUE(others.Argument<bool>(1));
        EXPECT_EQ(others.Argument<Vec3>(2), (Vec3{1.0f, 2.0f, 3.0f}));
    }

    TEST(EffectCommandsTest, ABufferKeepsWhatWasPushedInOrder) {
        const EffectToys& toys = RegisterEffectToys();
        EmitBufferStorage<4> storage;
        EmitBuffer& buffer = storage.Buffer();
        EXPECT_EQ(buffer.Count(), 0U);
        EXPECT_EQ(buffer.Capacity(), 4U);
        for (int32_t i = 0; i < 4; ++i) {
            EXPECT_TRUE(buffer.Push(Emitted(toys.count, i * 10)));
        }
        ASSERT_EQ(buffer.Count(), 4U);
        EXPECT_EQ(buffer.Dropped(), 0U);
        const Span<const EmittedCommand> commands = buffer.Commands();
        ASSERT_EQ(commands.size(), 4U);
        for (size_t i = 0; i < commands.size(); ++i) {
            EXPECT_EQ(commands[i].id, toys.count);
            EXPECT_EQ(commands[i].Argument<int32_t>(0), static_cast<int32_t>(i) * 10);
        }
    }

    TEST(EffectCommandsTest, AFullBufferDropsTheNewestAndCountsThem) {
        const EffectToys& toys = RegisterEffectToys();
        constexpr size_t kCapacity = 6;
        constexpr size_t kExtra = 5;
        EmitBufferStorage<kCapacity> storage;
        EmitBuffer& buffer = storage.Buffer();
        const EmittedCommand* const address = buffer.Commands().data();
        for (size_t i = 0; i < kCapacity + kExtra; ++i) {
            const bool pushed = buffer.Push(Emitted(toys.count, static_cast<int32_t>(i)));
            EXPECT_EQ(pushed, i < kCapacity);
        }
        EXPECT_EQ(buffer.Count(), kCapacity);
        EXPECT_EQ(buffer.Dropped(), kExtra);
        EXPECT_EQ(buffer.Commands().data(), address);
        const Span<const EmittedCommand> commands = buffer.Commands();
        ASSERT_EQ(commands.size(), kCapacity);
        for (size_t i = 0; i < kCapacity; ++i) {
            EXPECT_EQ(commands[i].Argument<int32_t>(0), static_cast<int32_t>(i));
        }
    }

    TEST(EffectCommandsTest, ClearEmptiesTheBufferAndTheDroppedCount) {
        const EffectToys& toys = RegisterEffectToys();
        EmitBufferStorage<2> storage;
        EmitBuffer& buffer = storage.Buffer();
        for (int32_t i = 0; i < 3; ++i) {
            buffer.Push(Emitted(toys.count, i));
        }
        ASSERT_EQ(buffer.Dropped(), 1U);
        buffer.Clear();
        EXPECT_EQ(buffer.Count(), 0U);
        EXPECT_EQ(buffer.Dropped(), 0U);
        EXPECT_TRUE(buffer.Commands().empty());
        EXPECT_TRUE(buffer.Push(Emitted(toys.ping, 0)));
        EXPECT_EQ(buffer.Count(), 1U);
    }

    TEST(EffectCommandsTest, ABufferWithNoStorageDropsEverything) {
        const EffectToys& toys = RegisterEffectToys();
        EmitBuffer buffer;
        EXPECT_FALSE(buffer.Push(Emitted(toys.ping, 0)));
        EXPECT_EQ(buffer.Count(), 0U);
        EXPECT_EQ(buffer.Dropped(), 1U);
    }

}  // namespace BECore::Tests
