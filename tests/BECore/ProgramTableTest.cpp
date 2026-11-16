// Tables of a program (Values/Programs/): the LoadTable instruction by hand here, the `at` and `ByLevel` formulas in the sections below.

#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/Printers.h>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

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
        CheckTable<PoolString>({PoolString::Intern("Fire"), PoolString::Intern("Water"), PoolString::Intern("Earth")});
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
                (void)builder.AddConstant(static_cast<int32_t>(i + 100));
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

}  // namespace BECore::Tests
