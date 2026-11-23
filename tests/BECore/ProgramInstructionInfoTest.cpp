// What an instruction reads and writes (Values/Programs/InstructionInfo.h): every opcode of the instruction set is run with registers that differ and the registers it claims to read
// are the registers whose change changes the result, the type it claims to write is the size it really writes. The programs of the compiler are checked for the data flow the table implies.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/ProgramListTypes.h>
#include <BECore/Random/Rng.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/InstructionInfo.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;
    using namespace ProgramListData;

    namespace {

        /// Registers the program loads before the instruction under test: its operands (up to four, a `Pack` of a `Vec4`) and the ones it must never read
        constexpr size_t kLoadedRegisters = 8;
        constexpr size_t kMaxOperands = 4;
        constexpr uint8_t kDst = kLoadedRegisters;
        constexpr int kTrials = 24;
        constexpr int kValueRange = 100;
        constexpr int kSmallIntRange = 10;
        constexpr int kAbsentOutOf = 4;
        constexpr int kByteRange = 255;
        constexpr uint8_t kPoison = 0xFF;
        constexpr uint8_t kPoisonOther = 0xAA;
        constexpr uint8_t kPreloadFirst = 0xAA;
        constexpr uint8_t kPreloadSecond = 0x55;
        constexpr uint8_t kEnumFieldBytes = sizeof(uint32_t);
        constexpr uint32_t kTableEntries = 3;
        constexpr uint32_t kBoolRange = 1;

        /// A register that a kernel reads as a value of a type, or as the element of a list (the register then holds the address of an image of the field)
        struct Operand {
            ProgramType type = ProgramType::Int32;
            bool element = false;
        };

        /// One instruction under test, with what the program around it needs
        struct Case {
            String name;
            Instruction ins;
            Array<Optional<Operand>, kMaxOperands> operands;
            /// The constants that come first in the program, which a table or a fused instruction names by index
            Vector<RegisterImage> constants;
            /// The type of the field the instruction reads at the offset of `ProgramListScratch::bytes` in levels 0 and 1, when it reads one
            Optional<ProgramType> field;
            /// The context holds a `DomainBoard` at level 0 (a registered operation reads it), its walls the field image
            bool board = false;
            /// What the language says the instruction writes, independent of the table under test; empty for an instruction that moves a register
            Optional<ProgramType> expectedResult;
            Optional<ProgramOperation> expectedOperation;
        };

        struct Setup {
            Array<RegisterImage, kLoadedRegisters> regs{};
            RegisterImage field{};
            /// The field of level 1, which is another one so that an instruction over two fields tells them apart
            RegisterImage secondField{};
        };

        /// The operand of register @p k: a loaded register past the last operand slot is no operand
        const Optional<Operand>& OperandAt(const Case& made, size_t k) {
            static const Optional<Operand> kNone;
            return k < made.operands.size() ? made.operands[k] : kNone;
        }

        uint32_t FieldOffset() {
            static const uint32_t offset = [] {
                const ProgramListScratch scratch{};
                return static_cast<uint32_t>(reinterpret_cast<const std::byte*>(scratch.bytes.data()) - reinterpret_cast<const std::byte*>(&scratch));
            }();
            return offset;
        }

        template <typename T>
        void Put(RegisterImage& image, const T& value) {
            std::memcpy(image.bytes.data(), &value, sizeof(T));
        }

        float RandomFloat(Rng& rng) {
            return static_cast<float>(rng.UniformInt(-kValueRange, kValueRange)) * 0.25f;
        }

        /// A valid image of a value of @p type: a bool is 0 or 1, a string a real one, a registered type now and then empty (`any` of an empty mask is another answer)
        RegisterImage MakeImage(ProgramType type, Rng& rng) {
            RegisterImage image{};
            switch (type) {
                case ProgramType::Bool:
                    Put<bool>(image, rng.UniformInt(0, 1) == 1);
                    break;
                case ProgramType::Int32:
                    // Small, so that a cell index of a board lands on the board now and then
                    Put<int32_t>(image, rng.UniformInt(-kSmallIntRange, kSmallIntRange));
                    break;
                case ProgramType::Int64:
                    Put<int64_t>(image, rng.UniformInt(-kValueRange, kValueRange));
                    break;
                case ProgramType::Float:
                    Put<float>(image, RandomFloat(rng));
                    break;
                case ProgramType::Double:
                    Put<double>(image, static_cast<double>(RandomFloat(rng)));
                    break;
                case ProgramType::Vec2:
                case ProgramType::Vec3:
                case ProgramType::Vec4:
                case ProgramType::Quat:
                    for (size_t i = 0; i < ProgramTypeSize(type) / sizeof(float); ++i) {
                        const float lane = RandomFloat(rng);
                        std::memcpy(image.bytes.data() + i * sizeof(float), &lane, sizeof(float));
                    }
                    break;
                case ProgramType::IVec2:
                    Put<IVec2>(image, IVec2{rng.UniformInt(-kValueRange, kValueRange), rng.UniformInt(-kValueRange, kValueRange)});
                    break;
                case ProgramType::Color:
                    for (size_t i = 0; i < ProgramTypeSize(type); ++i) {
                        image.bytes[i] = static_cast<uint8_t>(rng.UniformInt(0, kByteRange));
                    }
                    break;
                case ProgramType::Enum:
                    Put<ProgramEnum>(image, ProgramEnum{static_cast<uint32_t>(rng.UniformInt(0, 1))});
                    break;
                case ProgramType::String: {
                    const Array<PoolString, 3> strings{"alpha"_intern, "beta"_intern, "gamma"_intern};
                    Put<PoolString>(image, strings[static_cast<size_t>(rng.UniformInt(0, 2))]);
                    break;
                }
                case ProgramType::Domain:
                    if (rng.UniformInt(0, kAbsentOutOf - 1) != 0) {
                        for (uint8_t& byte : image.bytes) {
                            byte = static_cast<uint8_t>(rng.UniformInt(0, kByteRange));
                        }
                    }
                    break;
            }
            return image;
        }

        RegisterImage Filled(uint8_t byte) {
            RegisterImage image;
            image.bytes.fill(byte);
            return image;
        }

        /// The bytes of a field of the type that a load names: a registered type is its whole register, an enum is an index of four bytes, no other load has one
        uint8_t FieldBytes(ProgramType type) {
            if (type == ProgramType::Domain) {
                return static_cast<uint8_t>(ProgramTypeSize(ProgramType::Domain));
            }
            return type == ProgramType::Enum ? kEnumFieldBytes : 0;
        }

        /// The type the language gives the result of @p operation over operands of @p operand: a comparison and a logical operation a `bool`, a length and a dot product a `float`, a
        /// component its own type (an `int` for a color's channel and an integer vector), everything else the type of its operands
        ProgramType ExpectedResult(ProgramOperation operation, ProgramType operand) {
            switch (operation) {
                case ProgramOperation::Eq:
                case ProgramOperation::Ne:
                case ProgramOperation::Lt:
                case ProgramOperation::Le:
                case ProgramOperation::Gt:
                case ProgramOperation::Ge:
                case ProgramOperation::And:
                case ProgramOperation::Or:
                case ProgramOperation::Not:
                    return ProgramType::Bool;
                case ProgramOperation::Dot:
                case ProgramOperation::Length:
                    return ProgramType::Float;
                case ProgramOperation::Extract:
                    return operand == ProgramType::IVec2 || operand == ProgramType::Color ? ProgramType::Int32 : ProgramType::Float;
                default:
                    return operand;
            }
        }

        /// A rotation turns a vector, a number scales one, a vector keeps its own type
        ProgramType ExpectedMixedResult(ProgramType left, ProgramType right) {
            if (left == ProgramType::Quat) {
                return right;
            }
            return left == ProgramType::Float || left == ProgramType::Int32 ? right : left;
        }

        Case MakeCase(Opcode op) {
            Case made;
            made.name = String(EnumUtils<Opcode>::ToString(op));
            made.ins.op = op;
            made.ins.a = 0;
            made.ins.b = 1;
            made.ins.c = 2;
            return made;
        }

        template <typename Op, typename T>
        void AddRow(Vector<Case>& cases, Opcode opcode, ProgramOperation operation) {
            constexpr ProgramType kType = kProgramTypeOf<T>;
            Case made = MakeCase(opcode);
            made.expectedOperation = operation;
            made.expectedResult = ExpectedResult(operation, kType);
            if constexpr (Op::kShape == Kernel::OpShape::Slot) {
                made.field = kType;
                made.ins.a = 0;
                made.ins.b = FieldBytes(kType);
                made.ins.imm = FieldOffset();
            } else if constexpr (Op::kShape == Kernel::OpShape::Element) {
                made.operands[0] = Operand{kType, true};
                made.ins.b = FieldBytes(kType);
            } else if constexpr (Op::kShape == Kernel::OpShape::Unary || Op::kShape == Kernel::OpShape::Extract) {
                made.operands[0] = Operand{kType};
            } else if constexpr (Op::kShape == Kernel::OpShape::Binary || Op::kShape == Kernel::OpShape::Compare) {
                made.operands[0] = Operand{kType};
                made.operands[1] = Operand{kType};
            } else if constexpr (Op::kShape == Kernel::OpShape::Ternary) {
                made.operands[0] = Operand{kType};
                made.operands[1] = Operand{kType};
                made.operands[2] = Operand{kType};
            } else if constexpr (Op::kShape == Kernel::OpShape::Select) {
                made.operands[0] = Operand{ProgramType::Bool};
                made.operands[1] = Operand{kType};
                made.operands[2] = Operand{kType};
            } else if constexpr (Op::kShape == Kernel::OpShape::Blend) {
                made.operands[0] = Operand{kType};
                made.operands[1] = Operand{kType};
                made.operands[2] = Operand{kProgramTypeOf<typename Op::template Weight<T>>};
            } else {
                static_assert(Op::kShape == Kernel::OpShape::Pack);
                for (uint32_t i = 0; i < ComponentTraits<T>::kCount; ++i) {
                    made.operands[i] = Operand{kProgramTypeOf<typename ComponentTraits<T>::Value>};
                }
            }
            cases.push_back(std::move(made));
        }

        void AddMixed(Vector<Case>& cases, Opcode opcode, ProgramOperation operation, ProgramType left, ProgramType right) {
            Case made = MakeCase(opcode);
            made.expectedOperation = operation;
            made.expectedResult = ExpectedMixedResult(left, right);
            made.operands[0] = Operand{left};
            made.operands[1] = Operand{right};
            cases.push_back(std::move(made));
        }

        void AddConversion(Vector<Case>& cases, Opcode opcode, ProgramType from, ProgramType to) {
            Case made = MakeCase(opcode);
            made.expectedResult = to;
            made.operands[0] = Operand{from};
            cases.push_back(std::move(made));
        }

        void AddFused(Vector<Case>& cases, Opcode opcode, ProgramType type) {
            Rng rng(static_cast<uint64_t>(opcode));
            Case made = MakeCase(opcode);
            made.expectedResult = type;
            made.field = type;
            if (IsMulAddSlot(opcode)) {
                // The factor of a `Vec3` is a `float`
                made.constants.push_back(MakeImage(type == ProgramType::Vec3 ? ProgramType::Float : type, rng));
                made.constants.push_back(MakeImage(type, rng));
                made.ins.a = 0;
                made.ins.b = 0;
                made.ins.c = 1;
                made.ins.imm = FieldOffset();
            } else {
                // `LerpSlots`: the fields of levels 0 and 1 and the weight in register 0
                made.operands[0] = Operand{type};
                made.ins.a = 0;
                made.ins.b = 1;
                made.ins.c = 0;
                made.ins.imm = PackHalves(FieldOffset(), FieldOffset());
            }
            cases.push_back(std::move(made));
        }

        Vector<RegisterImage> TableOf(Rng& rng) {
            Vector<RegisterImage> table;
            for (uint32_t i = 0; i < kTableEntries; ++i) {
                table.push_back(MakeImage(ProgramType::Int32, rng));
            }
            return table;
        }

        void AddMoves(Vector<Case>& cases) {
            Rng rng(1);
            Case constant = MakeCase(Opcode::LoadConst);
            constant.constants.push_back(MakeImage(ProgramType::Vec3, rng));
            constant.ins.imm = 0;
            cases.push_back(std::move(constant));

            Case copy = MakeCase(Opcode::Copy);
            copy.operands[0] = Operand{ProgramType::Vec3};
            cases.push_back(std::move(copy));

            Case table = MakeCase(Opcode::LoadTable);
            table.constants = TableOf(rng);
            table.operands[0] = Operand{ProgramType::Int32};
            table.ins.imm = 0;
            SetTableEntryCount(table.ins, kTableEntries);
            cases.push_back(std::move(table));

            Case tableSlot = MakeCase(Opcode::LoadTableSlot);
            tableSlot.constants = TableOf(rng);
            tableSlot.field = ProgramType::Int32;
            tableSlot.ins.a = 0;
            tableSlot.ins.imm = PackHalves(FieldOffset(), 0);
            SetTableEntryCount(tableSlot.ins, kTableEntries);
            cases.push_back(std::move(tableSlot));
        }

        /// A call of each operation the toys of `ProgramDomainTypes.h` registered: its registers are the parameters it has, of the types it declares
        void AddCalls(Vector<Case>& cases) {
            const DomainToys& toys = RegisterDomainToys();
            for (const DomainOperationId id :
                 {toys.bitsAnd, toys.bitsOr, toys.majority, toys.shift, toys.popcount, toys.bit, toys.cell, toys.any, toys.walls, toys.wallsOfRow, toys.pairOf, toys.weightCount}) {
                const DomainOperation& operation = ProgramDomain::Operation(id);
                Case made = MakeCase(Opcode::CallDomain);
                made.name = Format("CallDomain {}", operation.name.ToStringView());
                made.ins.imm = id;
                made.board = true;
                made.field = ProgramType::Domain;
                made.expectedResult = operation.result.type;
                for (size_t i = 0; i < operation.parameterCount; ++i) {
                    made.operands[i] = Operand{operation.parameters[i].type};
                }
                cases.push_back(std::move(made));
            }
        }

        const Vector<Case>& AllCases() {
            static const Vector<Case> cases = [] {
                Vector<Case> built;
#define BE_INFO_CASE_ROW(Op, Type, Cpp) AddRow<Kernel::Op, Cpp>(built, Opcode::Op##Type, ProgramOperation::Op);
                BE_PROGRAM_ROWS(BE_INFO_CASE_ROW)
#undef BE_INFO_CASE_ROW
#define BE_INFO_CASE_MIXED(Op, LName, LCpp, RName, RCpp) AddMixed(built, Opcode::Op##LName##RName, ProgramOperation::Op, ProgramType::LName, ProgramType::RName);
                BE_PROGRAM_MIXED_ROWS(BE_INFO_CASE_MIXED)
#undef BE_INFO_CASE_MIXED
#define BE_INFO_CASE_CONVERSION(Src, SrcCpp, Dst, DstCpp) AddConversion(built, Opcode::Src##To##Dst, ProgramType::Src, ProgramType::Dst);
                BE_PROGRAM_CONVERSIONS(BE_INFO_CASE_CONVERSION)
#undef BE_INFO_CASE_CONVERSION
#define BE_INFO_CASE_FUSED(Op, Type, Cpp) AddFused(built, Opcode::Op##Type, ProgramType::Type);
                BE_PROGRAM_FUSED_ROWS(BE_INFO_CASE_FUSED)
#undef BE_INFO_CASE_FUSED
                AddMoves(built);
                AddCalls(built);
                return built;
            }();
            return cases;
        }

        /// The program of one case: the registers filled from @p setup, the destination filled with @p preload, the instruction, and the register image it writes
        RegisterImage RunCase(const Case& made, const Setup& setup, uint8_t preload) {
            // An element register holds the address of an image of the field, which lives as long as the run
            const Array<RegisterImage, kLoadedRegisters> elements = setup.regs;
            ProgramBuilder builder;
            Array<uint8_t, kLoadedRegisters + 1> registers{};
            for (uint8_t& reg : registers) {
                reg = Require(builder.AcquireRegister());
            }
            if (!made.constants.empty()) {
                const uint32_t first = builder.AddConstantRun(Span<const RegisterImage>(made.constants.data(), made.constants.size()));
                EXPECT_EQ(first, 0U) << made.name.c_str() << ": the constants of the case come first";
            }
            for (size_t k = 0; k < kLoadedRegisters; ++k) {
                RegisterImage image = setup.regs[k];
                if (OperandAt(made, k).has_value() && OperandAt(made, k)->element) {
                    const ElementImage element{elements[k].bytes.data(), 0, 0};
                    image = RegisterImage{};
                    std::memcpy(image.bytes.data(), &element, sizeof(element));
                }
                builder.Emit(Opcode::LoadConst, registers[k], 0, 0, 0, builder.AddConstant(image));
            }
            builder.Emit(Opcode::LoadConst, kDst, 0, 0, 0, builder.AddConstant(Filled(preload)));
            Instruction ins = made.ins;
            ins.dst = kDst;
            builder.Emit(ins);
            const auto finished = builder.Finish(ProgramType::Domain, kDst);
            EXPECT_TRUE(finished.has_value()) << made.name.c_str() << ": " << (finished.has_value() ? "" : finished.error().message.c_str());
            if (!finished.has_value()) {
                return {};
            }

            ProgramListScratch scratch;
            ProgramListScratch second;
            std::memcpy(scratch.bytes.data(), setup.field.bytes.data(), sizeof(scratch.bytes));
            std::memcpy(second.bytes.data(), setup.secondField.bytes.data(), sizeof(second.bytes));
            DomainBoard board;
            std::memcpy(&board.walls, setup.field.bytes.data(), sizeof(board.walls));
            Context context;
            if (made.board) {
                context.Set(0, board);
            } else {
                context.Set(0, scratch);
                context.Set(1, second);
            }
            RegisterImage result{};
            Run(*finished, context, result.bytes.data());
            return result;
        }

        Setup MakeSetup(const Case& made, Rng& rng) {
            Setup setup;
            for (size_t k = 0; k < kLoadedRegisters; ++k) {
                setup.regs[k] = OperandAt(made, k).has_value() ? MakeImage(OperandAt(made, k)->type, rng) : Filled(kPoison);
            }
            if (made.field.has_value()) {
                setup.field = MakeImage(*made.field, rng);
                setup.secondField = MakeImage(*made.field, rng);
            }
            return setup;
        }

        /// What register @p k holds instead: a register the case does not use gets other garbage; an operand a fresh value, or the value of another operand of its kind (so that `Eq` can
        /// turn true) when @p copy
        RegisterImage Replacement(const Case& made, size_t k, const Setup& base, Rng& rng, bool copy) {
            if (!OperandAt(made, k).has_value()) {
                return Filled(kPoisonOther);
            }
            if (copy) {
                for (size_t j = 0; j < kLoadedRegisters; ++j) {
                    if (j != k && OperandAt(made, j).has_value() && OperandAt(made, j)->type == OperandAt(made, k)->type && OperandAt(made, j)->element == OperandAt(made, k)->element) {
                        return base.regs[j];
                    }
                }
            }
            return MakeImage(OperandAt(made, k)->type, rng);
        }

        RegisterMask OperandMask(const Case& made) {
            RegisterMask mask = 0;
            for (size_t k = 0; k < kLoadedRegisters && k < made.operands.size(); ++k) {
                mask |= made.operands[k].has_value() ? RegisterBit(k) : 0;
            }
            return mask;
        }

        /// The type of the value each operand register is read as: what the case was built from, which a constant learns when an instruction reads it. A move reads a register of any type, and
        /// a registered operation names its own arguments
        void CheckOperandTypes(const Case& made) {
            const Opcode op = made.ins.op;
            if (op == Opcode::Copy || op == Opcode::CallDomain || IsListInstruction(op)) {
                return;
            }
            for (size_t k = 0; k < kLoadedRegisters; ++k) {
                const bool value = k < made.operands.size() && made.operands[k].has_value() && !made.operands[k]->element;
                const Optional<ProgramType> want = value ? Optional<ProgramType>{made.operands[k]->type} : std::nullopt;
                EXPECT_EQ(OperandTypeOf(made.ins, static_cast<uint32_t>(k)), want) << made.name.c_str() << ": register " << k;
            }
        }

        void CheckCase(const Case& made) {
            Case probe = made;
            probe.ins.dst = kDst;
            const InstructionInfo info = DescribeInstruction(probe.ins);

            Rng rng(static_cast<uint64_t>(made.ins.op) + 1);
            RegisterMask moved = 0;
            for (int trial = 0; trial < kTrials; ++trial) {
                const Setup base = MakeSetup(made, rng);
                const RegisterImage want = RunCase(made, base, kPreloadFirst);
                for (size_t k = 0; k < kLoadedRegisters; ++k) {
                    for (const bool copy : {false, true}) {
                        Setup other = base;
                        other.regs[k] = Replacement(made, k, base, rng, copy);
                        if (!(RunCase(made, other, kPreloadFirst) == want)) {
                            moved |= RegisterBit(k);
                        }
                    }
                }
            }
            EXPECT_EQ(moved & ~info.reads, 0U) << made.name.c_str() << ": a register it does not list as read changed the result";
            EXPECT_EQ(info.reads & ~moved, 0U) << made.name.c_str() << ": a register it lists as read never changed the result";
            EXPECT_EQ(info.reads & ~OperandMask(made), 0U) << made.name.c_str() << ": it lists a register that is no operand";
            EXPECT_EQ(info.operation, made.expectedOperation) << made.name.c_str();
            EXPECT_EQ(info.resultType, made.expectedResult) << made.name.c_str();
            CheckOperandTypes(probe);

            // The bytes it writes are the size of its type: the rest of the register keeps what was there, whatever that was
            Rng sizeRng(static_cast<uint64_t>(made.ins.op) + 1);
            const Setup setup = MakeSetup(made, sizeRng);
            const RegisterImage first = RunCase(made, setup, kPreloadFirst);
            const RegisterImage second = RunCase(made, setup, kPreloadSecond);
            const size_t written = info.resultType.has_value() ? ProgramTypeSize(*info.resultType) : kProgramRegisterBytes;
            for (size_t i = 0; i < kProgramRegisterBytes; ++i) {
                if (i < written) {
                    EXPECT_EQ(first.bytes[i], second.bytes[i]) << made.name.c_str() << ": byte " << i << " is not written by the instruction, but its type says " << written << " bytes";
                } else {
                    EXPECT_EQ(first.bytes[i], kPreloadFirst) << made.name.c_str() << ": byte " << i << " is written, but its type says " << written << " bytes";
                    EXPECT_EQ(second.bytes[i], kPreloadSecond) << made.name.c_str() << ": byte " << i << " is written, but its type says " << written << " bytes";
                }
            }
        }

        /// The data flow of a compiled program under the table: nothing reads a register nothing wrote, and the last instruction gives the type the checker gave the program
        void CheckFlow(const Program& program, StringView text) {
            RegisterMask written = 0;
            const String where(text);
            for (const Instruction& ins : program.Code()) {
                const InstructionInfo info = DescribeInstruction(ins);
                EXPECT_EQ(info.reads & ~written, 0U) << where.c_str() << ": " << String(EnumUtils<Opcode>::ToString(ins.op)).c_str() << " reads a register nothing wrote";
                if (ins.op != Opcode::BreakIf) {
                    written |= RegisterBit(ins.dst);
                }
            }
            const Instruction& last = program.Code().back();
            const InstructionInfo lastInfo = DescribeInstruction(last);
            if (last.dst == program.ResultRegister() && lastInfo.resultType.has_value() && last.op != Opcode::LoopEnd) {
                EXPECT_EQ(*lastInfo.resultType, program.ResultType()) << where.c_str() << ": the last instruction writes the result";
            }
        }

        template <typename T>
        void CheckFormula(StringView text, const ContextDecl& declaration) {
            for (const bool fuse : {false, true}) {
                CompileOptions options;
                options.fuse = fuse;
                const auto compiled = CompileValue<T>(text, declaration, options);
                ASSERT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? "" : compiled.error().message.c_str());
                ASSERT_EQ(compiled->Kind(), ValueKind::Program) << String(text).c_str();
                CheckFlow(ProgramPool::Get(compiled->Id()), text);
            }
        }

    }  // namespace

    TEST(ProgramInstructionInfoTest, EveryRowReadsExactlyTheRegistersItsChangeMoves) {
        for (const Case& made : AllCases()) {
            CheckCase(made);
        }
    }

    TEST(ProgramInstructionInfoTest, EveryOpcodeHasACaseOrIsAListInstruction) {
        Vector<Opcode> covered;
        for (const Case& made : AllCases()) {
            covered.push_back(made.ins.op);
        }
        for (const Opcode op : EnumUtils<Opcode>::Values()) {
            const bool listed = IsListInstruction(op) || op == Opcode::Invalid;
            const bool has = std::find(covered.begin(), covered.end(), op) != covered.end();
            EXPECT_TRUE(has || listed) << String(EnumUtils<Opcode>::ToString(op)).c_str() << " has no case";
            EXPECT_FALSE(has && listed) << String(EnumUtils<Opcode>::ToString(op)).c_str() << " is a list instruction with a case";
        }
    }

    TEST(ProgramInstructionInfoTest, TheListAndLoopInstructionsReadTheirListsElementsAndIndexes) {
        constexpr uint8_t kDest = 5;
        constexpr uint8_t kList = 1;
        constexpr uint8_t kIndex = 2;
        const auto describe = [](Opcode op) { return DescribeInstruction(Instruction{op, kDest, kList, kIndex, 3, 7}); };

        EXPECT_EQ(describe(Opcode::ListSlot).reads, 0U) << "its `a` is a level, not a register";
        EXPECT_EQ(describe(Opcode::ListElem).reads, RegisterBit(kList));
        EXPECT_EQ(describe(Opcode::ListSize).reads, RegisterBit(kList));
        EXPECT_EQ(describe(Opcode::ListAt).reads, RegisterBit(kList) | RegisterBit(kIndex));
        EXPECT_EQ(describe(Opcode::LoopBegin).reads, RegisterBit(kList));
        EXPECT_EQ(describe(Opcode::LoopEnd).reads, RegisterBit(kList) | RegisterBit(kDest)) << "the next element is computed from the current one";
        EXPECT_EQ(describe(Opcode::BreakIf).reads, RegisterBit(kList));

        EXPECT_EQ(describe(Opcode::ListSize).resultType, ProgramType::Int32);
        for (const Opcode op : {Opcode::ListSlot, Opcode::ListElem, Opcode::ListAt, Opcode::LoopBegin, Opcode::LoopEnd, Opcode::BreakIf}) {
            EXPECT_FALSE(describe(op).resultType.has_value()) << String(EnumUtils<Opcode>::ToString(op)).c_str() << " writes a list or an element image";
        }
        for (const Opcode op : {Opcode::ListSlot, Opcode::ListElem, Opcode::ListSize, Opcode::ListAt, Opcode::LoopBegin, Opcode::LoopEnd, Opcode::BreakIf}) {
            EXPECT_FALSE(describe(op).operation.has_value()) << String(EnumUtils<Opcode>::ToString(op)).c_str();
        }
    }

    TEST(ProgramInstructionInfoTest, ACallOfAnUnregisteredOperationReadsEveryRegisterAndWritesNoKnownType) {
        const InstructionInfo info = DescribeInstruction(Instruction{Opcode::CallDomain, 3, 0, 1, 2, static_cast<uint32_t>(ProgramDomain::OperationCount()) + 100});

        EXPECT_EQ(info.reads, ~RegisterMask{0}) << "the peephole pass stays blind to what it cannot see";
        EXPECT_FALSE(info.resultType.has_value());
        EXPECT_FALSE(info.operation.has_value());
    }

    TEST(ProgramInstructionInfoTest, ARegisterBeyondTheLastIsNoBit) {
        EXPECT_EQ(RegisterBit(kMaxProgramRegisters), 0U);
        EXPECT_EQ(RegisterBit(kMaxProgramRegisters - 1), RegisterMask{1} << (kMaxProgramRegisters - 1));
    }

    TEST(ProgramInstructionInfoTest, TheProgramsOfTheCompilerHaveTheDataFlowTheTableSays) {
        ContextDecl hand;
        ASSERT_TRUE(hand.Add<ProgramListHand>("hand").has_value());
        CheckFormula<int32_t>("hand.before * 3 + hand.after", hand);
        CheckFormula<int32_t>("count(hand.tiles, t, t.terminal)", hand);
        CheckFormula<int32_t>("sum(hand.tiles, t, t.rank)", hand);
        CheckFormula<int32_t>("sum(hand.sets, s, count(s.tiles, t, t.terminal))", hand);
        CheckFormula<bool>("any(hand.tiles, t, t.rank > 17)", hand);
        CheckFormula<bool>("all(hand.tiles, t, t.suit == hand.tiles[0].suit)", hand);
        CheckFormula<bool>("hand.tiles[3].suit == Winds", hand);
        CheckFormula<float>("sum(hand.tiles, t, t.weight)", hand);
        CheckFormula<float>("lerp(hand.tiles[0].weight, hand.tiles[1].weight, 0.25)", hand);
        CheckFormula<double>("max(hand.tiles, t, double(t.weight))", hand);
        CheckFormula<int32_t>("hand.counts[1] + hand.counts[2] * 2", hand);

        const DomainToys& toys = RegisterDomainToys();
        static_cast<void>(toys);
        ContextDecl board;
        ASSERT_TRUE(board.Add<DomainBoard>("board").has_value());
        CheckFormula<DomainBits256>("or_mask(cell(board.turn, 4), shift(cell(board.turn, 4), East))", board);
        CheckFormula<DomainBits256>("and_mask(walls(), shift(cell(board.turn, 4), East))", board);
        CheckFormula<int32_t>("popcount(or_mask(cell(3, 4), cell(5, 6))) + board.turn", board);
        CheckFormula<bool>("any(and_mask(walls(), cell(1, 2)))", board);
    }

}  // namespace BECore::Tests
