#include "NativeProgramSetRows.h"

#include <BECore/ProgramEdgeValues.h>
#include <BECore/ProgramRowTypes.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <NativePrograms/NativeProgramSet.h>

namespace BECore::Tests {

    using ProgramRowData::ProgramRowOperands;

    namespace {

        constexpr size_t kLoadConstSamples = 3;
        constexpr uint32_t kShortTableEntries = 5;
        constexpr uint32_t kLongTableEntries = 300;
        constexpr int32_t kLongTableStep = 3;

        /// An object to take the offsets of fields from: `offsetof` warns on a reflected class; the address of the field in an object is the same number
        const ProgramRowOperands& Probe() {
            static const ProgramRowOperands probe;
            return probe;
        }

        template <typename Field>
        uint32_t Offset(const Field& field) {
            return static_cast<uint32_t>(reinterpret_cast<const std::byte*>(&field) - reinterpret_cast<const std::byte*>(&Probe()));
        }

        /// The offset of the field of @p T the programs read, the second one (`i32b`, `f32b`) where a type has two. The order of the branches is the order of `BE_PROGRAM_TYPES`
        template <typename T>
        uint32_t FieldOffset(bool second = false) {
            if constexpr (std::is_same_v<T, bool>) {
                return Offset(Probe().flag);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                return second ? Offset(Probe().i32b) : Offset(Probe().i32);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return Offset(Probe().i64);
            } else if constexpr (std::is_same_v<T, float>) {
                return second ? Offset(Probe().f32b) : Offset(Probe().f32);
            } else if constexpr (std::is_same_v<T, double>) {
                return Offset(Probe().f64);
            } else if constexpr (std::is_same_v<T, Vec2>) {
                return Offset(Probe().v2);
            } else if constexpr (std::is_same_v<T, Vec3>) {
                return Offset(Probe().v3);
            } else if constexpr (std::is_same_v<T, Vec4>) {
                return Offset(Probe().v4);
            } else if constexpr (std::is_same_v<T, IVec2>) {
                return Offset(Probe().iv2);
            } else if constexpr (std::is_same_v<T, Quat>) {
                return Offset(Probe().quat);
            } else if constexpr (std::is_same_v<T, Color>) {
                return Offset(Probe().color);
            } else if constexpr (std::is_same_v<T, PoolString>) {
                return Offset(Probe().text);
            } else {
                static_assert(std::is_same_v<T, ProgramEnum>, "a field for every program type");
                return Offset(Probe().wide32);
            }
        }

        /// The size of the enum field `FieldOffset<ProgramEnum>` names
        constexpr uint8_t kEnumFieldBytes = sizeof(ValueTestData::ValueWideEnum);

        /// Assembles one program: registers are taken in order and never given back, so the numbers in the code are the order of the calls
        class RowAssembler {
        public:
            explicit RowAssembler(String name) : _name(std::move(name)) {}

            [[nodiscard]] uint8_t Acquire() {
                return _builder.AcquireRegister().value_or(0);
            }

            /// A register loaded from the field of @p T at @p level
            template <typename T>
            [[nodiscard]] uint8_t Load(LevelIndex level, bool second = false) {
                const uint8_t reg = Acquire();
                if constexpr (std::is_same_v<T, ProgramEnum>) {
                    _builder.EmitLoadSlot(reg, ProgramType::Enum, SlotRef{level, FieldOffset<T>()}, kEnumFieldBytes);
                } else {
                    _builder.EmitLoadSlot(reg, kProgramTypeOf<T>, SlotRef{level, FieldOffset<T>(second)});
                }
                return reg;
            }

            /// A register loaded from the enum field of @p bytes bytes at @p offset
            [[nodiscard]] uint8_t LoadEnum(LevelIndex level, uint32_t offset, uint8_t bytes) {
                const uint8_t reg = Acquire();
                _builder.EmitLoadSlot(reg, ProgramType::Enum, SlotRef{level, offset}, bytes);
                return reg;
            }

            [[nodiscard]] ProgramBuilder& Builder() {
                return _builder;
            }

            /// Interns the program with its result in @p resultRegister and appends it to @p programs unless it is there already
            std::expected<void, String> Finish(ProgramType resultType, uint8_t resultRegister, Vector<ProgramId>& programs) {
                auto program = _builder.Finish(resultType, resultRegister, PoolString::Intern(_name));
                if (!program.has_value()) {
                    return std::unexpected(Format("{}: {}", _name, program.error().message));
                }
                const Optional<ProgramId> id = ProgramPool::Intern(std::move(*program));
                if (!id.has_value()) {
                    return std::unexpected(Format("{}: the program pool is full", _name));
                }
                if (std::find(programs.begin(), programs.end(), *id) == programs.end()) {
                    programs.push_back(*id);
                }
                return {};
            }

        private:
            String _name;
            ProgramBuilder _builder;
        };

        /// The first error of a run of steps: a set with a hole would generate a file that quietly lacks it
        class Steps {
        public:
            void Add(std::expected<void, String> result) {
                if (!result.has_value() && _error.empty()) {
                    _error = std::move(result.error());
                }
            }

            [[nodiscard]] std::expected<void, String> Result() {
                if (!_error.empty()) {
                    return std::unexpected(std::move(_error));
                }
                return {};
            }

        private:
            String _error;
        };

        /// The operands of the three-level row: the first at A, the second at B, the third at C
        constexpr LevelIndex kLevels[] = {kOperandsLevelA, kOperandsLevelB, kOperandsLevelC};

        /// One instruction of a typed row. The shape of the operation says how many operands it reads and where they come from
        template <typename Op, typename T>
        std::expected<void, String> AssembleRow(StringView name, Opcode opcode, Vector<ProgramId>& programs) {
            constexpr Kernel::OpShape kShape = Op::kShape;
            using Result = Kernel::ResultOf<Op, T>;
            RowAssembler assembler{String(name)};
            ProgramBuilder& builder = assembler.Builder();

            if constexpr (kShape == Kernel::OpShape::Slot) {
                if constexpr (std::is_same_v<T, ProgramEnum>) {
                    // One program per width of the field; the one of the type's own field is the first
                    Steps steps;
                    const uint32_t offsets[] = {Offset(Probe().narrow), Offset(Probe().wide16), Offset(Probe().wide32), Offset(Probe().enum64)};
                    const uint8_t sizes[] = {sizeof(ValueTestData::ValueNarrowEnum), sizeof(ValueTestData::ValueShortEnum), sizeof(ValueTestData::ValueWideEnum), sizeof(uint64_t)};
                    for (size_t i = 0; i < std::size(offsets); ++i) {
                        RowAssembler widthAssembler{Format("{}{}", name, sizes[i])};
                        const uint8_t reg = widthAssembler.LoadEnum(kOperandsLevelA, offsets[i], sizes[i]);
                        steps.Add(widthAssembler.Finish(ProgramType::Enum, reg, programs));
                    }
                    return steps.Result();
                } else {
                    const uint8_t reg = assembler.Load<T>(kOperandsLevelA);
                    return assembler.Finish(kProgramTypeOf<T>, reg, programs);
                }
            } else if constexpr (kShape == Kernel::OpShape::Element) {
                // The element is the operands object of level A taken as a list of one; the index is the `i32` of level B, so the element is there when it is 0 and missing in the other cases,
                // which is the whole of what `LoadElem` does. The field of an enum is read at each width, as the slot load is
                Steps steps;
                const auto assemble = [&](StringView programName, uint32_t offset, uint8_t enumBytes) {
                    RowAssembler elementAssembler{String(programName)};
                    ListDescriptor list;
                    list.stride = sizeof(ProgramRowOperands);
                    list.count = 1;
                    const uint8_t listReg = elementAssembler.Acquire();
                    const uint8_t index = elementAssembler.Load<int32_t>(kOperandsLevelB);
                    const uint8_t element = elementAssembler.Acquire();
                    const uint8_t dst = elementAssembler.Acquire();
                    elementAssembler.Builder().EmitListSlot(listReg, kOperandsLevelA, list);
                    elementAssembler.Builder().EmitListAt(element, listReg, index);
                    elementAssembler.Builder().EmitLoadElem(dst, kProgramTypeOf<T>, element, offset, enumBytes);
                    steps.Add(elementAssembler.Finish(kProgramTypeOf<T>, dst, programs));
                };
                if constexpr (std::is_same_v<T, ProgramEnum>) {
                    const uint32_t offsets[] = {Offset(Probe().narrow), Offset(Probe().wide16), Offset(Probe().wide32), Offset(Probe().enum64)};
                    const uint8_t sizes[] = {sizeof(ValueTestData::ValueNarrowEnum), sizeof(ValueTestData::ValueShortEnum), sizeof(ValueTestData::ValueWideEnum), sizeof(uint64_t)};
                    for (size_t i = 0; i < std::size(offsets); ++i) {
                        assemble(Format("{}{}", name, sizes[i]), offsets[i], sizes[i]);
                    }
                } else {
                    assemble(name, FieldOffset<T>(), 0);
                }
                return steps.Result();
            } else if constexpr (kShape == Kernel::OpShape::Unary) {
                const uint8_t a = assembler.Load<T>(kLevels[0]);
                const uint8_t dst = assembler.Acquire();
                builder.Emit(opcode, dst, a);
                return assembler.Finish(kProgramTypeOf<Result>, dst, programs);
            } else if constexpr (kShape == Kernel::OpShape::Binary || kShape == Kernel::OpShape::Compare) {
                const uint8_t a = assembler.Load<T>(kLevels[0]);
                const uint8_t b = assembler.Load<T>(kLevels[1]);
                const uint8_t dst = assembler.Acquire();
                builder.Emit(opcode, dst, a, b);
                return assembler.Finish(kProgramTypeOf<Result>, dst, programs);
            } else if constexpr (kShape == Kernel::OpShape::Ternary) {
                const uint8_t a = assembler.Load<T>(kLevels[0]);
                const uint8_t b = assembler.Load<T>(kLevels[1]);
                const uint8_t c = assembler.Load<T>(kLevels[2]);
                const uint8_t dst = assembler.Acquire();
                builder.Emit(opcode, dst, a, b, c);
                return assembler.Finish(kProgramTypeOf<Result>, dst, programs);
            } else if constexpr (kShape == Kernel::OpShape::Blend) {
                const uint8_t a = assembler.Load<T>(kLevels[0]);
                const uint8_t b = assembler.Load<T>(kLevels[1]);
                const uint8_t weight = assembler.Load<typename Op::template Weight<T>>(kLevels[2]);
                const uint8_t dst = assembler.Acquire();
                builder.Emit(opcode, dst, a, b, weight);
                return assembler.Finish(kProgramTypeOf<Result>, dst, programs);
            } else if constexpr (kShape == Kernel::OpShape::Select) {
                const uint8_t condition = assembler.Load<bool>(kLevels[0]);
                const uint8_t a = assembler.Load<T>(kLevels[1]);
                const uint8_t b = assembler.Load<T>(kLevels[2]);
                const uint8_t dst = assembler.Acquire();
                builder.Emit(opcode, dst, condition, a, b);
                return assembler.Finish(kProgramTypeOf<Result>, dst, programs);
            } else if constexpr (kShape == Kernel::OpShape::Extract) {
                // One program per component: the index is the instruction's `imm`
                Steps steps;
                for (uint32_t component = 0; component < ComponentTraits<T>::kCount; ++component) {
                    RowAssembler componentAssembler{Format("{}{}", name, component)};
                    const uint8_t a = componentAssembler.Load<T>(kLevels[0]);
                    const uint8_t dst = componentAssembler.Acquire();
                    componentAssembler.Builder().Emit(opcode, dst, a, 0, 0, component);
                    steps.Add(componentAssembler.Finish(kProgramTypeOf<Result>, dst, programs));
                }
                return steps.Result();
            } else {
                static_assert(kShape == Kernel::OpShape::Pack, "every shape has a branch");
                // The components are in consecutive registers: the first three come from the three levels, the fourth from the second field of the first level
                using Component = typename ComponentTraits<T>::Value;
                uint8_t first = 0;
                for (uint32_t component = 0; component < ComponentTraits<T>::kCount; ++component) {
                    const bool second = component >= std::size(kLevels);
                    const uint8_t reg = assembler.Load<Component>(kLevels[component % std::size(kLevels)], second);
                    first = component == 0 ? reg : first;
                }
                const uint8_t dst = assembler.Acquire();
                builder.Emit(opcode, dst, first);
                return assembler.Finish(kProgramTypeOf<T>, dst, programs);
            }
        }

        /// One row of `BE_PROGRAM_MIXED_ROWS`: the left operand from level A, the right from level B
        template <typename Op, typename L, typename R>
        std::expected<void, String> AssembleMixed(StringView name, Opcode opcode, Vector<ProgramId>& programs) {
            RowAssembler assembler{String(name)};
            const uint8_t a = assembler.Load<L>(kOperandsLevelA);
            const uint8_t b = assembler.Load<R>(kOperandsLevelB);
            const uint8_t dst = assembler.Acquire();
            assembler.Builder().Emit(opcode, dst, a, b);
            return assembler.Finish(kProgramTypeOf<Kernel::MixedResultOf<Op, L, R>>, dst, programs);
        }

        template <typename From, typename To>
        std::expected<void, String> AssembleConversion(StringView name, Opcode opcode, Vector<ProgramId>& programs) {
            RowAssembler assembler{String(name)};
            const uint8_t from = assembler.Load<From>(kOperandsLevelA);
            const uint8_t dst = assembler.Acquire();
            assembler.Builder().Emit(opcode, dst, from);
            return assembler.Finish(kProgramTypeOf<To>, dst, programs);
        }

        /// `LoadConst` of a few of the edge values of @p T (the first, the second and the last: zero, one and NaN or the extreme) and `Copy` of a loaded field
        template <typename T>
        std::expected<void, String> AssembleMoves(StringView name, Vector<ProgramId>& programs) {
            Steps steps;
            const Vector<T> edges = Edges<T>();
            const size_t picks[kLoadConstSamples] = {0, 1, edges.size() - 1};
            // A `PoolString` constant holds the address of its entry, which is another one in the next process: such a program is never bound, so none is in the set
            for (const size_t pick : picks) {
                if constexpr (!std::is_same_v<T, PoolString>) {
                    RowAssembler assembler{Format("LoadConst{}{}", name, pick)};
                    const uint8_t reg = assembler.Acquire();
                    assembler.Builder().EmitLoadConst(reg, edges[pick]);
                    steps.Add(assembler.Finish(kProgramTypeOf<T>, reg, programs));
                }
            }
            RowAssembler copy{Format("Copy{}", name)};
            const uint8_t from = copy.Load<T>(kOperandsLevelA);
            const uint8_t dst = copy.Acquire();
            copy.Builder().Emit(Opcode::Copy, dst, from);
            steps.Add(copy.Finish(kProgramTypeOf<T>, dst, programs));
            return steps.Result();
        }

        RegisterImage ImageOf(int32_t value) {
            RegisterImage image{};
            std::memcpy(image.bytes.data(), &value, sizeof(value));
            return image;
        }

        /// `LoadTable` and `LoadTableSlot` over @p count `int32_t` entries, the index being the field `i32`: the edge values of the field are below, inside and above the range
        std::expected<void, String> AssembleTables(uint32_t count, Vector<ProgramId>& programs) {
            Steps steps;
            Vector<RegisterImage> entries;
            for (uint32_t i = 0; i < count; ++i) {
                entries.push_back(ImageOf(static_cast<int32_t>(i) * kLongTableStep - 1));
            }
            {
                RowAssembler assembler{Format("LoadTable{}", count)};
                const uint32_t first = assembler.Builder().AddConstantRun(Span<const RegisterImage>(entries.data(), entries.size()));
                const uint8_t index = assembler.Load<int32_t>(kOperandsLevelA);
                const uint8_t dst = assembler.Acquire();
                assembler.Builder().EmitLoadTable(dst, index, first, count);
                steps.Add(assembler.Finish(ProgramType::Int32, dst, programs));
            }
            {
                RowAssembler assembler{Format("LoadTableSlot{}", count)};
                const uint32_t first = assembler.Builder().AddConstantRun(Span<const RegisterImage>(entries.data(), entries.size()));
                const uint8_t dst = assembler.Acquire();
                Instruction ins{Opcode::LoadTableSlot, dst, kOperandsLevelA, 0, 0, PackHalves(FieldOffset<int32_t>(), first)};
                SetTableEntryCount(ins, count);
                assembler.Builder().Emit(ins);
                steps.Add(assembler.Finish(ProgramType::Int32, dst, programs));
            }
            return steps.Result();
        }

        /// A table whose entries are not numbers of 32 bits: `LoadTable` moves a whole register
        std::expected<void, String> AssembleVectorTable(Vector<ProgramId>& programs) {
            RowAssembler assembler{"LoadTableVec3"};
            Vector<RegisterImage> entries;
            for (const Vec3& value : Edges<Vec3>()) {
                RegisterImage image{};
                std::memcpy(image.bytes.data(), &value, sizeof(value));
                entries.push_back(image);
            }
            const uint32_t first = assembler.Builder().AddConstantRun(Span<const RegisterImage>(entries.data(), entries.size()));
            const uint8_t index = assembler.Load<int32_t>(kOperandsLevelA);
            const uint8_t dst = assembler.Acquire();
            assembler.Builder().EmitLoadTable(dst, index, first, static_cast<uint32_t>(entries.size()));
            return assembler.Finish(ProgramType::Vec3, dst, programs);
        }

        /// `MulAddSlot` over a field of A: `field * k + b`; the constants are ordinary ones and then the last edge of the type and the one before it
        template <typename T>
        std::expected<void, String> AssembleMulAddSlot(StringView name, Opcode opcode, Vector<ProgramId>& programs) {
            using Factor = std::conditional_t<std::is_same_v<T, Vec3>, float, T>;
            const Vector<Factor> factors = Edges<Factor>();
            const Vector<T> addends = Edges<T>();
            const Factor ordinaryFactor = static_cast<Factor>(3);
            const T ordinaryAddend = [] {
                if constexpr (std::is_same_v<T, Vec3>) {
                    return Vec3{1.0f, 2.0f, 3.0f};
                } else {
                    return static_cast<T>(7);
                }
            }();
            const Factor odd[] = {ordinaryFactor, factors.back()};
            const T oddAddend[] = {ordinaryAddend, addends[addends.size() - 2]};
            Steps steps;
            for (size_t i = 0; i < std::size(odd); ++i) {
                RowAssembler assembler{Format("{}{}", name, i)};
                const uint32_t factor = assembler.Builder().AddConstant(odd[i]);
                const uint32_t addend = assembler.Builder().AddConstant(oddAddend[i]);
                const uint8_t dst = assembler.Acquire();
                assembler.Builder().Emit(opcode, dst, kOperandsLevelA, static_cast<uint8_t>(factor), static_cast<uint8_t>(addend), FieldOffset<T>());
                steps.Add(assembler.Finish(kProgramTypeOf<T>, dst, programs));
            }
            return steps.Result();
        }

        /// `LerpSlots` over a field of A and the second field of B, the weight in a register from C
        template <std::floating_point T>
        std::expected<void, String> AssembleLerpSlots(StringView name, Opcode opcode, Vector<ProgramId>& programs) {
            RowAssembler assembler{String(name)};
            const uint8_t weight = assembler.Load<T>(kOperandsLevelC);
            const uint8_t dst = assembler.Acquire();
            const uint32_t fromOffset = FieldOffset<T>();
            const uint32_t toOffset = FieldOffset<T>(true);
            assembler.Builder().Emit(opcode, dst, kOperandsLevelA, kOperandsLevelB, weight, PackHalves(fromOffset, toOffset));
            return assembler.Finish(kProgramTypeOf<T>, dst, programs);
        }

        // `AssembleMulAddSlot<T>` and `AssembleLerpSlots<T>` are picked by the operation name of the row, as the interpreter picks `Execute<Op>`
        template <typename T>
        std::expected<void, String> AssembleFusedMulAddSlot(StringView name, Opcode opcode, Vector<ProgramId>& programs) {
            return AssembleMulAddSlot<T>(name, opcode, programs);
        }

        template <typename T>
        std::expected<void, String> AssembleFusedLerpSlots(StringView name, Opcode opcode, Vector<ProgramId>& programs) {
            return AssembleLerpSlots<T>(name, opcode, programs);
        }

    }  // namespace

    std::expected<void, String> AssembleRowPrograms(Vector<ProgramId>& programs) {
        Steps steps;
#define BE_NATIVE_SET_ROW(Op, Type, Cpp) steps.Add(AssembleRow<Kernel::Op, Cpp>(#Op #Type, Opcode::Op##Type, programs));
        BE_PROGRAM_ROWS(BE_NATIVE_SET_ROW)
#undef BE_NATIVE_SET_ROW
#define BE_NATIVE_SET_FUSED(Op, Type, Cpp) steps.Add(AssembleFused##Op<Cpp>(#Op #Type, Opcode::Op##Type, programs));
        BE_PROGRAM_FUSED_ROWS(BE_NATIVE_SET_FUSED)
#undef BE_NATIVE_SET_FUSED
#define BE_NATIVE_SET_MIXED(Op, LName, LCpp, RName, RCpp) steps.Add(AssembleMixed<Kernel::Op, LCpp, RCpp>(#Op #LName #RName, Opcode::Op##LName##RName, programs));
        BE_PROGRAM_MIXED_ROWS(BE_NATIVE_SET_MIXED)
#undef BE_NATIVE_SET_MIXED
#define BE_NATIVE_SET_CONVERSION(Src, SrcCpp, Dst, DstCpp) steps.Add(AssembleConversion<SrcCpp, DstCpp>(#Src "To" #Dst, Opcode::Src##To##Dst, programs));
        BE_PROGRAM_CONVERSIONS(BE_NATIVE_SET_CONVERSION)
#undef BE_NATIVE_SET_CONVERSION
#define BE_NATIVE_SET_MOVES(Name, Cpp) steps.Add(AssembleMoves<Cpp>(#Name, programs));
        BE_PROGRAM_TYPES(BE_NATIVE_SET_MOVES)
#undef BE_NATIVE_SET_MOVES
        steps.Add(AssembleTables(kShortTableEntries, programs));
        steps.Add(AssembleTables(kLongTableEntries, programs));
        steps.Add(AssembleVectorTable(programs));
        return steps.Result();
    }

}  // namespace BECore::Tests
