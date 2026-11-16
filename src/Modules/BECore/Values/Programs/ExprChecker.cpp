#include "ExprChecker.h"

#include <BECore/Reflection/CompiledPath.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Values/Programs/ExprFunctions.h>
#include <cmath>

namespace BECore {

    namespace {

        using TypedResult = std::expected<TypedNodeId, ProgramError>;

        constexpr uint64_t kInt32MagnitudeLimit = uint64_t{1} << 31;
        constexpr uint64_t kInt64MagnitudeLimit = uint64_t{1} << 63;
        /// A message lists this many enumerators of an enum and says how many it left out
        constexpr size_t kMaxEnumeratorsListed = 8;

        std::unexpected<ProgramError> Refuse(uint32_t column, String message) {
            return std::unexpected(ProgramError{std::move(message), column});
        }

        template <typename T>
        RegisterImage ImageOf(T value) {
            RegisterImage image{};
            std::memcpy(image.bytes.data(), &value, sizeof(T));
            return image;
        }

        // The types a descriptor can be matched against: every program type but `Enum`, whose descriptor is the field's own
#define BE_CHECKER_DESCRIBABLE_TYPES(X, Op) BE_PROGRAM_TYPES_SCALARS(X, Op) BE_PROGRAM_TYPES_MATH(X, Op) BE_PROGRAM_TYPES_STRING(X, Op)

        /// The type a descriptor stands for: a program type, or an enum with its own descriptor
        Optional<ExprType> ProgramTypeFor(const TypeDesc& type) {
            if (type.kind == TypeKind::Enum) {
                return ExprType::OfEnum(type);
            }
#define BE_PROGRAM_TYPE_FOR(Unused, Name, Cpp)                                                                                                                                                         \
    if (SameType(type, DescribeType<Cpp>())) {                                                                                                                                                         \
        return ExprType(ProgramType::Name);                                                                                                                                                            \
    }
            BE_CHECKER_DESCRIBABLE_TYPES(BE_PROGRAM_TYPE_FOR, Unused)
#undef BE_PROGRAM_TYPE_FOR
            return std::nullopt;
        }

        /// The descriptor of a program type, null for `Enum` (an enum is told by its field)
        const TypeDesc* DescriptorOf(ProgramType type) {
            switch (type) {
#define BE_PROGRAM_DESCRIPTOR(Unused, Name, Cpp)                                                                                                                                                       \
    case ProgramType::Name:                                                                                                                                                                            \
        return &DescribeType<Cpp>();
                BE_CHECKER_DESCRIBABLE_TYPES(BE_PROGRAM_DESCRIPTOR, Unused)
#undef BE_PROGRAM_DESCRIPTOR
                default:
                    break;
            }
            return nullptr;
        }

        String JoinTypes(const Vector<ProgramType>& types) {
            String text;
            for (size_t i = 0; i < types.size(); ++i) {
                if (i > 0) {
                    text += i + 1 == types.size() ? " or " : ", ";
                }
                const StringView name = ProgramTypeName(types[i]);
                text.append(name.data(), name.size());
            }
            return text;
        }

        Vector<ProgramType> AllTypes() {
            Vector<ProgramType> types;
            for (const ProgramType type : EnumUtils<ProgramType>::Values()) {
                types.push_back(type);
            }
            return types;
        }

        /// The types an operation has a row for, as a message lists them
        String TypesWith(ProgramOperation operation) {
            Vector<ProgramType> types;
            for (const ProgramType type : AllTypes()) {
                if (OpcodeOf(operation, type) != Opcode::Invalid) {
                    types.push_back(type);
                }
            }
            return JoinTypes(types);
        }

        /// The enumerators as a message lists them: `Fire, Water, Earth`, and the number left out of a long list
        String EnumeratorList(const TypeDesc& enumeration) {
            String text;
            const size_t shown = std::min(enumeration.names.size(), kMaxEnumeratorsListed);
            for (size_t i = 0; i < shown; ++i) {
                text += i == 0 ? "" : ", ";
                text.append(enumeration.names[i].data(), enumeration.names[i].size());
            }
            if (shown < enumeration.names.size()) {
                text += Format(", ... ({} more)", enumeration.names.size() - shown);
            }
            return text;
        }

        /// A type as a message names it: `float`, `vec3`, `enum {Fire, Water}`
        String DescribeExprType(const ExprType& type) {
            const StringView name = ProgramTypeName(type.program);
            if (type.program == ProgramType::Enum && type.enumeration != nullptr) {
                return Format("{} {{{}}}", name, EnumeratorList(*type.enumeration));
            }
            return String(name);
        }

        /// `an int`, `a float`, `an enum {Fire}`: the type with its article
        String Typed(const ExprType& type) {
            const String name = DescribeExprType(type);
            const bool vowel = StringView("aeiou").find(name.front()) != StringView::npos;
            return Format("{} {}", vowel ? "an" : "a", name);
        }

        /// A type of a field or an expected type as a message names it
        String DescribeDescriptor(const TypeDesc& type) {
            switch (type.kind) {
                case TypeKind::Object:
                case TypeKind::Pointer:
                    return String(type.ObjectName());
                case TypeKind::Opaque:
                    return String(type.opaqueName);
                default:
                    return String(TypeKindName(type.kind));
            }
        }

        /// How a conversion is written: the function that makes the type
        String ConvertHint(const ExprType& from, const ExprType& to) {
            if (!IsNumberType(from.program) || !IsNumberType(to.program)) {
                return {};
            }
            return Format("; convert one side explicitly: {}(x) or {}(x)", ProgramTypeName(to.program), ProgramTypeName(from.program));
        }

        bool IsComparison(ProgramOperation operation) {
            return operation == ProgramOperation::Eq || operation == ProgramOperation::Ne || operation == ProgramOperation::Lt || operation == ProgramOperation::Le ||
                   operation == ProgramOperation::Gt || operation == ProgramOperation::Ge;
        }

        bool IsLogic(ProgramOperation operation) {
            return operation == ProgramOperation::And || operation == ProgramOperation::Or;
        }

        class Checker {
        public:
            Checker(const ExprAst& ast, const ContextDecl& declaration) : _ast(ast), _declaration(declaration) {
                // The children of a node come before it, so one pass in order sees the children's answers
                _hasName.resize(ast.nodes.size(), false);
                for (size_t i = 0; i < ast.nodes.size(); ++i) {
                    const ExprNode& node = ast.nodes[i];
                    switch (node.kind) {
                        case ExprNodeKind::Name:
                            // A bare name is a level, a function or an enumerator: only a path reads the context and gives its operand a type to the others
                            _hasName[i] = node.text.find('.') != String::npos;
                            break;
                        case ExprNodeKind::Call:
                            for (uint32_t a = 0; a < node.argumentCount; ++a) {
                                _hasName[i] = _hasName[i] || _hasName[ast.arguments[node.firstArgument + a]];
                            }
                            break;
                        case ExprNodeKind::Unary:
                        case ExprNodeKind::Member:
                            _hasName[i] = _hasName[node.first];
                            break;
                        case ExprNodeKind::Binary:
                            _hasName[i] = _hasName[node.first] || _hasName[node.second];
                            break;
                        default:
                            break;
                    }
                }
            }

            std::expected<TypedExpr, ProgramError> Run(const TypeDesc& expected) {
                const auto wanted = CheckExpectedType(expected);
                if (!wanted.has_value()) {
                    return std::unexpected(wanted.error());
                }
                const ExprType want = *wanted;
                const TypedResult root = Check(_ast.root, want);
                if (!root.has_value()) {
                    return std::unexpected(root.error());
                }
                const TypedNode& node = _out.nodes[*root];
                if (!(node.type == want)) {
                    return Refuse(_ast[_ast.root].column, Format("the formula is {}, but a value of {} is asked for{}", Typed(node.type), DescribeExprType(want), ConvertHint(node.type, want)));
                }
                _out.root = *root;
                _out.type = want;
                return std::move(_out);
            }

        private:
            TypedNodeId Emit(TypedNode node) {
                _out.nodes.push_back(std::move(node));
                return static_cast<TypedNodeId>(_out.nodes.size() - 1);
            }

            TypedNodeId EmitOperation(Opcode opcode, const ExprType& type, uint32_t column, std::initializer_list<TypedNodeId> operands) {
                TypedNode node;
                node.kind = TypedNodeKind::Operation;
                node.type = type;
                node.opcode = opcode;
                node.column = column;
                for (const TypedNodeId operand : operands) {
                    node.operands[node.operandCount++] = operand;
                }
                return Emit(std::move(node));
            }

            [[nodiscard]] const ExprType& TypeOf(TypedNodeId id) const {
                return _out.nodes[id].type;
            }

            /// The hint is what a literal without a type of its own becomes: the type of the operand beside it, or the type the whole formula has
            TypedResult Check(ExprNodeId id, Optional<ExprType> hint) {
                const ExprNode& node = _ast[id];
                switch (node.kind) {
                    case ExprNodeKind::Number:
                        return CheckLiteral(id, hint);
                    case ExprNodeKind::Bool: {
                        TypedNode constant;
                        constant.type = ProgramType::Bool;
                        constant.constant = ImageOf<bool>(node.boolValue);
                        constant.column = node.column;
                        return Emit(std::move(constant));
                    }
                    case ExprNodeKind::String: {
                        TypedNode constant;
                        constant.type = ProgramType::String;
                        constant.constant = ImageOf<PoolString>(PoolString::Intern(node.text));
                        constant.column = node.column;
                        return Emit(std::move(constant));
                    }
                    case ExprNodeKind::Name:
                        return CheckName(id, hint);
                    case ExprNodeKind::Call:
                        return CheckCall(id, hint);
                    case ExprNodeKind::Member:
                        return Refuse(node.column, Format("'.{}': no type has components in a formula yet, only a whole value can be read", node.text));
                    case ExprNodeKind::Unary:
                        return CheckUnary(id, hint);
                    case ExprNodeKind::Binary:
                        return CheckBinary(id, hint);
                }
                return Refuse(node.column, "unknown node");
            }

            // Literals

            /// A number, or minus before one: the only things whose type is not their own
            [[nodiscard]] bool IsLiteral(ExprNodeId id) const {
                const ExprNode& node = _ast[id];
                if (node.kind == ExprNodeKind::Number) {
                    return true;
                }
                return node.kind == ExprNodeKind::Unary && node.operation == ProgramOperation::Neg && IsLiteral(node.first);
            }

            [[nodiscard]] bool ContainsFloatLiteral(ExprNodeId id) const {
                const ExprNode& node = _ast[id];
                switch (node.kind) {
                    case ExprNodeKind::Number:
                        return node.isFloat;
                    case ExprNodeKind::Call:
                        for (uint32_t i = 0; i < node.argumentCount; ++i) {
                            if (ContainsFloatLiteral(_ast.arguments[node.firstArgument + i])) {
                                return true;
                            }
                        }
                        return false;
                    case ExprNodeKind::Unary:
                    case ExprNodeKind::Member:
                        return ContainsFloatLiteral(node.first);
                    case ExprNodeKind::Binary:
                        return ContainsFloatLiteral(node.first) || ContainsFloatLiteral(node.second);
                    default:
                        return false;
                }
            }

            TypedResult CheckLiteral(ExprNodeId id, Optional<ExprType> hint) {
                bool negative = false;
                ExprNodeId number = id;
                while (_ast[number].kind == ExprNodeKind::Unary) {
                    negative = !negative;
                    number = _ast[number].first;
                }
                const ExprNode& node = _ast[number];
                const uint32_t column = _ast[id].column;
                const bool numberHint = hint.has_value() && IsNumberType(hint->program);

                TypedNode constant;
                constant.column = column;
                if (node.isFloat) {
                    const Optional<double> parsed = StringUtils::ParseNumber<double>(node.text);
                    if (!parsed.has_value() || !std::isfinite(*parsed)) {
                        return Refuse(column, Format("the number '{}' is out of the range of a double", node.text));
                    }
                    const double value = negative ? -*parsed : *parsed;
                    const ProgramType type = numberHint ? hint->program : ProgramType::Float;
                    constant.type = type;
                    if (type == ProgramType::Double) {
                        constant.constant = ImageOf<double>(value);
                    } else if (type == ProgramType::Float) {
                        if (std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
                            return Refuse(column, Format("the number '{}' is out of the range of a float; use a double", node.text));
                        }
                        // The text is read as a float itself: through a double the value would be rounded twice. What is below the smallest float is still the nearest one (0 or a subnormal)
                        const Optional<float> single = StringUtils::ParseNumber<float>(node.text);
                        const float rounded = single.has_value() ? *single : static_cast<float>(std::abs(value));
                        constant.constant = ImageOf<float>(negative ? -rounded : rounded);
                    } else {
                        return Refuse(column, Format("the number '{}' is a float literal, but {} is needed here; write an integer, or convert the other side with float(x)", node.text, Typed(type)));
                    }
                    return Emit(std::move(constant));
                }

                const Optional<uint64_t> magnitude = StringUtils::ParseNumber<uint64_t>(node.text);
                if (!magnitude.has_value()) {
                    return Refuse(column, Format("the integer '{}' does not fit an int64", node.text));
                }
                ProgramType type = ProgramType::Int32;
                if (numberHint) {
                    type = hint->program;
                } else if (*magnitude > (negative ? kInt32MagnitudeLimit : kInt32MagnitudeLimit - 1)) {
                    type = ProgramType::Int64;
                }
                constant.type = type;
                const auto signedValue = [&]() { return negative ? static_cast<int64_t>(uint64_t{0} - *magnitude) : static_cast<int64_t>(*magnitude); };
                switch (type) {
                    case ProgramType::Int32:
                        if (*magnitude > (negative ? kInt32MagnitudeLimit : kInt32MagnitudeLimit - 1)) {
                            return Refuse(column, Format("the integer '{}' does not fit an int; write it as an int64 value or a float", node.text));
                        }
                        constant.constant = ImageOf<int32_t>(static_cast<int32_t>(signedValue()));
                        break;
                    case ProgramType::Int64:
                        if (*magnitude > (negative ? kInt64MagnitudeLimit : kInt64MagnitudeLimit - 1)) {
                            return Refuse(column, Format("the integer '{}' does not fit an int64", node.text));
                        }
                        constant.constant = ImageOf<int64_t>(signedValue());
                        break;
                    case ProgramType::Float:
                        constant.constant = ImageOf<float>(negative ? -static_cast<float>(*magnitude) : static_cast<float>(*magnitude));
                        break;
                    case ProgramType::Double:
                        constant.constant = ImageOf<double>(negative ? -static_cast<double>(*magnitude) : static_cast<double>(*magnitude));
                        break;
                    default:
                        return Refuse(column, Format("a number is no {}", ProgramTypeName(type)));
                }
                return Emit(std::move(constant));
            }

            [[nodiscard]] bool LiteralIsZero(ExprNodeId id) const {
                ExprNodeId number = id;
                while (_ast[number].kind == ExprNodeKind::Unary) {
                    number = _ast[number].first;
                }
                const ExprNode& node = _ast[number];
                if (node.isFloat) {
                    const Optional<double> value = StringUtils::ParseNumber<double>(node.text);
                    return value.has_value() && *value == 0.0;
                }
                const Optional<uint64_t> value = StringUtils::ParseNumber<uint64_t>(node.text);
                return value.has_value() && *value == 0;
            }

            // Names

            TypedResult CheckName(ExprNodeId id, Optional<ExprType> hint) {
                const ExprNode& node = _ast[id];
                const StringView text = node.text;
                if (text.find('.') == StringView::npos) {
                    return CheckBareName(node, hint);
                }
                for (const ProgramType type : AllTypes()) {
                    const TypeDesc* descriptor = DescriptorOf(type);
                    if (descriptor == nullptr) {
                        continue;
                    }
                    const auto slot = LinkSlot(_declaration, text, *descriptor);
                    if (slot.has_value()) {
                        return EmitSlot(node, type, *slot);
                    }
                    if (slot.error().code != LinkErrorCode::type_mismatch) {
                        return Refuse(node.column, slot.error().message);
                    }
                }
                // An enum has no descriptor to ask for: it is the field's own
                const TypeDesc* field = FieldDescriptor(text);
                if (field != nullptr && field->kind == TypeKind::Enum) {
                    const auto slot = LinkSlot(_declaration, text, *field);
                    if (!slot.has_value()) {
                        return Refuse(node.column, slot.error().message);
                    }
                    return EmitSlot(node, ExprType::OfEnum(*field), *slot);
                }
                return Refuse(node.column, FieldOfAnotherType(text, field));
            }

            TypedNodeId EmitSlot(const ExprNode& node, const ExprType& type, SlotRef slot) {
                TypedNode typed;
                typed.kind = TypedNodeKind::Slot;
                typed.type = type;
                typed.slot = slot;
                typed.path = node.text;
                typed.column = node.column;
                return Emit(std::move(typed));
            }

            /// The type of the fixed field at the end of @p path, null when the path does not name one: the answers of `LinkSlot` stay the messages of every other case
            [[nodiscard]] const TypeDesc* FieldDescriptor(StringView path) const {
                const size_t dot = path.find('.');
                const Optional<LevelIndex> level = _declaration.IndexOf(path.substr(0, dot));
                if (!level.has_value()) {
                    return nullptr;
                }
                const auto compiled = CompilePath(*_declaration.Levels()[*level].type, path.substr(dot + 1));
                return compiled.has_value() && compiled->IsFixed() ? compiled->type : nullptr;
            }

            /// The field exists and is fixed, but none of the program types is its type: say what it is
            String FieldOfAnotherType(StringView path, const TypeDesc* field) const {
                const String fieldType = field != nullptr ? Format("of the type {}", DescribeDescriptor(*field)) : String("of another type");
                return Format("path '{}': the field is {}, and a formula reads only fields of the types {}", path, fieldType, JoinTypes(AllTypes()));
            }

            TypedResult CheckBareName(const ExprNode& node, Optional<ExprType> hint) {
                const StringView name = node.text;
                if (_declaration.IndexOf(name).has_value()) {
                    // LinkSlot says what a bare level is
                    const auto slot = LinkSlot(_declaration, name, DescribeType<int32_t>());
                    return Refuse(node.column, slot.has_value() ? Format("'{}' is a level, a path names a field of it ({}.field)", name, name) : slot.error().message);
                }
                if (FindExprFunction(name) != nullptr) {
                    return Refuse(node.column, Format("'{}' is a function: call it as {}(...)", name, name));
                }
                if (hint.has_value() && hint->program == ProgramType::Enum && hint->enumeration != nullptr) {
                    return CheckEnumerator(node, *hint);
                }
                Vector<String> known;
                String levels;
                for (const ContextLevel& level : _declaration.Levels()) {
                    known.push_back(level.name);
                    levels += levels.empty() ? "" : ", ";
                    levels += level.name;
                }
                String functions;
                for (const ExprFunction& function : kExprFunctions) {
                    known.push_back(String(function.name));
                    functions += functions.empty() ? "" : ", ";
                    functions.append(function.name.data(), function.name.size());
                }
                const String hintText = IDeserializer::MakeDidYouMeanHint(name, {&known});
                return Refuse(node.column, Format("unknown name '{}'{}; the declared levels are: {}; the functions are: {}; an enumerator is a name too, but it needs an enum beside it", name,
                                                  hintText, levels.empty() ? String("none") : levels, functions));
            }

            /// A name beside an enum is an enumerator of it (case-sensitive; a level of the same name was taken first)
            TypedResult CheckEnumerator(const ExprNode& node, const ExprType& type) {
                const TypeDesc& enumeration = *type.enumeration;
                Vector<String> known;
                for (size_t i = 0; i < enumeration.names.size(); ++i) {
                    if (enumeration.names[i] == node.text) {
                        TypedNode constant;
                        constant.type = type;
                        constant.constant = ImageOf<ProgramEnum>(ProgramEnum{static_cast<uint32_t>(i)});
                        constant.column = node.column;
                        return Emit(std::move(constant));
                    }
                    known.push_back(String(enumeration.names[i]));
                }
                for (const ContextLevel& level : _declaration.Levels()) {
                    known.push_back(level.name);
                }
                const String hintText = IDeserializer::MakeDidYouMeanHint(node.text, {&known});
                return Refuse(node.column, Format("unknown name '{}'{}; it is no level and no enumerator of {}", node.text, hintText, DescribeExprType(type)));
            }

            // Operands of one type

            /**
             * @brief Checks operands that must have one type and returns their typed nodes in source order
             *
             * Operands that read the context come first, so a literal beside them takes their type; a constant subtree beside no typed operand takes the hint, or float when
             * it holds a float literal. @p subject names the operator or function in a message.
             */
            std::expected<Vector<TypedNodeId>, ProgramError> CheckSameType(const Vector<ExprNodeId>& operands, Optional<ExprType> hint, const String& subject) {
                Vector<TypedNodeId> typed(operands.size(), 0);
                Optional<ExprType> common;
                Optional<ExprType> anchor = hint;
                bool allConstant = true;
                for (const ExprNodeId operand : operands) {
                    allConstant = allConstant && !_hasName[operand];
                }
                if (allConstant && !(hint.has_value() && IsNumberType(hint->program))) {
                    for (const ExprNodeId operand : operands) {
                        if (ContainsFloatLiteral(operand)) {
                            anchor = ProgramType::Float;
                        }
                    }
                }
                for (const bool named : {true, false}) {
                    for (size_t i = 0; i < operands.size(); ++i) {
                        if (_hasName[operands[i]] != named) {
                            continue;
                        }
                        const TypedResult result = Check(operands[i], common.has_value() ? common : anchor);
                        if (!result.has_value()) {
                            return std::unexpected(result.error());
                        }
                        typed[i] = *result;
                        if (!common.has_value()) {
                            common = TypeOf(*result);
                        }
                    }
                }
                for (size_t i = 1; i < operands.size(); ++i) {
                    if (!(TypeOf(typed[i]) == TypeOf(typed[0]))) {
                        return Refuse(_ast[operands[i]].column, Format("{} needs operands of one type, but they are {} and {}: there is no implicit conversion between types{}", subject,
                                                                       DescribeExprType(TypeOf(typed[0])), DescribeExprType(TypeOf(typed[i])), ConvertHint(TypeOf(typed[i]), TypeOf(typed[0]))));
                    }
                }
                return typed;
            }

            std::unexpected<ProgramError> NotDefined(uint32_t column, const String& subject, ProgramOperation operation, const ExprType& type) const {
                return Refuse(column, Format("{} is not defined for {}; it works on {}", subject, DescribeExprType(type), TypesWith(operation)));
            }

            // Operators

            TypedResult CheckUnary(ExprNodeId id, Optional<ExprType> hint) {
                const ExprNode& node = _ast[id];
                if (node.operation == ProgramOperation::Neg && IsLiteral(id)) {
                    return CheckLiteral(id, hint);
                }
                const String subject = Format("the operator '{}'", OperatorSymbol(node.operation));
                if (node.operation == ProgramOperation::Not) {
                    const TypedResult operand = Check(node.first, ProgramType::Bool);
                    if (!operand.has_value()) {
                        return operand;
                    }
                    if (TypeOf(*operand) != ProgramType::Bool) {
                        return Refuse(_ast[node.first].column, Format("{} needs a bool, but the operand is {}; compare it, for example with != 0", subject, Typed(TypeOf(*operand))));
                    }
                    return EmitOperation(OpcodeOf(ProgramOperation::Not, ProgramType::Bool), ProgramType::Bool, node.column, {*operand});
                }
                const TypedResult operand = Check(node.first, hint);
                if (!operand.has_value()) {
                    return operand;
                }
                const Opcode opcode = OpcodeOf(ProgramOperation::Neg, TypeOf(*operand).program);
                if (opcode == Opcode::Invalid) {
                    return NotDefined(node.column, subject, ProgramOperation::Neg, TypeOf(*operand));
                }
                return EmitOperation(opcode, TypeOf(*operand), node.column, {*operand});
            }

            TypedResult CheckBinary(ExprNodeId id, Optional<ExprType> hint) {
                const ExprNode& node = _ast[id];
                const ProgramOperation operation = node.operation;
                const String subject = Format("the operator '{}'", OperatorSymbol(operation));
                if (IsLogic(operation)) {
                    TypedNodeId operands[2] = {};
                    const ExprNodeId sources[2] = {node.first, node.second};
                    for (size_t i = 0; i < 2; ++i) {
                        const TypedResult operand = Check(sources[i], ProgramType::Bool);
                        if (!operand.has_value()) {
                            return operand;
                        }
                        if (TypeOf(*operand) != ProgramType::Bool) {
                            return Refuse(_ast[sources[i]].column,
                                          Format("{} needs bool operands, but the {} one is {}; compare it, for example with != 0", subject, i == 0 ? "left" : "right", Typed(TypeOf(*operand))));
                        }
                        operands[i] = *operand;
                    }
                    return EmitOperation(OpcodeOf(operation, ProgramType::Bool), ProgramType::Bool, node.column, {operands[0], operands[1]});
                }
                if ((operation == ProgramOperation::Div || operation == ProgramOperation::Mod) && IsLiteral(node.second) && LiteralIsZero(node.second)) {
                    return Refuse(_ast[node.second].column, Format("{} by a literal zero", operation == ProgramOperation::Div ? "division" : "modulo"));
                }
                const auto operands = CheckSameType({node.first, node.second}, IsComparison(operation) ? Optional<ExprType>{} : hint, subject);
                if (!operands.has_value()) {
                    return std::unexpected(operands.error());
                }
                const ExprType common = TypeOf((*operands)[0]);
                const Opcode opcode = OpcodeOf(operation, common.program);
                if (opcode == Opcode::Invalid) {
                    return NotDefined(node.column, subject, operation, common);
                }
                return EmitOperation(opcode, IsComparison(operation) ? ProgramType::Bool : common, node.column, {(*operands)[0], (*operands)[1]});
            }

            // Functions

            TypedResult CheckCall(ExprNodeId id, Optional<ExprType> hint) {
                const ExprNode& node = _ast[id];
                const ExprFunction* function = FindExprFunction(node.text);
                if (function == nullptr) {
                    if (_declaration.IndexOf(node.text).has_value()) {
                        return Refuse(node.column, Format("'{}' is a level, not a function", node.text));
                    }
                    String names;
                    for (const ExprFunction& known : kExprFunctions) {
                        names += names.empty() ? "" : ", ";
                        names.append(known.name.data(), known.name.size());
                    }
                    return Refuse(node.column, Format("unknown function '{}'; the functions are: {}", node.text, names));
                }
                if (node.argumentCount != function->arity) {
                    return Refuse(node.column, Format("'{}' takes {} argument{}, but {} {} given", function->name, function->arity, function->arity == 1 ? "" : "s", node.argumentCount,
                                                      node.argumentCount == 1 ? "is" : "are"));
                }
                Vector<ExprNodeId> arguments;
                for (uint32_t i = 0; i < node.argumentCount; ++i) {
                    arguments.push_back(_ast.arguments[node.firstArgument + i]);
                }
                const String subject = Format("the function '{}'", function->name);
                switch (function->shape) {
                    case ExprFunctionShape::Convert:
                        return CheckConversion(node, *function, arguments[0]);
                    case ExprFunctionShape::Select:
                        return CheckSelect(node, arguments, hint, subject);
                    case ExprFunctionShape::SameType:
                        break;
                }
                const auto typed = CheckSameType(arguments, hint, subject);
                if (!typed.has_value()) {
                    return std::unexpected(typed.error());
                }
                const ExprType type = TypeOf((*typed)[0]);
                const Opcode opcode = OpcodeOf(function->operation, type.program);
                if (opcode == Opcode::Invalid) {
                    return NotDefined(node.column, subject, function->operation, type);
                }
                TypedNode result;
                result.kind = TypedNodeKind::Operation;
                result.type = type;
                result.opcode = opcode;
                result.column = node.column;
                for (const TypedNodeId operand : *typed) {
                    result.operands[result.operandCount++] = operand;
                }
                return Emit(std::move(result));
            }

            TypedResult CheckSelect(const ExprNode& node, const Vector<ExprNodeId>& arguments, Optional<ExprType> hint, const String& subject) {
                const TypedResult condition = Check(arguments[0], ProgramType::Bool);
                if (!condition.has_value()) {
                    return condition;
                }
                if (TypeOf(*condition) != ProgramType::Bool) {
                    return Refuse(_ast[arguments[0]].column, Format("{} needs a bool as its first argument, but it is {}", subject, Typed(TypeOf(*condition))));
                }
                const auto values = CheckSameType({arguments[1], arguments[2]}, hint, subject);
                if (!values.has_value()) {
                    return std::unexpected(values.error());
                }
                const ExprType type = TypeOf((*values)[0]);
                return EmitOperation(OpcodeOf(ProgramOperation::Select, type.program), type, node.column, {*condition, (*values)[0], (*values)[1]});
            }

            /// A literal argument is converted from what it spells: an integer literal as the target type, a float literal as a double, so `double(0.1)` is exact
            TypedResult CheckConversion(const ExprNode& node, const ExprFunction& function, ExprNodeId argument) {
                Optional<ExprType> hint;
                if (IsLiteral(argument)) {
                    hint = ContainsFloatLiteral(argument) ? ProgramType::Double : function.target;
                }
                const TypedResult operand = Check(argument, hint);
                if (!operand.has_value()) {
                    return operand;
                }
                const ProgramType from = TypeOf(*operand).program;
                if (!IsNumberType(from)) {
                    return Refuse(_ast[argument].column, Format("'{}' converts a number, but the argument is {}", function.name, Typed(TypeOf(*operand))));
                }
                if (from == function.target) {
                    return *operand;
                }
                return EmitOperation(ConversionOpcode(from, function.target), function.target, node.column, {*operand});
            }

            const ExprAst& _ast;
            const ContextDecl& _declaration;
            Vector<bool> _hasName;
            TypedExpr _out;
        };

    }  // namespace

    std::expected<ExprType, ProgramError> CheckExpectedType(const TypeDesc& expected) {
        const Optional<ExprType> want = ProgramTypeFor(expected);
        if (!want.has_value()) {
            return Refuse(0, Format("a formula can have the type {}, not '{}'", JoinTypes(AllTypes()), DescribeDescriptor(expected)));
        }
        return *want;
    }

    std::expected<TypedExpr, ProgramError> CheckExpression(const ExprAst& ast, const ContextDecl& declaration, const TypeDesc& expected) {
        Checker checker(ast, declaration);
        return checker.Run(expected);
    }

}  // namespace BECore
