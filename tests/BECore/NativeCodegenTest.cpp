// The generator of native programs (Values/Programs/Native/ProgramCodegen.h): its text is the reference for what a compiled program does, so the cases pin the shape of the
// text, its determinism and that every opcode of the instruction set has a statement. That the text computes what `Run` computes is the equality test over a generated set.

#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/Native/ProgramCodegen.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <EASTL/sort.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kAttackOffset = 4;
        constexpr int32_t kFactor = 3;
        constexpr int32_t kAddend = 7;
        constexpr StringView kSet = "test set";
        constexpr StringView kNoConstants = "Span<const RegisterImage>{}";

        const PoolString kFormula = "caster.attack * 3 + 7"_intern;

        ProgramId InternProgram(const ProgramBuilder& builder, ProgramType type, uint8_t resultRegister, PoolString source = {}) {
            auto program = builder.Finish(type, resultRegister, source);
            EXPECT_TRUE(program.has_value());
            if (!program.has_value()) {
                return 0;
            }
            const Optional<ProgramId> id = ProgramPool::Intern(std::move(*program));
            EXPECT_TRUE(id.has_value());
            return id.value_or(0);
        }

        /// `caster.attack * 3 + 7` as the lowering writes it without fusion
        ProgramId UnfusedFormula(PoolString source = kFormula) {
            ProgramBuilder builder;
            const uint8_t reg = Require(builder.AcquireRegister());
            const uint8_t scratch = Require(builder.AcquireRegister());
            builder.Emit(Opcode::LoadSlotInt32, reg, 0, 0, 0, kAttackOffset);
            builder.EmitLoadConst(scratch, kFactor);
            builder.Emit(Opcode::MulInt32, reg, reg, scratch);
            builder.EmitLoadConst(scratch, kAddend);
            builder.Emit(Opcode::AddInt32, reg, reg, scratch);
            return InternProgram(builder, ProgramType::Int32, reg, source);
        }

        /// The same formula after `Fuse`: one `MulAddSlot` over the constants 0 and 1
        ProgramId FusedFormula() {
            ProgramBuilder builder;
            const uint8_t reg = Require(builder.AcquireRegister());
            const uint32_t factor = builder.AddConstant(kFactor);
            const uint32_t addend = builder.AddConstant(kAddend);
            builder.Emit(Opcode::MulAddSlotInt32, reg, 0, static_cast<uint8_t>(factor), static_cast<uint8_t>(addend), kAttackOffset);
            return InternProgram(builder, ProgramType::Int32, reg, kFormula);
        }

        ProgramId ConstantProgram(int32_t value) {
            ProgramBuilder builder;
            const uint8_t reg = Require(builder.AcquireRegister());
            builder.EmitLoadConst(reg, value);
            return InternProgram(builder, ProgramType::Int32, reg);
        }

        /// `any(list, e, e.flag)` with an early exit over a list of one-byte elements at the offset 8 of level 0, and, when @p nested, a loop over the same list inside it
        ProgramId AnyOfList(bool nested) {
            ListDescriptor list;
            list.offset = 8;
            list.stride = 1;
            list.count = 4;
            ProgramBuilder builder;
            const uint8_t listReg = Require(builder.AcquireRegister());
            const uint8_t element = Require(builder.AcquireRegister());
            const uint8_t answer = Require(builder.AcquireRegister());
            const uint8_t flag = Require(builder.AcquireRegister());
            builder.EmitListSlot(listReg, 0, list);
            builder.EmitLoadConst(answer, false);
            const size_t outer = builder.EmitLoopBegin(element, listReg);
            if (nested) {
                const uint8_t innerElement = Require(builder.AcquireRegister());
                const size_t inner = builder.EmitLoopBegin(innerElement, listReg);
                builder.EmitLoadElem(flag, ProgramType::Bool, innerElement, 0);
                builder.EmitLoopEnd(inner);
                builder.ReleaseRegister(innerElement);
            }
            builder.EmitLoadElem(flag, ProgramType::Bool, element, 0);
            builder.Emit(Opcode::OrBool, answer, answer, flag);
            builder.EmitBreakIf(answer, true);
            builder.EmitLoopEnd(outer);
            return InternProgram(builder, ProgramType::Bool, answer);
        }

        /// The numbers after `goto L` and the numbers of the labels that start a line, each in the order of the text
        struct Jumps {
            Vector<uint32_t> targets;
            Vector<uint32_t> labels;
        };

        Jumps JumpsOf(StringView text) {
            Jumps jumps;
            const auto number = [](StringView digits) {
                uint32_t value = 0;
                for (const char c : digits) {
                    if (c < '0' || c > '9') {
                        break;
                    }
                    value = value * 10 + static_cast<uint32_t>(c - '0');
                }
                return value;
            };
            for (size_t at = text.find("goto L"); at != StringView::npos; at = text.find("goto L", at + 1)) {
                jumps.targets.push_back(number(text.substr(at + 6)));
            }
            for (size_t at = text.find("\nL"); at != StringView::npos; at = text.find("\nL", at + 1)) {
                jumps.labels.push_back(number(text.substr(at + 2)));
            }
            return jumps;
        }

        String Generate(Span<const ProgramId> programs) {
            return GenerateNativeSource(programs, NativeCodegenOptions{kSet});
        }

        String Generate(ProgramId id) {
            return Generate(Span<const ProgramId>(&id, 1));
        }

        String Hex(const ProgramId id) {
            return Format("{:016x}", ProgramPool::Get(id).CodeHash());
        }

        size_t CountOf(StringView text, StringView needle) {
            size_t count = 0;
            for (size_t at = text.find(needle); at != StringView::npos; at = text.find(needle, at + needle.size())) {
                ++count;
            }
            return count;
        }

        /// The hashes of the functions in the order the text defines them
        Vector<String> FunctionHashes(StringView text) {
            constexpr StringView kMark = "static void Program_";
            constexpr size_t kHashDigits = 16;
            Vector<String> hashes;
            for (size_t at = text.find(kMark); at != StringView::npos; at = text.find(kMark, at + kMark.size())) {
                hashes.emplace_back(text.data() + at + kMark.size(), kHashDigits);
            }
            return hashes;
        }

    }  // namespace

    TEST(NativeCodegenTest, AnUnfusedProgramIsOneStatementPerInstructionWithTheInstructionWrittenOut) {
        const ProgramId id = UnfusedFormula();
        const String hex = Hex(id);

        const String text = Generate(id);

        const String expected = Format(R"(// Native programs generated for the content set "test set". Generated by ProgramCodegen: never edited, regenerate it with the build.
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>

namespace BECore {{

constexpr Instruction kCode_{0}[] = {{
    Instruction{{Opcode::LoadSlotInt32, 0, 0, 0, 0, 4U}},
    Instruction{{Opcode::LoadConst, 1, 0, 0, 0, 0U}},
    Instruction{{Opcode::MulInt32, 0, 0, 1, 0, 0U}},
    Instruction{{Opcode::LoadConst, 1, 0, 0, 0, 1U}},
    Instruction{{Opcode::AddInt32, 0, 0, 1, 0, 0U}},
}};
constexpr RegisterImage kConstants_{0}[] = {{
    {{{{{{0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}}}}},
    {{{{{{0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}}}}},
}};
static void Program_{0}([[maybe_unused]] const Context& context, void* result) {{
    RegisterImage regs[2];
    Kernel::Execute<Kernel::LoadSlot, int32_t>(regs, Instruction{{Opcode::LoadSlotInt32, 0, 0, 0, 0, 4U}}, context);
    Kernel::ExecuteLoadConst(regs, Instruction{{Opcode::LoadConst, 1, 0, 0, 0, 0U}}, kConstants_{0});
    Kernel::Execute<Kernel::Mul, int32_t>(regs, Instruction{{Opcode::MulInt32, 0, 0, 1, 0, 0U}}, context);
    Kernel::ExecuteLoadConst(regs, Instruction{{Opcode::LoadConst, 1, 0, 0, 0, 1U}}, kConstants_{0});
    Kernel::Execute<Kernel::Add, int32_t>(regs, Instruction{{Opcode::AddInt32, 0, 0, 1, 0, 0U}}, context);
    Kernel::StoreResult(regs, 0, ProgramType::Int32, result);
}}

constexpr NativeProgramEntry kEntries[] = {{
    {{0x{0}ULL, ProgramType::Int32, 0, 2, 1U, kCode_{0}, kConstants_{0}, "caster.attack * 3 + 7", &Program_{0}}},
}};

const NativeProgramRegistrar kRegistrar{{kEntries}};

}}  // namespace BECore
)",
                                       hex);
        EXPECT_EQ(text, expected);
    }

    TEST(NativeCodegenTest, AFusedProgramCallsTheFusedKernelWithItsConstants) {
        const ProgramId id = FusedFormula();

        const String text = Generate(id);

        EXPECT_NE(text.find("Kernel::ExecuteMulAddSlot<int32_t>(regs, Instruction{Opcode::MulAddSlotInt32, 0, 0, 0, 1, 4U}, kConstants_" + Hex(id) + ", context);"), String::npos) << text;
        EXPECT_NE(text.find("RegisterImage regs[1];"), String::npos);
        EXPECT_EQ(CountOf(text, "Kernel::Execute"), 1U);
    }

    TEST(NativeCodegenTest, TheSameProgramsGiveTheSameBytesInAnyOrderAndOnEveryCall) {
        const ProgramId unfused = UnfusedFormula();
        const ProgramId fused = FusedFormula();
        const ProgramId constant = ConstantProgram(9101);
        const Array<ProgramId, 3> forward{unfused, fused, constant};
        const Array<ProgramId, 3> backward{constant, fused, unfused};

        const String first = Generate(Span<const ProgramId>(forward));

        EXPECT_EQ(Generate(Span<const ProgramId>(forward)), first);
        EXPECT_EQ(Generate(Span<const ProgramId>(backward)), first);
    }

    TEST(NativeCodegenTest, ProgramsAreOrderedByTheirCodeHash) {
        const Array<ProgramId, 4> programs{ConstantProgram(9103), UnfusedFormula(), ConstantProgram(9104), FusedFormula()};
        Vector<String> expected;
        for (const ProgramId id : programs) {
            expected.push_back(Hex(id));
        }
        eastl::sort(expected.begin(), expected.end());

        const Vector<String> hashes = FunctionHashes(Generate(Span<const ProgramId>(programs)));

        EXPECT_EQ(hashes, expected);
    }

    TEST(NativeCodegenTest, EqualProgramsAreWrittenOnceWhateverTheirSourceOrTheirIds) {
        const ProgramId first = UnfusedFormula(kFormula);
        const ProgramId second = UnfusedFormula("caster.attack*3+7"_intern);
        ASSERT_EQ(first, second) << "the pool interns by code";
        const Array<ProgramId, 3> programs{first, second, first};

        const String text = Generate(Span<const ProgramId>(programs));

        EXPECT_EQ(CountOf(text, "static void Program_"), 1U);
        EXPECT_EQ(CountOf(text, "constexpr Instruction kCode_"), 1U);
    }

    TEST(NativeCodegenTest, AProgramOfThirtyTwoRegistersGetsThirtyTwoRegisters) {
        ProgramBuilder builder;
        const uint8_t first = Require(builder.AcquireRegister());
        for (size_t i = 1; i < kMaxProgramRegisters; ++i) {
            static_cast<void>(Require(builder.AcquireRegister()));
        }
        for (size_t reg = 0; reg < kMaxProgramRegisters; ++reg) {
            builder.EmitLoadConst(static_cast<uint8_t>(reg), static_cast<int32_t>(9200 + reg));
        }
        const ProgramId id = InternProgram(builder, ProgramType::Int32, first);

        const String text = Generate(id);

        EXPECT_NE(text.find("RegisterImage regs[32];"), String::npos);
        EXPECT_NE(text.find("Instruction{Opcode::LoadConst, 31, 0, 0, 0, 31U}"), String::npos);
        EXPECT_EQ(CountOf(text, "Kernel::ExecuteLoadConst("), kMaxProgramRegisters);
    }

    TEST(NativeCodegenTest, ATableProgramKeepsItsRunOfConstantsAndTheTableKernel) {
        constexpr uint32_t kEntries = 3;
        ProgramBuilder builder;
        const uint8_t index = Require(builder.AcquireRegister());
        const uint8_t value = Require(builder.AcquireRegister());
        const Array<RegisterImage, kEntries> run{
            RegisterImage{},
            RegisterImage{},
            RegisterImage{},
        };
        const uint32_t first = builder.AddConstantRun(Span<const RegisterImage>(run.data(), run.size()));
        builder.EmitLoadConst(index, 1);
        builder.EmitLoadTable(value, index, first, kEntries);
        const ProgramId id = InternProgram(builder, ProgramType::Int32, value);

        const String text = Generate(id);

        EXPECT_NE(text.find("Kernel::ExecuteLoadTable(regs, Instruction{Opcode::LoadTable, 1, 0, 3, 0, 0U}, kConstants_" + Hex(id) + ");"), String::npos) << text;
        EXPECT_EQ(CountOf(text, "    {{{0x"), kEntries + 1) << "the run and the index constant";
    }

    TEST(NativeCodegenTest, AJumpTargetHasALabelAndNothingElseDoes) {
        for (const bool nested : {false, true}) {
            const ProgramId id = AnyOfList(nested);
            const Program& program = ProgramPool::Get(id);

            const String text = Generate(id);

            const Jumps jumps = JumpsOf(text);
            Vector<uint32_t> expected;
            for (const Instruction& ins : program.Code()) {
                if (IsJump(ins.op)) {
                    expected.push_back(ins.imm);
                }
            }
            EXPECT_EQ(jumps.targets, expected) << text;
            Vector<uint32_t> targets = jumps.targets;
            eastl::sort(targets.begin(), targets.end());
            targets.erase(eastl::unique(targets.begin(), targets.end()), targets.end());
            Vector<uint32_t> labels = jumps.labels;
            eastl::sort(labels.begin(), labels.end());
            EXPECT_EQ(labels, targets) << "a label for each target, once, and none that nothing jumps to (an unused label is a warning)\n" << text;
            EXPECT_NE(text.find(Format("\nL{}:\n    Kernel::StoreResult", program.Code().size())), String::npos) << "a jump to the end of the code lands before the result is stored\n" << text;
        }
    }

    TEST(NativeCodegenTest, AProgramWithoutAJumpHasNoLabel) {
        const String text = Generate(UnfusedFormula());

        EXPECT_TRUE(JumpsOf(text).labels.empty()) << text;
        EXPECT_EQ(CountOf(text, "goto"), 0U);
    }

    TEST(NativeCodegenTest, ALoopProgramGivesTheSameTextOnEveryCall) {
        const ProgramId id = AnyOfList(true);
        const ProgramId plain = AnyOfList(false);
        const Array<ProgramId, 2> forward{id, plain};
        const Array<ProgramId, 2> backward{plain, id};

        const String first = Generate(Span<const ProgramId>(forward));

        EXPECT_EQ(Generate(Span<const ProgramId>(forward)), first);
        EXPECT_EQ(Generate(Span<const ProgramId>(backward)), first);
    }

    TEST(NativeCodegenTest, AnEmptySetIsAFileThatInstallsNothing) {
        const String text = GenerateNativeSource({}, NativeCodegenOptions{kSet});

        EXPECT_EQ(text, R"(// Native programs generated for the content set "test set". Generated by ProgramCodegen: never edited, regenerate it with the build.
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>

namespace BECore {

const NativeProgramRegistrar kRegistrar{Span<const NativeProgramEntry>{}};

}  // namespace BECore
)");
    }

    TEST(NativeCodegenTest, TextThatIsNoCppLiteralIsEscapedInTheSourceAndTheSetName) {
        ProgramBuilder builder;
        const uint8_t reg = Require(builder.AcquireRegister());
        builder.EmitLoadConst(reg, 9302);
        constexpr StringView kAwkwardSource = "a\"b\\c\n\xC3\xA9";
        const ProgramId withSource = InternProgram(builder, ProgramType::Int32, reg, PoolString::Intern(kAwkwardSource));

        const String text = GenerateNativeSource(Span<const ProgramId>(&withSource, 1), NativeCodegenOptions{"line one\nline two"});

        EXPECT_NE(text.find(R"("a\"b\\c\012\303\251")"), String::npos) << text;
        EXPECT_NE(text.find("\"line one line two\""), String::npos) << "the set name stays on one comment line";
    }

    TEST(NativeCodegenTest, EveryOpcodeButInvalidHasAStatement) {
        EXPECT_TRUE(EmitNativeInstruction(Instruction{}, kNoConstants).empty());
        for (const Opcode op : EnumUtils<Opcode>::Values()) {
            if (op == Opcode::Invalid) {
                continue;
            }
            const String statement = EmitNativeInstruction(Instruction{op, 1, 2, 3, 4, 5}, "kConstants");
            EXPECT_FALSE(statement.empty()) << "opcode " << Format("{}", op);
            // A jump is the kernel's answer in an `if`, the target a label of the generated function
            EXPECT_EQ(statement.find(IsJump(op) ? "if (Kernel::" : "Kernel::"), 0U) << statement;
            EXPECT_EQ(statement.back(), ';') << statement;
        }
    }

}  // namespace BECore::Tests
