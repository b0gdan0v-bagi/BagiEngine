#include "ProgramCodegen.h"

#include <BECore/Values/Programs/Opcode.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr size_t kOctalDigits = 3;
        constexpr uint32_t kOctalBase = 8;
        constexpr char kFirstPrintable = ' ';
        constexpr char kLastPrintable = '~';

        /// The instruction as a C++ expression: the compiler folds its register numbers and offsets into the code it makes
        String InstructionLiteral(const Instruction& ins) {
            return Format("Instruction{{Opcode::{}, {}, {}, {}, {}, {}U}}", ins.op, static_cast<uint32_t>(ins.dst), static_cast<uint32_t>(ins.a), static_cast<uint32_t>(ins.b),
                          static_cast<uint32_t>(ins.c), ins.imm);
        }

        /// The text as the inside of a C++ string literal: printable ASCII as it is except `"` and `\`, any other byte as a three-digit octal escape that cannot swallow the next character
        String EscapeLiteral(StringView text) {
            String out;
            out.reserve(text.size());
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out.push_back('\\');
                    out.push_back(c);
                } else if (c >= kFirstPrintable && c <= kLastPrintable) {
                    out.push_back(c);
                } else {
                    auto value = static_cast<uint32_t>(static_cast<uint8_t>(c));
                    char digits[kOctalDigits];
                    for (size_t i = kOctalDigits; i-- > 0;) {
                        digits[i] = static_cast<char>('0' + value % kOctalBase);
                        value /= kOctalBase;
                    }
                    out.push_back('\\');
                    out.append(digits, kOctalDigits);
                }
            }
            return out;
        }

        /// A comment line holds no line break: the name of a content set is text of the caller's
        String SingleLine(StringView text) {
            String out(text.data(), text.size());
            for (char& c : out) {
                if (c == '\n' || c == '\r') {
                    c = ' ';
                }
            }
            return out;
        }

        /// A program of the set with its hash, so the sort hashes each program once
        struct Candidate {
            const Program* program = nullptr;
            uint64_t hash = 0;
        };

        /// Whether the first differs from the second, and in that case whether it is the lesser: the steps of a lexicographic comparison
        template <typename T>
        [[nodiscard]] bool Differs(const T& left, const T& right, bool& less) {
            if (left == right) {
                return false;
            }
            less = left < right;
            return true;
        }

        /// A total order over the fields `SameCode` compares, so two programs of one hash are always in the same order
        [[nodiscard]] bool CodeLess(const Program& left, const Program& right) {
            bool less = false;
            if (Differs(static_cast<uint32_t>(left.ResultType()), static_cast<uint32_t>(right.ResultType()), less) || Differs(left.ResultRegister(), right.ResultRegister(), less) ||
                Differs(left.RegisterCount(), right.RegisterCount(), less) || Differs(left.Reads(), right.Reads(), less) || Differs(left.Code().size(), right.Code().size(), less)) {
                return less;
            }
            for (size_t i = 0; i < left.Code().size(); ++i) {
                const Instruction& l = left.Code()[i];
                const Instruction& r = right.Code()[i];
                if (Differs(static_cast<uint32_t>(l.op), static_cast<uint32_t>(r.op), less) || Differs(l.dst, r.dst, less) || Differs(l.a, r.a, less) || Differs(l.b, r.b, less) ||
                    Differs(l.c, r.c, less) || Differs(l.imm, r.imm, less)) {
                    return less;
                }
            }
            if (Differs(left.Constants().size(), right.Constants().size(), less)) {
                return less;
            }
            for (size_t i = 0; i < left.Constants().size(); ++i) {
                const int order = std::memcmp(left.Constants()[i].bytes.data(), right.Constants()[i].bytes.data(), kProgramRegisterBytes);
                if (order != 0) {
                    return order < 0;
                }
            }
            return false;
        }

        /// The name a program's arrays and function carry: its hash, and the position among programs of the same hash when the hash is not unique
        String ProgramName(uint64_t hash, size_t ordinal) {
            String name = Format("{:016x}", hash);
            if (ordinal > 0) {
                AppendFormat(name, "_{}", ordinal);
            }
            return name;
        }

        void AppendCode(String& out, const Program& program, StringView name) {
            AppendFormat(out, "constexpr Instruction kCode_{}[] = {{\n", name);
            for (const Instruction& ins : program.Code()) {
                AppendFormat(out, "    {},\n", InstructionLiteral(ins));
            }
            out += "};\n";
        }

        void AppendConstants(String& out, const Program& program, StringView name) {
            AppendFormat(out, "constexpr RegisterImage kConstants_{}[] = {{\n", name);
            for (const RegisterImage& image : program.Constants()) {
                out += "    {{{";
                for (size_t i = 0; i < image.bytes.size(); ++i) {
                    if (i > 0) {
                        out += ", ";
                    }
                    AppendFormat(out, "{:#04x}", static_cast<uint32_t>(image.bytes[i]));
                }
                out += "}}},\n";
            }
            out += "};\n";
        }

        /// The function of a program, or false when an instruction has no statement (the program is then left to the interpreter)
        bool AppendFunction(String& out, const Program& program, StringView name, StringView constants) {
            String body;
            for (const Instruction& ins : program.Code()) {
                const String statement = EmitNativeInstruction(ins, constants);
                if (statement.empty()) {
                    ASSERT(false, "GenerateNativeSource: an instruction of a program has no C++ statement");
                    return false;
                }
                AppendFormat(body, "    {}\n", statement);
            }
            // A zero-length array is not C++; no program has no registers, but the text must not depend on that
            const size_t registers = program.RegisterCount() > 0 ? program.RegisterCount() : 1;
            AppendFormat(out, "static void Program_{}([[maybe_unused]] const Context& context, void* result) {{\n", name);
            AppendFormat(out, "    RegisterImage regs[{}];\n", registers);
            out += body;
            AppendFormat(out, "    Kernel::StoreResult(regs, {}, ProgramType::{}, result);\n", static_cast<uint32_t>(program.ResultRegister()), program.ResultType());
            out += "}\n";
            return true;
        }

        void AppendEntry(String& out, const Program& program, uint64_t hash, StringView name, bool hasConstants) {
            AppendFormat(out, "    {{0x{:016x}ULL, ProgramType::{}, {}, {}, {}U, kCode_{}, {}, \"{}\", &Program_{}}},\n", hash, program.ResultType(), static_cast<uint32_t>(program.ResultRegister()),
                         static_cast<uint32_t>(program.RegisterCount()), static_cast<uint32_t>(program.Reads()), name, hasConstants ? Format("kConstants_{}", name) : String("{}"),
                         EscapeLiteral(program.Source().ToStringView()), name);
        }

    }  // namespace

    String EmitNativeInstruction(const Instruction& ins, StringView constants) {
        const String literal = InstructionLiteral(ins);
        switch (ins.op) {
            case Opcode::Invalid:
                return {};
            case Opcode::LoadConst:
                return Format("Kernel::ExecuteLoadConst(regs, {}, {});", literal, constants);
            case Opcode::Copy:
                return Format("Kernel::ExecuteCopy(regs, {});", literal);
            case Opcode::LoadTable:
                return Format("Kernel::ExecuteLoadTable(regs, {}, {});", literal, constants);
            case Opcode::LoadTableSlot:
                return Format("Kernel::ExecuteLoadTableSlot(regs, {}, {}, context);", literal, constants);
            case Opcode::CallDomain:
                return Format("Kernel::ExecuteCallDomain(regs, {}, context);", literal);
#define BE_PROGRAM_EMIT_ROW(Op, Type, Cpp)                                                                                                                                                             \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return Format("Kernel::Execute<Kernel::" #Op ", " #Cpp ">(regs, {}, context);", literal);
                BE_PROGRAM_ROWS(BE_PROGRAM_EMIT_ROW)
#undef BE_PROGRAM_EMIT_ROW
#define BE_PROGRAM_EMIT_FUSED(Op, Type, Cpp)                                                                                                                                                           \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return Format("Kernel::Execute" #Op "<" #Cpp ">(regs, {}, {}, context);", literal, constants);
                BE_PROGRAM_FUSED_ROWS(BE_PROGRAM_EMIT_FUSED)
#undef BE_PROGRAM_EMIT_FUSED
#define BE_PROGRAM_EMIT_MIXED(Op, LName, LCpp, RName, RCpp)                                                                                                                                            \
    case Opcode::Op##LName##RName:                                                                                                                                                                     \
        return Format("Kernel::ExecuteMixed<Kernel::" #Op ", " #LCpp ", " #RCpp ">(regs, {});", literal);
                BE_PROGRAM_MIXED_ROWS(BE_PROGRAM_EMIT_MIXED)
#undef BE_PROGRAM_EMIT_MIXED
#define BE_PROGRAM_EMIT_CONVERSION(Src, SrcCpp, Dst, DstCpp)                                                                                                                                           \
    case Opcode::Src##To##Dst:                                                                                                                                                                         \
        return Format("Kernel::Store<" #DstCpp ">(regs[{}], Kernel::Convert<" #SrcCpp ", " #DstCpp ">(Kernel::Load<" #SrcCpp ">(regs[{}])));", static_cast<uint32_t>(ins.dst),                         \
                      static_cast<uint32_t>(ins.a));
                BE_PROGRAM_CONVERSIONS(BE_PROGRAM_EMIT_CONVERSION)
#undef BE_PROGRAM_EMIT_CONVERSION
        }
        return {};
    }

    String GenerateNativeSource(Span<const ProgramId> programs, const NativeCodegenOptions& options) {
        Vector<Candidate> candidates;
        candidates.reserve(programs.size());
        for (const ProgramId id : programs) {
            const Program& program = ProgramPool::Get(id);
            candidates.push_back(Candidate{&program, program.CodeHash()});
        }
        eastl::sort(candidates.begin(), candidates.end(),
                    [](const Candidate& left, const Candidate& right) { return left.hash != right.hash ? left.hash < right.hash : CodeLess(*left.program, *right.program); });
        candidates.erase(
            eastl::unique(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) { return left.hash == right.hash && left.program->SameCode(*right.program); }),
            candidates.end());

        String out;
        AppendFormat(out, "// Native programs generated for the content set \"{}\". Generated by ProgramCodegen: never edited, regenerate it with the build.\n", SingleLine(options.contentSet));
        out += "#include <BECore/Values/Programs/Kernels.h>\n#include <BECore/Values/Programs/Native/NativePrograms.h>\n\nnamespace BECore {\n";

        String entries;
        size_t ordinal = 0;
        for (size_t i = 0; i < candidates.size(); ++i) {
            const Candidate& candidate = candidates[i];
            ordinal = i > 0 && candidates[i - 1].hash == candidate.hash ? ordinal + 1 : 0;
            const Program& program = *candidate.program;
            const String name = ProgramName(candidate.hash, ordinal);
            const bool hasConstants = !program.Constants().empty();
            const String constants = hasConstants ? Format("kConstants_{}", name) : String("Span<const RegisterImage>{}");

            String text;
            text += "\n";
            AppendCode(text, program, name);
            if (hasConstants) {
                AppendConstants(text, program, name);
            }
            if (!AppendFunction(text, program, name, constants)) {
                continue;
            }
            out += text;
            AppendEntry(entries, program, candidate.hash, name, hasConstants);
        }

        out += "\n";
        if (entries.empty()) {
            out += "const NativeProgramRegistrar kRegistrar{Span<const NativeProgramEntry>{}};\n";
        } else {
            AppendFormat(out, "constexpr NativeProgramEntry kEntries[] = {{\n{}}};\n\nconst NativeProgramRegistrar kRegistrar{{kEntries}};\n", entries);
        }
        out += "\n}  // namespace BECore\n";
        return out;
    }

}  // namespace BECore
