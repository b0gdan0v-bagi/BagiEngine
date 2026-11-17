// Tables of a program (Values/Programs/): the LoadTable instruction by hand here, the `at` and `ByLevel` formulas in the sections below.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr int32_t kInt32Min = std::numeric_limits<int32_t>::min();
        constexpr int32_t kInt32Max = std::numeric_limits<int32_t>::max();

        /// A table over @p entries read at @p index: the index and the entries are constants, the result is the register of the table instruction
        template <typename T>
        std::expected<Program, ProgramError> TableProgram(const Vector<T>& entries, int32_t index) {
            ProgramBuilder builder;
            const uint8_t indexReg = builder.AcquireRegister().value();
            builder.EmitLoadConst(indexReg, index);
            Vector<RegisterImage> images;
            images.reserve(entries.size());
            for (const T& entry : entries) {
                RegisterImage image{};
                std::memcpy(image.bytes.data(), &entry, sizeof(T));
                images.push_back(image);
            }
            const uint32_t first = builder.AddConstantRun(Span<const RegisterImage>(images.data(), images.size()));
            const uint8_t dst = builder.AcquireRegister().value();
            builder.EmitLoadTable(dst, indexReg, first, static_cast<uint32_t>(entries.size()));
            return builder.Finish(kProgramTypeOf<T>, dst);
        }

        /// Colors have no `==`, and a table must give back the very bytes it was made from
        template <typename T>
        bool SameBytes(const T& a, const T& b) {
            return std::memcmp(&a, &b, sizeof(T)) == 0;
        }

        template <typename T>
        T At(const Vector<T>& entries, int32_t index) {
            const auto program = TableProgram(entries, index);
            EXPECT_TRUE(program.has_value());
            return program.has_value() ? Run<T>(*program, Context{}) : T{};
        }

        /// Every index that matters of a table of @p entries (three or more, all different): below 0, the first, the middle, the last, past the end, the extremes
        template <typename T>
        void CheckTable(const Vector<T>& entries) {
            ASSERT_GE(entries.size(), 3U);
            const auto last = static_cast<int32_t>(entries.size()) - 1;
            EXPECT_TRUE(SameBytes(At(entries, -1), entries.front()));
            EXPECT_TRUE(SameBytes(At(entries, kInt32Min), entries.front()));
            EXPECT_TRUE(SameBytes(At(entries, 0), entries.front()));
            EXPECT_TRUE(SameBytes(At(entries, 1), entries[1]));
            EXPECT_TRUE(SameBytes(At(entries, last), entries.back()));
            EXPECT_TRUE(SameBytes(At(entries, last + 1), entries.back()));
            EXPECT_TRUE(SameBytes(At(entries, kInt32Max), entries.back()));
            for (int32_t i = 0; i <= last; ++i) {
                EXPECT_TRUE(SameBytes(At(entries, i), entries[static_cast<size_t>(i)])) << i;
            }
        }

        /// The levels a formula table is read at: below the first entry, in range and past the end of every table of these tests
        constexpr int32_t kLevels[] = {kInt32Min, -3, -1, 0, 1, 2, 3, 4, 5, 8, kInt32Max};

        template <typename T>
        Value<T> Compiled(const ValueWorld& world, StringView text) {
            const auto value = CompileValue<T>(text, world.declaration);
            EXPECT_TRUE(value.has_value()) << String(text).c_str() << ": " << (value.has_value() ? String() : value.error().message).c_str();
            return value.value_or(Value<T>{});
        }

        /// The formula `at(cast.level, <entries>)` against the same table in C++, at every level of `kLevels`
        template <typename T, size_t N>
        void ExpectTableFormula(StringView text, const T (&entries)[N]) {
            ValueWorld world;
            const Value<T> value = Compiled<T>(world, text);
            ASSERT_EQ(value.Kind(), ValueKind::Program) << String(text).c_str();
            for (const int32_t level : kLevels) {
                world.cast.level = level;
                const size_t at = level < 0 ? 0 : std::min<size_t>(static_cast<size_t>(level), N - 1);
                EXPECT_TRUE(SameBytes(value.Evaluate(world.context), entries[at])) << String(text).c_str() << " at level " << level;
            }
        }

        Instruction TableInstruction(uint32_t first, uint32_t count) {
            Instruction ins{Opcode::LoadTable, 1, 0, 0, 0, first};
            SetTableEntryCount(ins, count);
            return ins;
        }

    }  // namespace

    TEST(ProgramTableTest, EveryProgramTypeHasTables) {
        CheckTable<bool>({true, false, true});
        CheckTable<int32_t>({10, -20, kInt32Max, kInt32Min});
        CheckTable<int64_t>({10, -20, std::numeric_limits<int64_t>::max()});
        CheckTable<float>({1.5f, -2.5f, 0.25f});
        CheckTable<double>({1.5, -2.5, 0.25, 1e300});
        CheckTable<Vec2>({Vec2{1.0f, 2.0f}, Vec2{3.0f, 4.0f}, Vec2{5.0f, 6.0f}});
        CheckTable<Vec3>({Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}});
        CheckTable<Vec4>({Vec4{1.0f, 2.0f, 3.0f, 4.0f}, Vec4{5.0f, 6.0f, 7.0f, 8.0f}, Vec4{9.0f, 10.0f, 11.0f, 12.0f}});
        CheckTable<IVec2>({IVec2{1, 2}, IVec2{3, 4}, IVec2{5, 6}});
        CheckTable<Quat>({Quat{0.0f, 0.0f, 0.0f, 1.0f}, Quat{1.0f, 0.0f, 0.0f, 0.0f}, Quat{0.0f, 1.0f, 0.0f, 0.0f}});
        CheckTable<Color>({Color{1, 2, 3, 4}, Color{5, 6, 7, 8}, Color{9, 10, 11, 12}});
        CheckTable<ProgramEnum>({ProgramEnum{2}, ProgramEnum{0}, ProgramEnum{1}});
        CheckTable<PoolString>({"Fire"_intern, "Water"_intern, "Earth"_intern});
    }

    TEST(ProgramTableTest, ATableOfOneEntryIsThatEntryForEveryIndex) {
        const Vector<int32_t> entries{42};
        for (const int32_t index : {kInt32Min, -1, 0, 1, kInt32Max}) {
            EXPECT_EQ(At(entries, index), 42) << index;
        }
    }

    TEST(ProgramTableTest, TheEntriesAreConsecutiveWhateverTheConstantsBefore) {
        ProgramBuilder builder;
        const uint8_t indexReg = builder.AcquireRegister().value();
        builder.EmitLoadConst(indexReg, 1);
        const uint32_t before = builder.AddConstant(int32_t{20});
        const Vector<int32_t> entries{10, 20, 30};
        Vector<RegisterImage> images(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            std::memcpy(images[i].bytes.data(), &entries[i], sizeof(int32_t));
        }
        const uint32_t first = builder.AddConstantRun(Span<const RegisterImage>(images.data(), images.size()));
        // 20 is one of the entries and also a constant already: the run is appended as it is, so the table stays one run
        EXPECT_NE(first, before);
        const uint8_t dst = builder.AcquireRegister().value();
        builder.EmitLoadTable(dst, indexReg, first, 3);
        const Program program = builder.Finish(ProgramType::Int32, dst).value();
        EXPECT_EQ(Run<int32_t>(program, Context{}), 20);
        EXPECT_EQ(program.Constants().size(), 5U);
    }

    TEST(ProgramTableTest, FinishRefusesATableThatCannotRun) {
        // The pool holds @p constants constants in all: the load of the index is the first
        const auto finish = [](const Instruction& ins, size_t constants) {
            ProgramBuilder builder;
            const uint8_t indexReg = builder.AcquireRegister().value();
            builder.EmitLoadConst(indexReg, 0);
            const uint8_t dst = builder.AcquireRegister().value();
            for (size_t i = 1; i < constants; ++i) {
                static_cast<void>(builder.AddConstant(static_cast<int32_t>(i + 100)));
            }
            Instruction table = ins;
            table.dst = dst;
            table.a = indexReg;
            builder.Emit(table);
            return builder.Finish(ProgramType::Int32, dst);
        };
        const auto empty = finish(TableInstruction(1, 0), 3);
        ASSERT_FALSE(empty.has_value());
        EXPECT_NE(empty.error().message.find("no entry"), String::npos) << empty.error().message;

        const auto shortRun = finish(TableInstruction(2, 3), 4);
        ASSERT_FALSE(shortRun.has_value());
        EXPECT_NE(shortRun.error().message.find("but there are"), String::npos) << shortRun.error().message;

        const auto pastTheEnd = finish(TableInstruction(std::numeric_limits<uint32_t>::max(), 2), 4);
        EXPECT_FALSE(pastTheEnd.has_value());

        EXPECT_TRUE(finish(TableInstruction(1, 3), 4).has_value());
        EXPECT_FALSE(finish(TableInstruction(1, 4), 4).has_value());
    }

    TEST(ProgramTableTest, FinishRefusesATableOverTheLimit) {
        ProgramBuilder builder;
        const uint8_t indexReg = builder.AcquireRegister().value();
        builder.EmitLoadConst(indexReg, 0);
        Vector<RegisterImage> images(kMaxTableEntries + 1);
        const uint32_t first = builder.AddConstantRun(Span<const RegisterImage>(images.data(), images.size()));
        const uint8_t dst = builder.AcquireRegister().value();
        builder.EmitLoadTable(dst, indexReg, first, kMaxTableEntries + 1);
        const auto refused = builder.Finish(ProgramType::Int32, dst);
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().message.find(Format("at most {}", kMaxTableEntries)), String::npos) << refused.error().message;

        ProgramBuilder exact;
        const uint8_t exactIndex = exact.AcquireRegister().value();
        exact.EmitLoadConst(exactIndex, 0);
        images.resize(kMaxTableEntries);
        const uint32_t exactFirst = exact.AddConstantRun(Span<const RegisterImage>(images.data(), images.size()));
        const uint8_t exactDst = exact.AcquireRegister().value();
        exact.EmitLoadTable(exactDst, exactIndex, exactFirst, kMaxTableEntries);
        EXPECT_TRUE(exact.Finish(ProgramType::Int32, exactDst).has_value());
    }

    TEST(ProgramTableTest, TheCountIsSixteenBitsAcrossTwoFields) {
        for (const uint32_t count : {1U, 255U, 256U, 257U, kMaxTableEntries}) {
            Instruction ins;
            SetTableEntryCount(ins, count);
            EXPECT_EQ(TableEntryCount(ins), count);
        }
    }

    TEST(ProgramTableTest, TheTableIndexRuleIsClamped) {
        EXPECT_EQ(Kernel::TableIndex(kInt32Min, 5), 0U);
        EXPECT_EQ(Kernel::TableIndex(-1, 5), 0U);
        EXPECT_EQ(Kernel::TableIndex(4, 5), 4U);
        EXPECT_EQ(Kernel::TableIndex(5, 5), 4U);
        EXPECT_EQ(Kernel::TableIndex(kInt32Max, 5), 4U);
        EXPECT_EQ(Kernel::TableIndex(kInt32Max, 1), 0U);
    }

    TEST(ProgramTableTest, TwoProgramsWithTheSameCodeAndOtherEntriesAreTwoPrograms) {
        const Program first = TableProgram<int32_t>({1, 2, 3}, 1).value();
        const Program second = TableProgram<int32_t>({1, 2, 4}, 1).value();
        const Program same = TableProgram<int32_t>({1, 2, 3}, 1).value();
        EXPECT_EQ(first.Code().size(), second.Code().size());
        EXPECT_FALSE(first.SameCode(second));
        EXPECT_TRUE(first.SameCode(same));
        const auto firstId = ProgramPool::Intern(first);
        const auto secondId = ProgramPool::Intern(second);
        const auto sameId = ProgramPool::Intern(same);
        ASSERT_TRUE(firstId && secondId && sameId);
        EXPECT_NE(*firstId, *secondId);
        EXPECT_EQ(*firstId, *sameId);
    }

    TEST(ProgramTableTest, TheDisassemblyNamesTheRunOfAnEntry) {
        const Program program = TableProgram<int32_t>({7, 8, 9}, 1).value();
        const String text = program.Disassemble();
        EXPECT_NE(text.find("LoadTable"), String::npos) << text;
        EXPECT_NE(text.find("3 entries"), String::npos) << text;
    }

    TEST(ProgramTableTest, AnInlineTableOfEveryNumberTypeEqualsAnArray) {
        ExpectTableFormula<int32_t>("at(cast.level, 10, -20, 30, 40)", {10, -20, 30, 40});
        ExpectTableFormula<int64_t>("at(cast.level, 10, -20, 5000000000)", {10, -20, 5000000000});
        ExpectTableFormula<float>("at(cast.level, 1.5, -2.5, 0.25)", {1.5f, -2.5f, 0.25f});
        ExpectTableFormula<double>("at(cast.level, 1.5, -2.5, 0.1, 1e300)", {1.5, -2.5, 0.1, 1e300});
        ExpectTableFormula<bool>("at(cast.level, true, false, false, true)", {true, false, false, true});
        ExpectTableFormula<int32_t>("at(cast.level, 7)", {7});
    }

    TEST(ProgramTableTest, AnInlineTableOfOtherTypesEqualsAnArray) {
        ExpectTableFormula<Vec3>("at(cast.level, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1))", {Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}});
        ExpectTableFormula<ValueElement>("at(cast.level, Fire, Earth, Water)", {ValueElement::Fire, ValueElement::Earth, ValueElement::Water});
        ExpectTableFormula<PoolString>("at(cast.level, 'low', 'mid', 'high')", {"low"_intern, "mid"_intern, "high"_intern});
    }

    TEST(ProgramTableTest, ATableWhoseCountIsNotARegisterLoadsAtEverySize) {
        // The count sits in the fields that name registers elsewhere: 32 and more, a low byte of 0 and a high byte
        for (const int32_t count : {31, 32, 33, 100, 255, 256, 257, 300, 512}) {
            Vector<int32_t> entries;
            for (int32_t i = 0; i < count; ++i) {
                entries.push_back(i * 7 + 1000);
            }
            CheckTable(entries);
        }
    }

    TEST(ProgramTableTest, AFormulaTableOfManyEntriesRunsFusedAndUnfused) {
        constexpr int32_t kEntries = 300;
        String text = "at(cast.level";
        Vector<int32_t> entries;
        for (int32_t i = 0; i < kEntries; ++i) {
            entries.push_back(i * 3 + 1000);
            text += Format(", {}", entries.back());
        }
        text += ")";
        ValueWorld world;
        for (const bool fuse : {true, false}) {
            CompileOptions options;
            options.fuse = fuse;
            const auto compiled = CompileExpression(text, world.declaration, DescribeType<int32_t>(), options);
            ASSERT_TRUE(compiled.has_value()) << fuse << ": " << compiled.error().message.c_str();
            ASSERT_EQ(compiled->kind, CompiledKind::Program);
            const Program& program = ProgramPool::Get(compiled->program);
            for (const int32_t level : {kInt32Min, -1, 0, 1, 31, 32, 40, 255, 256, 257, kEntries - 1, kEntries, kInt32Max}) {
                world.cast.level = level;
                const size_t at = level < 0 ? 0 : std::min<size_t>(static_cast<size_t>(level), kEntries - 1);
                EXPECT_EQ(Run<int32_t>(program, world.context), entries[at]) << fuse << " level " << level;
            }
        }
    }

    TEST(ProgramTableTest, TheEntriesTakeTheTypeBesideTheTable) {
        ValueWorld world;
        // A literal entry adapts to the other operand, an integer literal to a float too
        const Value<float> scaled = Compiled<float>(world, "caster.speed * at(cast.level, 1, 2, 3)");
        const Value<double> wide = Compiled<double>(world, "caster.precise + at(cast.level, 1, 2.5, 3)");
        world.caster.speed = 1.5f;
        world.caster.precise = 0.25;
        for (const int32_t level : kLevels) {
            world.cast.level = level;
            const size_t at = level < 0 ? 0 : std::min<size_t>(static_cast<size_t>(level), 2);
            EXPECT_EQ(scaled.Evaluate(world.context), 1.5f * static_cast<float>(at + 1));
            EXPECT_EQ(wide.Evaluate(world.context), 0.25 + (at == 1 ? 2.5 : static_cast<double>(at + 1)));
        }
    }

    TEST(ProgramTableTest, ATableInsideAFormula) {
        ValueWorld world;
        const Value<int32_t> value = Compiled<int32_t>(world, "caster.attack * at(caster.attack % 3 + 1, 1, 2, 3) - at(cast.level, 100, 200)");
        for (const int32_t attack : {0, 1, 5, 12, -4}) {
            for (const int32_t level : kLevels) {
                world.caster.attack = attack;
                world.cast.level = level;
                const int32_t index = attack % 3 + 1;
                const int32_t factor = index < 0 ? 1 : std::min(index, 2) + 1;
                const int32_t offset = level <= 0 ? 100 : 200;
                EXPECT_EQ(value.Evaluate(world.context), attack * factor - offset) << attack << " " << level;
            }
        }
    }

    TEST(ProgramTableTest, ATableOfConstantsOverAConstantIndexFolds) {
        ValueWorld world;
        const struct {
            StringView text;
            int32_t expected;
        } rows[] = {{"at(3, 10, 20, 30, 40)", 40}, {"at(1 + 1, 10, 20, 30)", 30}, {"at(-1, 10, 20)", 10}, {"at(99, 10, 20)", 20}, {"at(0, 5 * 2, 20 + 1)", 10}, {"at(1, 5 * 2, 20 + 1)", 21}};
        for (const auto& row : rows) {
            const Value<int32_t> value = Compiled<int32_t>(world, row.text);
            ASSERT_EQ(value.Kind(), ValueKind::Const) << String(row.text).c_str();
            EXPECT_EQ(value.Get(world.context), row.expected) << String(row.text).c_str();
        }
    }

    TEST(ProgramTableTest, ATableOverAFieldIsALoadAndATable) {
        ValueWorld world;
        // The load and the table are two instructions as the lowering writes them; the peephole pass makes them one (`ProgramFusionTest`), so this is the code without it
        CompileOptions unfused;
        unfused.fuse = false;
        const auto compiled = CompileExpression("at(cast.level, 10, 20, 30)", world.declaration, DescribeType<int32_t>(), unfused);
        ASSERT_TRUE(compiled.has_value());
        ASSERT_EQ(compiled->kind, CompiledKind::Program);
        const Program& program = ProgramPool::Get(compiled->program);
        ASSERT_EQ(program.Code().size(), 2U) << program.Disassemble().c_str();
        EXPECT_EQ(program.Code()[0].op, Opcode::LoadSlotInt32);
        EXPECT_EQ(program.Code()[1].op, Opcode::LoadTable);
        EXPECT_EQ(program.Constants().size(), 3U);
        EXPECT_EQ(program.Reads(), LevelBit(1));
        const String text = program.Disassemble();
        EXPECT_NE(text.find("LoadTable r0 <- r0 [3 entries from constant 0]"), String::npos) << text;
        EXPECT_NE(text.find("result Int32 in r0"), String::npos) << text;

        const Value<int32_t> value = Compiled<int32_t>(world, "at(cast.level, 10, 20, 30)");
        ASSERT_EQ(value.Kind(), ValueKind::Program);
        EXPECT_EQ(ProgramPool::Get(value.Id()).Code().size(), 1U);
        EXPECT_EQ(ProgramPool::Get(value.Id()).Code()[0].op, Opcode::LoadTableSlot);
    }

    TEST(ProgramTableTest, TwoTextsThatMakeTheSameCodeAreOnePoolEntry) {
        ValueWorld world;
        const Value<int32_t> first = Compiled<int32_t>(world, "at(cast.level, 10, 20, 30)");
        const Value<int32_t> second = Compiled<int32_t>(world, "at( cast.level ,10,20 , 15 * 2 )");
        const Value<int32_t> other = Compiled<int32_t>(world, "at(cast.level, 10, 20, 31)");
        EXPECT_EQ(first.Id(), second.Id());
        EXPECT_NE(first.Id(), other.Id());
    }

    TEST(ProgramTableTest, AnEntryThatIsNoConstantIsRefusedAtItsOwnColumn) {
        ValueWorld world;
        const struct {
            StringView text;
            uint32_t column;
        } rows[] = {{"at(cast.level, caster.attack)", 16}, {"at(cast.level, 1, caster.attack + 1)", 33}, {"at(cast.level, 1, 2, min(caster.attack, 3))", 22}};
        for (const auto& row : rows) {
            const auto compiled = CompileExpression(row.text, world.declaration, DescribeType<int32_t>());
            ASSERT_FALSE(compiled.has_value()) << String(row.text).c_str();
            EXPECT_EQ(compiled.error().column, row.column) << String(row.text).c_str() << ": " << compiled.error().message.c_str();
            EXPECT_NE(compiled.error().message.find("a table entry must be a constant"), String::npos) << compiled.error().message;
        }
    }

    TEST(ProgramTableTest, ATableThatIsRefusedAddsNothingToThePool) {
        ValueWorld world;
        const size_t before = ProgramPool::Count();
        EXPECT_FALSE(CompileExpression("at(cast.level, 91, caster.attack)", world.declaration, DescribeType<int32_t>()).has_value());
        EXPECT_FALSE(CompileExpression("at(cast.level, 91, 92.5)", world.declaration, DescribeType<int32_t>()).has_value());
        EXPECT_EQ(ProgramPool::Count(), before);
    }

}  // namespace BECore::Tests
