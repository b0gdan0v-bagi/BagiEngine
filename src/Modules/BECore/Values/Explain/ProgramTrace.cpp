#include "ProgramTrace.h"

#include <BECore/Values/Explain/FieldResolver.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/InstructionInfo.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Lists/ListImage.h>

namespace BECore {

    namespace {

        constexpr StringView kConstantLabel = "constant";
        constexpr StringView kSizeLabel = "size";
        constexpr StringView kMulAddLabel = "MulAdd";
        constexpr StringView kHexDigits = "0123456789abcdef";
        /// Writing out a trace is bounded as the tree is: a node several parents share is written under each of them, and a hand-built program can share them again and again
        constexpr uint32_t kMaxFormattedLines = kMaxTraceNodes * 4;
        constexpr size_t kIndentWidth = 2;
        constexpr uint32_t kMaxOperands = 4;

        String Hex(const uint8_t* bytes, size_t count) {
            String text;
            for (size_t i = 0; i < count; ++i) {
                text += kHexDigits[bytes[i] >> 4];
                text += kHexDigits[bytes[i] & 0xFU];
            }
            return text;
        }

        template <typename T>
        RegisterImage ImageOf(T value) {
            RegisterImage image{};
            std::memcpy(image.bytes.data(), &value, sizeof(T));
            return image;
        }

        /// The bytes of the field of @p type at @p offset of the object of @p level, as a register holds them; zero for a level with no object
        RegisterImage ReadFieldImage(const Context& context, ProgramType type, LevelIndex level, uint32_t offset) {
            RegisterImage image{};
            const void* object = level < kMaxContextLevels ? context.Object(level) : nullptr;
            if (object != nullptr) {
                std::memcpy(image.bytes.data(), static_cast<const uint8_t*>(object) + offset, ProgramTypeSize(type));
            }
            return image;
        }

        /// What the descriptor of a registered type says; null for one with none
        const TypeDesc* DescriptorOf(const DomainValueType& type) {
            if (type.enumType != nullptr) {
                return type.enumType;
            }
            return type.IsDomain() ? ProgramDomain::Type(type.domain).descriptor : nullptr;
        }

        /// What a register is while the run goes on: the node that wrote it and, for a list or an element, the list it walks
        struct RegisterState {
            /// A value register: its node; a list or an element register: the node of the list
            uint32_t node = kNoTraceNode;
            /// A list register: the type of the elements (null for a scalar or an unreadable one); an element register: its own type
            const TypeInfo* element = nullptr;
            /// The type of the list field, which names the enumerators of a list of enums
            const TypeDesc* list = nullptr;
            /// An element register: the position, for a label
            uint32_t index = 0;
        };

        class TraceBuilder final : public ProgramTraceSink {
        public:
            TraceBuilder(const Program& program, const Context& context, const ContextDecl& declaration) : _program(program), _context(context), _declaration(declaration) {}

            void OnStep(const ProgramTraceStep& step) override {
                if (_trace.truncated) {
                    return;
                }
                const Instruction& ins = *step.instruction;
                const RegisterImage& written = step.registers[ins.dst];
                const InstructionInfo info = DescribeInstruction(ins);
                switch (ins.op) {
                    case Opcode::Invalid:
                        return;
                    case Opcode::LoadConst:
                        OnLoadConst(ins);
                        return;
                    case Opcode::Copy:
                        _registers[ins.dst] = _registers[ins.a];
                        return;
                    case Opcode::LoadTable:
                        OnLoadTable(ins, written);
                        return;
                    case Opcode::LoadTableSlot:
                        OnLoadTableSlot(ins, written);
                        return;
                    case Opcode::CallDomain:
                        OnCallDomain(ins, written);
                        return;
                    case Opcode::ListSlot:
                        OnListSlot(ins, written);
                        return;
                    case Opcode::ListElem:
                        OnListElem(ins, written);
                        return;
                    case Opcode::ListSize:
                        OnListSize(ins, written);
                        return;
                    case Opcode::ListAt:
                        OnListAt(ins, written);
                        return;
                    case Opcode::LoopBegin:
                        OnLoopBegin(ins, written);
                        return;
                    case Opcode::LoopEnd:
                        OnLoopEnd(ins, written);
                        return;
                    case Opcode::BreakIf:
                        return;
                    default:
                        break;
                }
                if (IsLoadSlot(ins.op)) {
                    OnLoadSlot(ins, written);
                } else if (IsLoadElem(ins.op)) {
                    OnLoadElem(ins, written);
                } else if (IsMulAddSlot(ins.op)) {
                    OnMulAddSlot(ins, written);
                } else if (IsLerpSlots(ins.op)) {
                    OnLerpSlots(ins, written);
                } else {
                    OnOperation(ins, info, written);
                }
            }

            /// The trace with the result of the run: the root takes the value, which a cut trace has not reached
            ProgramTrace Finish(const RegisterImage& result) {
                _trace.resultType = _program.ResultType();
                _trace.result = result;
                const uint32_t root = _registers[_program.ResultRegister()].node;
                if (root != kNoTraceNode) {
                    Retype(root, _trace.resultType);
                    TraceNode& node = _trace.nodes[root];
                    if (node.type == _trace.resultType) {
                        node.image = result;
                    }
                    _trace.root = root;
                    _trace.resultDescribed = node.described;
                }
                return std::move(_trace);
            }

        private:
            /// Whether @p count more nodes fit; a step that does not fit ends the recording
            bool Fits(size_t count) {
                if (_trace.nodes.size() + count > kMaxTraceNodes) {
                    _trace.truncated = true;
                    return false;
                }
                return true;
            }

            /// The node is added after its children, which are already in the tree; @p typed says whether the instruction knows what its value is (a constant learns it from its reader)
            uint32_t AddNode(TraceNode node, Span<const uint32_t> children, bool typed) {
                node.firstChild = static_cast<uint32_t>(_trace.childIndexes.size());
                node.childCount = static_cast<uint32_t>(children.size());
                _trace.childIndexes.insert(_trace.childIndexes.end(), children.begin(), children.end());
                _trace.nodes.push_back(std::move(node));
                _typed.push_back(typed);
                return static_cast<uint32_t>(_trace.nodes.size() - 1);
            }

            void Retype(uint32_t node, ProgramType type) {
                if (node != kNoTraceNode && !_typed[node]) {
                    _trace.nodes[node].type = type;
                    _typed[node] = true;
                }
            }

            void Describe(uint32_t node, const TypeDesc* described) {
                if (node != kNoTraceNode && described != nullptr && _trace.nodes[node].described == nullptr) {
                    _trace.nodes[node].described = described;
                }
            }

            /// `caster.attack`; where no field starts, `level 'caster' +24`, and `level 3 +24` where the declaration has no such level
            String FieldLabel(LevelIndex level, uint32_t offset, const Optional<ResolvedField>& resolved) const {
                if (resolved.has_value()) {
                    return resolved->path;
                }
                if (level < _declaration.Size()) {
                    return Format("level '{}' +{}", _declaration.Levels()[level].name, offset);
                }
                return Format("level {} +{}", level, offset);
            }

            /// `hand.tiles[3]`: the element a register holds
            String ElementLabel(const RegisterState& element) const {
                const String list = element.node != kNoTraceNode ? _trace.nodes[element.node].label : String("list");
                return Format("{}[{}]", list, element.index);
            }

            /// The registers an instruction reads as operands, in the order of its fields (`a`, `b`, `c`; a `Pack` reads a run from `a`), each once
            Vector<uint8_t> OperandRegisters(const Instruction& ins, const InstructionInfo& info) const {
                Vector<uint8_t> operands;
                if (IsPack(ins.op)) {
                    const uint32_t count = ComponentsOf(*ComponentOpcodeType(ins.op)).count;
                    for (uint32_t i = 0; i < count; ++i) {
                        operands.push_back(static_cast<uint8_t>(ins.a + i));
                    }
                    return operands;
                }
                for (const uint8_t reg : {ins.a, ins.b, ins.c}) {
                    if ((info.reads & RegisterBit(reg)) != 0 && std::find(operands.begin(), operands.end(), reg) == operands.end()) {
                        operands.push_back(reg);
                    }
                }
                return operands;
            }

            void OnLoadConst(const Instruction& ins) {
                if (!Fits(1)) {
                    return;
                }
                TraceNode node;
                node.kind = TraceStepKind::Constant;
                node.opcode = ins.op;
                node.label = String(kConstantLabel);
                node.image = ins.imm < _program.Constants().size() ? _program.Constants()[ins.imm] : RegisterImage{};
                _registers[ins.dst] = RegisterState{AddNode(std::move(node), {}, false), nullptr, nullptr, 0};
            }

            void OnLoadSlot(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                const ProgramType type = *ResultTypeOf(ins.op);
                _registers[ins.dst] = RegisterState{AddField(ins.op, type, ins.a, ins.imm, written), nullptr, nullptr, 0};
            }

            /// A node for the field at @p offset of the object of @p level; the room is checked by the caller
            uint32_t AddField(Opcode opcode, ProgramType type, LevelIndex level, uint32_t offset, const RegisterImage& image) {
                const Optional<ResolvedField> resolved = ResolveField(_declaration, level, offset, type);
                TraceNode node;
                node.kind = TraceStepKind::ContextKey;
                node.type = type;
                node.opcode = opcode;
                node.label = FieldLabel(level, offset, resolved);
                node.image = image;
                node.described = resolved.has_value() ? resolved->type : nullptr;
                return AddNode(std::move(node), {}, true);
            }

            void OnLoadElem(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                const RegisterState& owner = _registers[ins.a];
                const ProgramType type = *ResultTypeOf(ins.op);
                TraceNode node;
                node.kind = TraceStepKind::ContextKey;
                node.type = type;
                node.opcode = ins.op;
                node.label = ElementLabel(owner);
                if (owner.element != nullptr) {
                    const Optional<ResolvedField> resolved = ResolveElementField(*owner.element, ins.imm, type);
                    node.label += resolved.has_value() ? Format(".{}", resolved->path) : Format(" +{}", ins.imm);
                    node.described = resolved.has_value() ? resolved->type : nullptr;
                } else if (owner.list != nullptr) {
                    node.described = owner.list->element;
                }
                node.image = written;
                const uint32_t children[] = {owner.node};
                _registers[ins.dst] = RegisterState{AddNode(std::move(node), owner.node != kNoTraceNode ? Span<const uint32_t>(children, 1) : Span<const uint32_t>{}, true), nullptr, nullptr, 0};
            }

            void OnMulAddSlot(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(4)) {
                    return;
                }
                const ProgramType type = *ResultTypeOf(ins.op);
                const ProgramType factorType = type == ProgramType::Vec3 ? ProgramType::Float : type;
                const uint32_t field = AddField(Opcode::Invalid, type, ins.a, ins.imm, ReadFieldImage(_context, type, ins.a, ins.imm));
                const uint32_t factor = AddConstant(factorType, ins.b);
                const uint32_t addend = AddConstant(type, ins.c);
                const uint32_t children[] = {field, factor, addend};
                TraceNode node;
                node.kind = TraceStepKind::Operation;
                node.type = type;
                node.opcode = ins.op;
                node.label = String(kMulAddLabel);
                node.image = written;
                _registers[ins.dst] = RegisterState{AddNode(std::move(node), children, true), nullptr, nullptr, 0};
            }

            void OnLerpSlots(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(3)) {
                    return;
                }
                const ProgramType type = *ResultTypeOf(ins.op);
                const uint32_t from = AddField(Opcode::Invalid, type, ins.a, LowHalf(ins.imm), ReadFieldImage(_context, type, ins.a, LowHalf(ins.imm)));
                const uint32_t to = AddField(Opcode::Invalid, type, ins.b, HighHalf(ins.imm), ReadFieldImage(_context, type, ins.b, HighHalf(ins.imm)));
                const uint32_t weight = _registers[ins.c].node;
                Retype(weight, type);
                Vector<uint32_t> children{from, to};
                if (weight != kNoTraceNode) {
                    children.push_back(weight);
                }
                TraceNode node;
                node.kind = TraceStepKind::Operation;
                node.type = type;
                node.opcode = ins.op;
                node.operation = ProgramOperation::Lerp;
                node.label = String(EnumUtils<ProgramOperation>::ToString(ProgramOperation::Lerp));
                node.image = written;
                _registers[ins.dst] = RegisterState{AddNode(std::move(node), Span<const uint32_t>(children.data(), children.size()), true), nullptr, nullptr, 0};
            }

            /// A node for constant @p index of the program, of a type the fused instruction that holds it knows
            uint32_t AddConstant(ProgramType type, uint32_t index) {
                TraceNode node;
                node.kind = TraceStepKind::Constant;
                node.type = type;
                node.label = String(kConstantLabel);
                node.image = index < _program.Constants().size() ? _program.Constants()[index] : RegisterImage{};
                return AddNode(std::move(node), {}, true);
            }

            void OnLoadTable(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                const uint32_t index = _registers[ins.a].node;
                Retype(index, ProgramType::Int32);
                const uint32_t count = TableEntryCount(ins);
                const uint32_t row = index != kNoTraceNode && count > 0 ? Kernel::TableIndex(Kernel::Load<int32_t>(_trace.nodes[index].image), count) : 0;
                const uint32_t children[] = {index};
                _registers[ins.dst] = RegisterState{AddTable(ins, row, count, written, index != kNoTraceNode ? Span<const uint32_t>(children, 1) : Span<const uint32_t>{}), nullptr, nullptr, 0};
            }

            void OnLoadTableSlot(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(2)) {
                    return;
                }
                const uint32_t offset = LowHalf(ins.imm);
                const RegisterImage indexImage = ReadFieldImage(_context, ProgramType::Int32, ins.a, offset);
                const uint32_t index = AddField(Opcode::Invalid, ProgramType::Int32, ins.a, offset, indexImage);
                const uint32_t count = TableEntryCount(ins);
                const uint32_t row = count > 0 ? Kernel::TableIndex(Kernel::Load<int32_t>(indexImage), count) : 0;
                const uint32_t children[] = {index};
                _registers[ins.dst] = RegisterState{AddTable(ins, row, count, written, children), nullptr, nullptr, 0};
            }

            uint32_t AddTable(const Instruction& ins, uint32_t row, uint32_t count, const RegisterImage& written, Span<const uint32_t> children) {
                TraceNode node;
                node.kind = TraceStepKind::Table;
                node.opcode = ins.op;
                node.label = Format("table row {} of {}", row, count);
                node.image = written;
                return AddNode(std::move(node), children, false);
            }

            void OnCallDomain(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                TraceNode node;
                node.kind = TraceStepKind::Call;
                node.opcode = ins.op;
                node.image = written;
                Vector<uint32_t> children;
                if (ins.imm < ProgramDomain::OperationCount()) {
                    const DomainOperation& operation = ProgramDomain::Operation(ins.imm);
                    node.label = String(operation.name.ToStringView());
                    node.type = operation.result.type;
                    node.described = DescriptorOf(operation.result);
                    const Array<uint8_t, kMaxDomainArguments> registers{ins.a, ins.b, ins.c};
                    for (uint32_t i = 0; i < operation.parameterCount; ++i) {
                        const uint32_t argument = _registers[registers[i]].node;
                        if (argument == kNoTraceNode) {
                            continue;
                        }
                        Retype(argument, operation.parameters[i].type);
                        Describe(argument, DescriptorOf(operation.parameters[i]));
                        children.push_back(argument);
                    }
                } else {
                    node.label = Format("operation {}", ins.imm);
                }
                _registers[ins.dst] = RegisterState{AddNode(std::move(node), Span<const uint32_t>(children.data(), children.size()), true), nullptr, nullptr, 0};
            }

            /// The operations of the language, the mixed ones and the conversions: the children are the nodes of the operands
            void OnOperation(const Instruction& ins, const InstructionInfo& info, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                Vector<uint32_t> children;
                for (const uint8_t reg : OperandRegisters(ins, info)) {
                    const uint32_t child = _registers[reg].node;
                    if (child == kNoTraceNode) {
                        continue;
                    }
                    if (const Optional<ProgramType> type = OperandTypeOf(ins, reg)) {
                        Retype(child, *type);
                    }
                    children.push_back(child);
                }
                const ProgramType resultType = info.resultType.value_or(ProgramType::Int32);
                const TypeDesc* described = ShareDescription(children, resultType);
                TraceNode node;
                node.kind = info.operation.has_value() ? TraceStepKind::Operation : TraceStepKind::Conversion;
                node.type = resultType;
                node.opcode = ins.op;
                node.operation = info.operation;
                node.label = info.operation.has_value() ? String(EnumUtils<ProgramOperation>::ToString(*info.operation)) : String(EnumUtils<Opcode>::ToString(ins.op));
                if (ComponentOpcodeType(ins.op).has_value()) {
                    AppendComponentName(node, ins);
                }
                node.image = written;
                node.described = described;
                _registers[ins.dst] = RegisterState{AddNode(std::move(node), Span<const uint32_t>(children.data(), children.size()), true), nullptr, nullptr, 0};
            }

            /// `Extract .x`: the component the instruction takes
            static void AppendComponentName(TraceNode& node, const Instruction& ins) {
                const Optional<ProgramType> type = ComponentOpcodeType(ins.op);
                if (!type.has_value() || IsPack(ins.op)) {
                    return;
                }
                const ComponentSet set = ComponentsOf(*type);
                if (ins.imm < set.count) {
                    node.label += Format(" .{}", set.names[ins.imm]);
                }
            }

            /// The operands of one enum or registered type are the same type, and so is a result of that type (`select`): what one of them knows the others and the result know
            const TypeDesc* ShareDescription(const Vector<uint32_t>& operands, ProgramType resultType) {
                const TypeDesc* shared = nullptr;
                ProgramType sharedType = ProgramType::Enum;
                for (const uint32_t operand : operands) {
                    const TraceNode& node = _trace.nodes[operand];
                    if ((node.type == ProgramType::Enum || node.type == ProgramType::Domain) && node.described != nullptr) {
                        shared = node.described;
                        sharedType = node.type;
                        break;
                    }
                }
                if (shared == nullptr) {
                    return nullptr;
                }
                for (const uint32_t operand : operands) {
                    if (_trace.nodes[operand].type == sharedType) {
                        Describe(operand, shared);
                    }
                }
                return resultType == sharedType ? shared : nullptr;
            }

            void OnListSlot(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                const ListDescriptor descriptor = DescriptorAt(ins.imm);
                const Optional<ResolvedField> resolved = ResolveField(_declaration, ins.a, descriptor.offset);
                const TypeDesc* list = resolved.has_value() && resolved->type->kind == TypeKind::List ? resolved->type : nullptr;
                _registers[ins.dst] = ListRegister(ins, FieldLabel(ins.a, descriptor.offset, resolved), list, written);
            }

            void OnListElem(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                const RegisterState& owner = _registers[ins.a];
                const ListDescriptor descriptor = DescriptorAt(ins.imm);
                Optional<ResolvedField> resolved;
                if (owner.element != nullptr) {
                    resolved = ResolveElementField(*owner.element, descriptor.offset);
                }
                const String field = resolved.has_value() ? resolved->path : Format("+{}", descriptor.offset);
                const TypeDesc* list = resolved.has_value() && resolved->type->kind == TypeKind::List ? resolved->type : nullptr;
                _registers[ins.dst] = ListRegister(ins, Format("{}.{}", ElementLabel(owner), field), list, written);
            }

            /// The node of a list read from a field, and the register that holds it
            RegisterState ListRegister(const Instruction& ins, String label, const TypeDesc* list, const RegisterImage& written) {
                const auto image = Kernel::Load<ListImage>(written);
                TraceNode node;
                node.kind = TraceStepKind::List;
                node.opcode = ins.op;
                node.label = std::move(label);
                node.image = ImageOf(static_cast<int32_t>(std::min<uint32_t>(image.count, std::numeric_limits<int32_t>::max())));
                node.described = list;
                const TypeInfo* element = nullptr;
                if (list != nullptr) {
                    const auto described = DescribeListElement(*list);
                    element = described.has_value() ? *described : nullptr;
                }
                return RegisterState{AddNode(std::move(node), {}, true), element, list, 0};
            }

            void OnListSize(const Instruction& ins, const RegisterImage& written) {
                if (!Fits(1)) {
                    return;
                }
                const uint32_t children[] = {_registers[ins.a].node};
                TraceNode node;
                node.kind = TraceStepKind::Operation;
                node.type = ProgramType::Int32;
                node.opcode = ins.op;
                node.label = String(kSizeLabel);
                node.image = written;
                _registers[ins.dst] = RegisterState{AddNode(std::move(node), children[0] != kNoTraceNode ? Span<const uint32_t>(children, 1) : Span<const uint32_t>{}, true), nullptr, nullptr, 0};
            }

            /// The element register of `ListAt`, `LoopBegin` and `LoopEnd` has no node: it is the list's, with the position it holds
            void OnListAt(const Instruction& ins, const RegisterImage& written) {
                const uint32_t index = _registers[ins.b].node;
                Retype(index, ProgramType::Int32);
                RegisterState element = _registers[ins.a];
                element.index = index != kNoTraceNode ? static_cast<uint32_t>(Kernel::Load<int32_t>(_trace.nodes[index].image)) : Kernel::Load<ElementImage>(written).index;
                _registers[ins.dst] = element;
            }

            void OnLoopBegin(const Instruction& ins, const RegisterImage& written) {
                RegisterState element = _registers[ins.a];
                element.index = Kernel::Load<ElementImage>(written).index;
                _registers[ins.dst] = element;
            }

            void OnLoopEnd(const Instruction& ins, const RegisterImage& written) {
                if (_registers[ins.dst].node == kNoTraceNode) {
                    _registers[ins.dst] = _registers[ins.a];
                }
                _registers[ins.dst].index = Kernel::Load<ElementImage>(written).index;
            }

            ListDescriptor DescriptorAt(uint32_t index) const {
                return index < _program.Constants().size() ? ListDescriptor::FromImage(_program.Constants()[index]) : ListDescriptor{};
            }

            const Program& _program;
            const Context& _context;
            const ContextDecl& _declaration;
            ProgramTrace _trace;
            /// Per node: whether its type is known (a constant and a table entry learn it from the first instruction that reads them)
            Vector<bool> _typed;
            Array<RegisterState, kMaxProgramRegisters> _registers{};
        };

        void AppendNode(const ProgramTrace& trace, uint32_t index, size_t depth, uint32_t& lines, String& text) {
            if (lines >= kMaxFormattedLines) {
                return;
            }
            ++lines;
            const TraceNode& node = trace.nodes[index];
            text.append(depth * kIndentWidth, ' ');
            text += Format("{} = {}\n", node.label, FormatNodeValue(node));
            for (const uint32_t child : trace.Children(node)) {
                AppendNode(trace, child, depth + 1, lines, text);
            }
        }

    }  // namespace

    ProgramTrace ExplainProgram(const Program& program, const Context& context, const ContextDecl& declaration) {
        for (LevelIndex level = 0; level < kMaxContextLevels; ++level) {
            if ((program.Reads() & LevelBit(level)) != 0 && context.Object(level) == nullptr) {
                ProgramTrace empty;
                empty.resultType = program.ResultType();
                empty.missingLevel = level;
                return empty;
            }
        }
        TraceBuilder builder(program, context, declaration);
        RegisterImage result{};
        RunTraced(program, context, result.bytes.data(), builder);
        return builder.Finish(result);
    }

    String FormatImage(ProgramType type, const RegisterImage& image, const TypeDesc* described) {
        switch (type) {
            case ProgramType::Bool:
                return String(Kernel::Load<bool>(image) ? "true" : "false");
            case ProgramType::Int32:
                return Format("{}", Kernel::Load<int32_t>(image));
            case ProgramType::Int64:
                return Format("{}", Kernel::Load<int64_t>(image));
            case ProgramType::Float:
                return Format("{}", Kernel::Load<float>(image));
            case ProgramType::Double:
                return Format("{}", Kernel::Load<double>(image));
            case ProgramType::Vec2: {
                const auto value = Kernel::Load<Vec2>(image);
                return Format("({}, {})", value.x, value.y);
            }
            case ProgramType::Vec3: {
                const auto value = Kernel::Load<Vec3>(image);
                return Format("({}, {}, {})", value.x, value.y, value.z);
            }
            case ProgramType::Vec4: {
                const auto value = Kernel::Load<Vec4>(image);
                return Format("({}, {}, {}, {})", value.x, value.y, value.z, value.w);
            }
            case ProgramType::Quat: {
                const auto value = Kernel::Load<Quat>(image);
                return Format("({}, {}, {}, {})", value.x, value.y, value.z, value.w);
            }
            case ProgramType::IVec2: {
                const auto value = Kernel::Load<IVec2>(image);
                return Format("({}, {})", value.x, value.y);
            }
            case ProgramType::Color: {
                const auto value = Kernel::Load<Color>(image);
                return Format("({}, {}, {}, {})", value.r, value.g, value.b, value.a);
            }
            case ProgramType::Enum: {
                const uint32_t index = Kernel::Load<ProgramEnum>(image).index;
                if (described != nullptr && described->kind == TypeKind::Enum && index < described->names.size()) {
                    return String(described->names[index]);
                }
                return Format("#{}", index);
            }
            case ProgramType::String:
                return Format("\"{}\"", Kernel::Load<PoolString>(image).ToStringView());
            case ProgramType::Domain: {
                if (described != nullptr) {
                    if (const Optional<DomainTypeId> domain = ProgramDomain::FindTypeByDescriptor(*described)) {
                        const DomainType& registered = ProgramDomain::Type(*domain);
                        return Format("<{} {} bytes: {}>", registered.name.ToStringView(), registered.size, Hex(image.bytes.data(), std::min<size_t>(registered.size, kProgramRegisterBytes)));
                    }
                }
                return Format("<registered {} bytes: {}>", kProgramRegisterBytes, Hex(image.bytes.data(), kProgramRegisterBytes));
            }
        }
        return String();
    }

    String FormatNodeValue(const TraceNode& node) {
        if (node.kind == TraceStepKind::List) {
            return Format("{} elements", Kernel::Load<int32_t>(node.image));
        }
        return FormatImage(node.type, node.image, node.described);
    }

    String FormatTrace(const ProgramTrace& trace) {
        String text;
        uint32_t lines = 0;
        if (trace.root != kNoTraceNode) {
            AppendNode(trace, trace.root, 0, lines, text);
        }
        if (trace.truncated) {
            text += Format("... the trace stops at {} nodes\n", kMaxTraceNodes);
        }
        return text;
    }

}  // namespace BECore
