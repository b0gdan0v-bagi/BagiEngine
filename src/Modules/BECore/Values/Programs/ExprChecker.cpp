#include "ExprChecker.h"

#include <BECore/Reflection/CompiledPath.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/ExprFunctions.h>
#include <BECore/Values/Programs/Kernels.h>
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

        /// The pairs of different types an operation has a row for, as a message lists them (`; mixed pairs: vec3 * float, ...`); empty when it has none
        String MixedPairsOf(ProgramOperation operation) {
            String text;
            for (const ProgramType lhs : AllTypes()) {
                for (const ProgramType rhs : AllTypes()) {
                    if (MixedOpcodeOf(operation, lhs, rhs) != Opcode::Invalid) {
                        text += text.empty() ? "; mixed pairs: " : ", ";
                        text += Format("{} {} {}", ProgramTypeName(lhs), OperatorSymbol(operation), ProgramTypeName(rhs));
                    }
                }
            }
            return text;
        }

        /// Whether the type is an operand of the operation in any row, same-type or mixed
        bool TakesPartIn(ProgramOperation operation, ProgramType type) {
            if (OpcodeOf(operation, type) != Opcode::Invalid) {
                return true;
            }
            for (const ProgramType other : AllTypes()) {
                if (MixedOpcodeOf(operation, type, other) != Opcode::Invalid || MixedOpcodeOf(operation, other, type) != Opcode::Invalid) {
                    return true;
                }
            }
            return false;
        }

        /// The scalar a vector is scaled by, the type a literal beside the vector takes in `*` and `/`
        Optional<ProgramType> ScalarOfVector(ProgramType type) {
            switch (type) {
                case ProgramType::Vec2:
                case ProgramType::Vec3:
                case ProgramType::Vec4:
                    return ProgramType::Float;
                case ProgramType::IVec2:
                    return ProgramType::Int32;
                default:
                    return std::nullopt;
            }
        }

        /// The types that have components, as the message of a component of another type lists them
        String TypesWithComponents() {
            Vector<ProgramType> types;
            for (const ProgramType type : AllTypes()) {
                if (ComponentsOf(type).count > 0) {
                    types.push_back(type);
                }
            }
            return JoinTypes(types);
        }

        String JoinComponentNames(const ComponentSet& set) {
            String text;
            for (uint32_t i = 0; i < set.count; ++i) {
                text += i == 0 ? "" : ", ";
                text.append(set.names[i].data(), set.names[i].size());
            }
            return text;
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

        /// The names of the functions, `at` and `ByLevel` included, as a message lists them
        Vector<String> FunctionNames() {
            Vector<String> names;
            for (const ExprFunction& function : kExprFunctions) {
                names.push_back(String(function.name));
            }
            names.push_back(String(kTableFunction));
            names.push_back(String(kNamedTableFunction));
            for (const ExprListFunction& function : kListFunctions) {
                if (FindExprFunction(function.name) == nullptr) {
                    names.push_back(String(function.name));
                }
            }
            return names;
        }

        String JoinNames(const Vector<String>& names) {
            String text;
            for (const String& name : names) {
                text += text.empty() ? "" : ", ";
                text += name;
            }
            return text;
        }

        bool IsComparison(ProgramOperation operation) {
            return operation == ProgramOperation::Eq || operation == ProgramOperation::Ne || operation == ProgramOperation::Lt || operation == ProgramOperation::Le ||
                   operation == ProgramOperation::Gt || operation == ProgramOperation::Ge;
        }

        bool IsArithmetic(ProgramOperation operation) {
            return operation == ProgramOperation::Add || operation == ProgramOperation::Sub || operation == ProgramOperation::Mul || operation == ProgramOperation::Div ||
                   operation == ProgramOperation::Mod;
        }

        bool IsLogic(ProgramOperation operation) {
            return operation == ProgramOperation::And || operation == ProgramOperation::Or;
        }

        /// What a name or a path of a formula stands for when it is not a value: the node (a `List` or an `Element`, else any node) and what the list holds. `list` is the type of the
        /// list (of an `Element`, the list it is an element of) and `element` the class of its elements, null for a list of scalars
        struct Reference {
            TypedNodeId id = 0;
            const TypeDesc* list = nullptr;
            const TypeInfo* element = nullptr;
        };

        using ReferenceResult = std::expected<Reference, ProgramError>;

        /// A loop variable in scope: the body of its fold only
        struct LoopScope {
            String name;
            uint32_t loop = 0;
            const TypeDesc* list = nullptr;
            const TypeInfo* element = nullptr;
        };

        class Checker {
        public:
            Checker(const ExprAst& ast, const ContextDecl& declaration, const CompileOptions& options) : _ast(ast), _declaration(declaration), _options(options) {
                // The children of a node come before it, so one pass in order sees the children's answers
                _hasName.resize(ast.nodes.size(), false);
                // A loop variable is read from the list, so a bare name that is one gives its operand a type as a path does (`c * 2` over a list of `int`). The variable comes after the
                // body in the node order, so the names are collected first; a bare name of the same text that is no variable counts too, which only makes it checked earlier
                Vector<StringView> variables;
                for (const ExprNode& node : ast.nodes) {
                    const ExprListFunction* function = node.kind == ExprNodeKind::Call ? FindListFunction(node.text) : nullptr;
                    if (function != nullptr && function->isFold && node.argumentCount == function->arity && ast[ast.arguments[node.firstArgument + 1]].kind == ExprNodeKind::Name) {
                        variables.push_back(ast[ast.arguments[node.firstArgument + 1]].text);
                    }
                }
                for (size_t i = 0; i < ast.nodes.size(); ++i) {
                    const ExprNode& node = ast.nodes[i];
                    switch (node.kind) {
                        case ExprNodeKind::Name:
                            // A bare name is a level, a function or an enumerator: only a path reads the context and gives its operand a type to the others
                            _hasName[i] = node.text.find('.') != String::npos || std::find(variables.begin(), variables.end(), StringView(node.text)) != variables.end();
                            break;
                        case ExprNodeKind::Call:
                            // The type of a `ByLevel` is the one beside it, however its index is read, so it waits for the operands that have a type of their own
                            if (node.text == kNamedTableFunction) {
                                break;
                            }
                            for (uint32_t a = 0; a < node.argumentCount; ++a) {
                                _hasName[i] = _hasName[i] || _hasName[ast.arguments[node.firstArgument + a]];
                            }
                            break;
                        case ExprNodeKind::Unary:
                        case ExprNodeKind::Member:
                        case ExprNodeKind::Index:
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
                if (node.type != want) {
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
                        return AsValue(id, ResolveMember(id));
                    case ExprNodeKind::Unary:
                        return CheckUnary(id, hint);
                    case ExprNodeKind::Binary:
                        return CheckBinary(id, hint);
                    case ExprNodeKind::Index:
                        return AsValue(id, CheckIndex(id));
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

            /// Numbers and arithmetic over them only: such a subtree has no type of its own and takes the one of the operand beside it
            [[nodiscard]] bool IsUntypedNumber(ExprNodeId id) const {
                const ExprNode& node = _ast[id];
                switch (node.kind) {
                    case ExprNodeKind::Number:
                        return true;
                    case ExprNodeKind::Unary:
                        return node.operation == ProgramOperation::Neg && IsUntypedNumber(node.first);
                    case ExprNodeKind::Binary:
                        return IsArithmetic(node.operation) && IsUntypedNumber(node.first) && IsUntypedNumber(node.second);
                    default:
                        return false;
                }
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

            /// A name or a path as a value: a list or an element is refused with the way to read it
            TypedResult CheckName(ExprNodeId id, Optional<ExprType> hint) {
                return AsValue(id, ResolveName(id, hint));
            }

            /// What a name or a path stands for: a loop variable (or a field of its element), a field of a level, or a list
            ReferenceResult ResolveName(ExprNodeId id, Optional<ExprType> hint) {
                const ExprNode& node = _ast[id];
                const StringView text = node.text;
                const size_t dot = text.find('.');
                if (const LoopScope* scope = FindScope(text.substr(0, dot))) {
                    return ResolveVariable(node, *scope, dot == StringView::npos ? StringView() : text.substr(dot + 1));
                }
                if (dot == StringView::npos) {
                    return Wrap(CheckBareName(node, hint));
                }
                const TypeDesc* field = FieldDescriptor(text);
                if (field == nullptr) {
                    if (const Optional<TypedResult> component = CheckComponentPath(node)) {
                        return Wrap(*component);
                    }
                    // The refusal of a path that names no fixed field does not depend on the type asked for
                    const auto refused = LinkSlot(_declaration, text, DescribeType<int32_t>());
                    return Refuse(node.column, refused.has_value() ? FieldOfAnotherType(text, nullptr) : refused.error().message);
                }
                if (field->kind == TypeKind::List) {
                    const auto list = LinkList(_declaration, text);
                    if (!list.has_value()) {
                        return Refuse(node.column, list.error().message);
                    }
                    return EmitList(text, *list->list, list->offset, SlotRef{list->level, list->offset}, std::nullopt, node.column);
                }
                const Optional<ExprType> type = ProgramTypeFor(*field);
                if (!type.has_value()) {
                    return Refuse(node.column, FieldOfAnotherType(text, field));
                }
                const auto slot = LinkSlot(_declaration, text, *field);
                if (!slot.has_value()) {
                    return Refuse(node.column, slot.error().message);
                }
                return Reference{EmitSlot(node.text, *type, *slot, node.column)};
            }

            static ReferenceResult Wrap(const TypedResult& result) {
                if (!result.has_value()) {
                    return std::unexpected(result.error());
                }
                return Reference{*result};
            }

            [[nodiscard]] const LoopScope* FindScope(StringView name) const {
                for (const LoopScope& scope : _scopes) {
                    if (scope.name == name) {
                        return &scope;
                    }
                }
                return nullptr;
            }

            /// The source of a node as a message names it: the path, `list[0]`, `f(...)`; what has no short form is "the expression"
            [[nodiscard]] String Describe(ExprNodeId id) const {
                const ExprNode& node = _ast[id];
                switch (node.kind) {
                    case ExprNodeKind::Name:
                    case ExprNodeKind::Number:
                        return node.text;
                    case ExprNodeKind::Member:
                        return Format("{}.{}", Describe(node.first), node.text);
                    case ExprNodeKind::Index: {
                        const ExprNodeKind kind = _ast[node.second].kind;
                        const bool simple = kind == ExprNodeKind::Name || kind == ExprNodeKind::Number;
                        return Format("{}[{}]", Describe(node.first), simple ? Describe(node.second) : String("..."));
                    }
                    case ExprNodeKind::Call:
                        return Format("{}(...)", node.text);
                    default:
                        return "the expression";
                }
            }

            /// What a list holds as a message names it: the class of its elements or the type of its scalars
            static String DescribeElements(const Reference& list) {
                return list.element != nullptr ? String(list.element->Name()) : DescribeDescriptor(*list.list->element);
            }

            /// A reference where a value is needed: a list and an element are not values, and the message says how to read them
            TypedResult AsValue(ExprNodeId id, const ReferenceResult& reference) {
                if (!reference.has_value()) {
                    return std::unexpected(reference.error());
                }
                const TypedNode& node = _out.nodes[reference->id];
                const String text = Describe(id);
                if (node.kind == TypedNodeKind::List) {
                    return Refuse(_ast[id].column, Format("'{}' is a list of {}, not a value: size({}) is its length, {}[i] its element i, and count, any, all, sum, min and max over it give values",
                                                          text, DescribeElements(*reference), text, text));
                }
                if (node.kind == TypedNodeKind::Element) {
                    return Refuse(_ast[id].column, Format("'{}' is {}, an element of a list, not a value: read a field of it, for example {}.{}", text, DescribeElements(*reference), text,
                                                          FirstFieldName(*reference->element)));
                }
                return reference->id;
            }

            static String FirstFieldName(const TypeInfo& type) {
                const Vector<FieldRef> fields = TypeRegistry::GetInstance().AllFields(type);
                return fields.empty() ? String("field") : String(fields.front().field->name);
            }

            TypedNodeId EmitSlot(StringView path, const ExprType& type, SlotRef slot, uint32_t column) {
                TypedNode typed;
                typed.kind = TypedNodeKind::Slot;
                typed.type = type;
                typed.slot = slot;
                typed.path = String(path);
                typed.column = column;
                return Emit(std::move(typed));
            }

            // Components

            /// The index of the component @p name of @p type, or the error that says what the type has
            std::expected<uint32_t, ProgramError> ComponentIndex(const ExprType& type, StringView name, uint32_t column) const {
                const ComponentSet set = ComponentsOf(type.program);
                if (set.count == 0) {
                    return Refuse(column, Format("'.{}': {} has no components; only {} have them", name, Typed(type), TypesWithComponents()));
                }
                const Optional<uint32_t> index = set.Find(name);
                if (!index.has_value()) {
                    return Refuse(column, Format("'.{}' is not a component of {}; it has {}", name, DescribeExprType(type), JoinComponentNames(set)));
                }
                return *index;
            }

            /// The component @p index of the value of @p base, computed by the program
            TypedNodeId EmitExtract(TypedNodeId base, const ExprType& type, uint32_t index, uint32_t column) {
                const TypedNodeId id = EmitOperation(OpcodeOf(ProgramOperation::Extract, type.program), ComponentsOf(type.program).type, column, {base});
                _out.nodes[id].immediate = index;
                return id;
            }

            /// A component of an expression (`lerp(a, b, t).y`) or a field of an element: the members after a base are read together, `list[i].a.b` is one path through the element's type
            ReferenceResult ResolveMember(ExprNodeId id) {
                Vector<const ExprNode*> members;
                ExprNodeId bottom = id;
                while (_ast[bottom].kind == ExprNodeKind::Member) {
                    members.push_back(&_ast[bottom]);
                    bottom = _ast[bottom].first;
                }
                std::reverse(members.begin(), members.end());
                const ReferenceResult base = CheckReference(bottom);
                if (!base.has_value()) {
                    return base;
                }
                if (_out.nodes[base->id].kind == TypedNodeKind::Element) {
                    String path;
                    for (const ExprNode* member : members) {
                        path += path.empty() ? "" : ".";
                        path += member->text;
                    }
                    return ResolveInElement(*base, path, members.front()->column, Describe(id));
                }
                const TypedResult value = AsValue(bottom, base);
                if (!value.has_value()) {
                    return std::unexpected(value.error());
                }
                TypedNodeId current = *value;
                for (const ExprNode* member : members) {
                    const ExprType type = TypeOf(current);
                    const auto index = ComponentIndex(type, member->text, member->column);
                    if (!index.has_value()) {
                        return std::unexpected(index.error());
                    }
                    current = EmitExtract(current, type, *index, member->column);
                }
                return Reference{current};
            }

            /// Any node as what it stands for: a list or an element where the syntax makes one, else the value
            ReferenceResult CheckReference(ExprNodeId id) {
                switch (_ast[id].kind) {
                    case ExprNodeKind::Name:
                        return ResolveName(id, std::nullopt);
                    case ExprNodeKind::Member:
                        return ResolveMember(id);
                    case ExprNodeKind::Index:
                        return CheckIndex(id);
                    default:
                        return Wrap(Check(id, std::nullopt));
                }
            }

            // Lists and elements

            /// The list node of a list field: of a level (@p slot) or of the element @p owner
            ReferenceResult EmitList(StringView path, const TypeDesc& listType, uint32_t offset, SlotRef slot, Optional<TypedNodeId> owner, uint32_t column) {
                const auto element = DescribeListElement(listType);
                if (!element.has_value()) {
                    return Refuse(column, Format("path '{}': {}", path, element.error().message));
                }
                const auto layout = MakeListDescriptor(offset, listType);
                if (!layout.has_value()) {
                    return Refuse(column, Format("path '{}': {}", path, layout.error()));
                }
                TypedNode node;
                node.kind = TypedNodeKind::List;
                node.slot = slot;
                node.path = String(path);
                node.list = *layout;
                node.column = column;
                if (owner.has_value()) {
                    node.operands[node.operandCount++] = *owner;
                }
                return Reference{Emit(std::move(node)), &listType, *element};
            }

            /// The read of the field at @p offset of an element, of the type @p type
            TypedNodeId EmitElementValue(TypedNodeId element, const ExprType& type, uint32_t offset, uint32_t column) {
                const TypedNodeId id = EmitOperation(LoadElemOpcode(type.program), type, column, {element});
                _out.nodes[id].immediate = offset;
                return id;
            }

            /// The component @p index of the field of @p type at @p offset of an element: a read at the component's offset where the field stores it as the program computes with it
            TypedResult EmitElementComponent(TypedNodeId element, const ExprType& type, uint32_t offset, uint32_t index, uint32_t column) {
                const ComponentSet set = ComponentsOf(type.program);
                if (set.stride != ProgramTypeSize(set.type)) {
                    return EmitExtract(EmitElementValue(element, type, offset, column), type, index, column);
                }
                const uint64_t at = uint64_t{offset} + set.Offset(index);
                if (at > std::numeric_limits<uint32_t>::max()) {
                    return Refuse(column, Format("the field is {} bytes into its element, more than a read can address", at));
                }
                return EmitElementValue(element, ExprType(set.type), static_cast<uint32_t>(at), column);
            }

            /// A loop variable and what follows it (`v`, `v.suit`, `v.tiles`, `v.position.x`)
            ReferenceResult ResolveVariable(const ExprNode& node, const LoopScope& scope, StringView path) {
                TypedNode variable;
                variable.kind = TypedNodeKind::Element;
                variable.loop = scope.loop;
                variable.column = node.column;
                return ResolveInElement(Reference{Emit(std::move(variable)), scope.list, scope.element}, path, node.column, node.text);
            }

            /**
             * What @p path (the fields after an element, `a.b.c`) stands for in the element @p element of a list: a value (a `LoadElem`), a list of the element, or the element itself
             * for an empty path of an object. An element that is a scalar is the value read at its start, and `.x` is a component of it. @p text names the source in messages.
             */
            ReferenceResult ResolveInElement(const Reference& element, StringView path, uint32_t column, StringView text) {
                const TypeDesc& elementType = *element.list->element;
                if (element.element == nullptr) {
                    const Optional<ExprType> type = ProgramTypeFor(elementType);
                    if (!type.has_value()) {
                        return Refuse(column,
                                      Format("'{}': the elements of this list are {}, and a formula reads only values of the types {}", text, DescribeDescriptor(elementType), JoinTypes(AllTypes())));
                    }
                    if (path.empty()) {
                        return Reference{EmitElementValue(element.id, *type, 0, column)};
                    }
                    const auto index = ComponentIndex(*type, path, column);
                    if (!index.has_value()) {
                        return std::unexpected(index.error());
                    }
                    return Wrap(EmitElementComponent(element.id, *type, 0, *index, column));
                }
                if (path.empty()) {
                    return element;
                }
                const auto link = LinkElementField(*element.element, path);
                if (!link.has_value()) {
                    if (const Optional<TypedResult> component = CheckElementComponent(element, path, column)) {
                        return Wrap(*component);
                    }
                    return Refuse(column, link.error().message);
                }
                if (link->type->kind == TypeKind::List) {
                    return EmitList(text, *link->type, link->offset, SlotRef{}, element.id, column);
                }
                const Optional<ExprType> type = ProgramTypeFor(*link->type);
                if (!type.has_value()) {
                    return Refuse(column, FieldOfAnotherType(text, link->type));
                }
                return Reference{EmitElementValue(element.id, *type, link->offset, column)};
            }

            /// `position.x` of an element, as `CheckComponentPath` reads `caster.position.x`: null when the path is no field of a type with components and one more segment
            Optional<TypedResult> CheckElementComponent(const Reference& element, StringView path, uint32_t column) {
                const size_t last = path.rfind('.');
                if (last == StringView::npos) {
                    return std::nullopt;
                }
                const auto prefix = LinkElementField(*element.element, path.substr(0, last));
                const Optional<ExprType> type = prefix.has_value() ? ProgramTypeFor(*prefix->type) : std::nullopt;
                if (!type.has_value()) {
                    return std::nullopt;
                }
                const auto index = ComponentIndex(*type, path.substr(last + 1), column);
                if (!index.has_value()) {
                    return TypedResult(std::unexpected(index.error()));
                }
                return EmitElementComponent(element.id, *type, prefix->offset, *index, column);
            }

            /// `list[i]`: the element, or the value of an element that is a scalar
            ReferenceResult CheckIndex(ExprNodeId id) {
                const ExprNode& node = _ast[id];
                const ReferenceResult base = CheckReference(node.first);
                if (!base.has_value()) {
                    return base;
                }
                const TypedNode& baseNode = _out.nodes[base->id];
                if (baseNode.kind != TypedNodeKind::List) {
                    const String what = baseNode.kind == TypedNodeKind::Element ? Format("{}, an element of a list", DescribeElements(*base)) : Typed(baseNode.type);
                    return Refuse(node.column, Format("'{}' is {} and has no elements: '[' reads an element of a list", Describe(node.first), what));
                }
                const TypedResult index = CheckTableIndex(node.second, Format("the list '{}'", Describe(node.first)));
                if (!index.has_value()) {
                    return std::unexpected(index.error());
                }
                TypedNode element;
                element.kind = TypedNodeKind::Element;
                element.column = node.column;
                element.operands[element.operandCount++] = base->id;
                element.operands[element.operandCount++] = *index;
                return ResolveInElement(Reference{Emit(std::move(element)), base->list, base->element}, StringView(), node.column, Describe(id));
            }

            /**
             * A path that names no field as a whole may be a field of a type with components and one more segment (`caster.velocity.x`, `caster.span.offset.x`). Where the field
             * stores the component as the program computes with it, the component is a slot of its own at its offset: one load. A `Color` channel is a byte, so it is an `Extract` of
             * the field. Null when the path is no such thing (the message of the whole path is then the better one).
             */
            Optional<TypedResult> CheckComponentPath(const ExprNode& node) {
                const StringView text = node.text;
                const size_t last = text.rfind('.');
                if (last == text.find('.')) {
                    return std::nullopt;
                }
                const StringView prefix = text.substr(0, last);
                const StringView name = text.substr(last + 1);
                const TypeDesc* field = FieldDescriptor(prefix);
                const Optional<ExprType> type = field != nullptr ? ProgramTypeFor(*field) : std::nullopt;
                if (!type.has_value()) {
                    return std::nullopt;
                }
                const auto index = ComponentIndex(*type, name, node.column);
                if (!index.has_value()) {
                    return TypedResult(std::unexpected(index.error()));
                }
                const ComponentSet set = ComponentsOf(type->program);
                const auto slot = LinkSlot(_declaration, prefix, *field);
                if (!slot.has_value()) {
                    return Refuse(node.column, slot.error().message);
                }
                if (set.stride != ProgramTypeSize(set.type)) {
                    return EmitExtract(EmitSlot(prefix, *type, *slot, node.column), *type, *index, node.column);
                }
                const uint64_t offset = uint64_t{slot->offset} + set.Offset(*index);
                if (offset > std::numeric_limits<uint32_t>::max()) {
                    return Refuse(node.column, Format("path '{}': the field is {} bytes into its level, more than a slot can address", text, offset));
                }
                return EmitSlot(text, ExprType(set.type), SlotRef{slot->level, static_cast<uint32_t>(offset)}, node.column);
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
                if (FindExprFunction(name) != nullptr || FindListFunction(name) != nullptr || name == kTableFunction || name == kNamedTableFunction) {
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
                const Vector<String> functionNames = FunctionNames();
                known.insert(known.end(), functionNames.begin(), functionNames.end());
                const String functions = JoinNames(functionNames);
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

            /// The hint for the operand after one of type @p common: in `*` and `/` a literal beside a vector is the scalar the vector is scaled by
            static ExprType BesideHint(const ExprType& common, bool scalarBesideVector) {
                if (scalarBesideVector) {
                    if (const Optional<ProgramType> scalar = ScalarOfVector(common.program)) {
                        return ExprType(*scalar);
                    }
                }
                return common;
            }

            /**
             * @brief Checks operands and returns their typed nodes in source order, whatever types they have
             *
             * The operands that read the context come first, then the other typed ones, then the subtrees of numbers alone, so a literal beside any of them takes its type; a
             * constant subtree beside no typed operand takes the hint, or float when it holds a float literal.
             */
            std::expected<Vector<TypedNodeId>, ProgramError> CheckOperands(const Vector<ExprNodeId>& operands, Optional<ExprType> hint, bool scalarBesideVector) {
                constexpr int kPasses = 3;
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
                for (int pass = 0; pass < kPasses; ++pass) {
                    for (size_t i = 0; i < operands.size(); ++i) {
                        const int own = _hasName[operands[i]] ? 0 : (IsUntypedNumber(operands[i]) ? 2 : 1);
                        if (own != pass) {
                            continue;
                        }
                        const TypedResult result = Check(operands[i], common.has_value() ? Optional<ExprType>(BesideHint(*common, scalarBesideVector)) : anchor);
                        if (!result.has_value()) {
                            return std::unexpected(result.error());
                        }
                        typed[i] = *result;
                        if (!common.has_value()) {
                            common = TypeOf(*result);
                        }
                    }
                }
                return typed;
            }

            /// The error at the first operand whose type is not the first one's
            std::expected<void, ProgramError> RequireSameType(const Vector<ExprNodeId>& operands, const Vector<TypedNodeId>& typed, const String& subject) const {
                for (size_t i = 1; i < operands.size(); ++i) {
                    if (TypeOf(typed[i]) != TypeOf(typed[0])) {
                        return Refuse(_ast[operands[i]].column, Format("{} needs operands of one type, but they are {} and {}: there is no implicit conversion between types{}", subject,
                                                                       DescribeExprType(TypeOf(typed[0])), DescribeExprType(TypeOf(typed[i])), ConvertHint(TypeOf(typed[i]), TypeOf(typed[0]))));
                    }
                }
                return {};
            }

            /// Operands that must have one type: @p subject names the operator or function in a message
            std::expected<Vector<TypedNodeId>, ProgramError> CheckSameType(const Vector<ExprNodeId>& operands, Optional<ExprType> hint, const String& subject) {
                auto typed = CheckOperands(operands, hint, false);
                if (!typed.has_value()) {
                    return typed;
                }
                if (const auto same = RequireSameType(operands, *typed, subject); !same.has_value()) {
                    return std::unexpected(same.error());
                }
                return typed;
            }

            std::unexpected<ProgramError> NotDefined(uint32_t column, const String& subject, ProgramOperation operation, const ExprType& type) const {
                return Refuse(column, Format("{} is not defined for {}; it works on {}{}", subject, DescribeExprType(type), TypesWith(operation), MixedPairsOf(operation)));
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
                if (IsArithmetic(operation)) {
                    return CheckArithmetic(node, hint, subject);
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

            /// `+ - * / %`: two operands of one type, or a pair of `BE_PROGRAM_MIXED_ROWS` (`vec3 * float`), whose literal side is the scalar of the vector
            TypedResult CheckArithmetic(const ExprNode& node, Optional<ExprType> hint, const String& subject) {
                const ProgramOperation operation = node.operation;
                const Vector<ExprNodeId> sources{node.first, node.second};
                const auto operands = CheckOperands(sources, hint, operation == ProgramOperation::Mul || operation == ProgramOperation::Div);
                if (!operands.has_value()) {
                    return std::unexpected(operands.error());
                }
                const ExprType left = TypeOf((*operands)[0]);
                const ExprType right = TypeOf((*operands)[1]);
                if (left == right) {
                    const Opcode opcode = OpcodeOf(operation, left.program);
                    if (opcode == Opcode::Invalid) {
                        return NotDefined(node.column, subject, operation, left);
                    }
                    return EmitOperation(opcode, left, node.column, {(*operands)[0], (*operands)[1]});
                }
                const Opcode mixed = left.program != right.program ? MixedOpcodeOf(operation, left.program, right.program) : Opcode::Invalid;
                if (mixed != Opcode::Invalid) {
                    return EmitOperation(mixed, ExprType(*ResultTypeOf(mixed)), node.column, {(*operands)[0], (*operands)[1]});
                }
                if (!TakesPartIn(operation, left.program)) {
                    return NotDefined(node.column, subject, operation, left);
                }
                return Refuse(_ast[node.second].column, Format("{} needs operands of one type, but they are {} and {}: there is no implicit conversion between types{}{}", subject,
                                                               DescribeExprType(left), DescribeExprType(right), ConvertHint(right, left), MixedPairsOf(operation)));
            }

            // Functions

            TypedResult CheckCall(ExprNodeId id, Optional<ExprType> hint) {
                const ExprNode& node = _ast[id];
                if (node.text == kTableFunction) {
                    return CheckTable(node, hint);
                }
                if (node.text == kNamedTableFunction) {
                    return CheckNamedTable(node, hint);
                }
                const ExprFunction* function = FindExprFunction(node.text);
                const ExprListFunction* listFunction = FindListFunction(node.text);
                // `min` and `max` are in both tables: three arguments are a fold, any other count is the row of the numbers and its arity error
                if (listFunction != nullptr && (function == nullptr || node.argumentCount == listFunction->arity)) {
                    return CheckListFunction(node, *listFunction);
                }
                if (function == nullptr) {
                    if (_declaration.IndexOf(node.text).has_value()) {
                        return Refuse(node.column, Format("'{}' is a level, not a function", node.text));
                    }
                    return Refuse(node.column, Format("unknown function '{}'; the functions are: {}", node.text, JoinNames(FunctionNames())));
                }
                if (node.argumentCount != function->arity) {
                    return Refuse(node.column, ArityMessage(node, function->arity));
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
                    case ExprFunctionShape::Pack:
                        return CheckPack(node, *function, arguments, subject);
                    case ExprFunctionShape::Blend:
                        return CheckBlend(node, arguments, hint, subject);
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
                // The row says what it gives: `length` of a vector is a float
                const ProgramType resultType = *ResultTypeOf(opcode);
                TypedNode result;
                result.kind = TypedNodeKind::Operation;
                result.type = resultType == type.program ? type : ExprType(resultType);
                result.opcode = opcode;
                result.column = node.column;
                for (const TypedNodeId operand : *typed) {
                    result.operands[result.operandCount++] = operand;
                }
                return Emit(std::move(result));
            }

            static String ArityMessage(const ExprNode& node, uint32_t arity) {
                return Format("'{}' takes {} argument{}, but {} {} given", node.text, arity, arity == 1 ? "" : "s", node.argumentCount, node.argumentCount == 1 ? "is" : "are");
            }

            /// `size(list)` and the folds; a fold is told apart from the two-argument `min` and `max` by its arity before it gets here
            TypedResult CheckListFunction(const ExprNode& node, const ExprListFunction& function) {
                if (node.argumentCount != function.arity) {
                    return Refuse(node.column, ArityMessage(node, function.arity));
                }
                const String subject = Format("the function '{}'", function.name);
                if (function.isFold) {
                    return Refuse(node.column, Format("{} is not checked yet", subject));
                }
                const auto list = RequireList(_ast.arguments[node.firstArgument], subject);
                if (!list.has_value()) {
                    return std::unexpected(list.error());
                }
                return EmitOperation(Opcode::ListSize, ProgramType::Int32, node.column, {list->id});
            }

            /// The first argument of a list function: a list, or the error that says what it is instead
            ReferenceResult RequireList(ExprNodeId source, const String& subject) {
                const ReferenceResult reference = CheckReference(source);
                if (!reference.has_value()) {
                    return reference;
                }
                const TypedNode& node = _out.nodes[reference->id];
                if (node.kind == TypedNodeKind::List) {
                    return reference;
                }
                const String what = node.kind == TypedNodeKind::Element ? Format("{}, an element of a list", DescribeElements(*reference)) : Typed(node.type);
                return Refuse(_ast[source].column, Format("{} needs a list as its first argument, but '{}' is {}", subject, Describe(source), what));
            }

            /// The index of `at` or `ByLevel`: an `int`, with a hint to convert a number of another type
            TypedResult CheckTableIndex(ExprNodeId indexSource, const String& subject) {
                const TypedResult index = Check(indexSource, ProgramType::Int32);
                if (!index.has_value() || TypeOf(*index) == ProgramType::Int32) {
                    return index;
                }
                const String convert = IsNumberType(TypeOf(*index).program) ? "; convert it with int(x)" : "";
                return Refuse(_ast[indexSource].column, Format("{} needs an int as its index, but it is {}{}", subject, Typed(TypeOf(*index)), convert));
            }

            /**
             * `at(i, e0, e1, ...)`: an `int` index and at least one entry, every entry of one type. That type is the hint (the operand beside the table, else the type the field
             * expects) or, with none, the one of the first typed entry; a literal entry takes it as a literal does anywhere. Whether an entry is a constant is the lowering's to say.
             */
            TypedResult CheckTable(const ExprNode& node, Optional<ExprType> hint) {
                const String subject = Format("the function '{}'", kTableFunction);
                if (node.argumentCount < 2) {
                    return Refuse(node.column, Format("{} takes an index and at least one entry, as at(i, e0, e1, ...), but {} {} given", subject, node.argumentCount,
                                                      node.argumentCount == 1 ? "argument is" : "arguments are"));
                }
                const size_t entryCount = node.argumentCount - 1;
                if (entryCount > kMaxTableEntries) {
                    return Refuse(node.column, Format("{} has {} entries, but a table has at most {}", subject, entryCount, kMaxTableEntries));
                }
                const ExprNodeId indexSource = _ast.arguments[node.firstArgument];
                const TypedResult index = CheckTableIndex(indexSource, subject);
                if (!index.has_value()) {
                    return index;
                }
                Vector<ExprNodeId> sources;
                for (size_t i = 0; i < entryCount; ++i) {
                    sources.push_back(_ast.arguments[node.firstArgument + 1 + i]);
                }
                auto entries = CheckSameType(sources, hint, subject);
                if (!entries.has_value()) {
                    return std::unexpected(entries.error());
                }
                TypedNode table;
                table.kind = TypedNodeKind::Operation;
                table.type = TypeOf((*entries)[0]);
                table.opcode = Opcode::LoadTable;
                table.column = node.column;
                table.operands[table.operandCount++] = *index;
                table.entries = std::move(*entries);
                return Emit(std::move(table));
            }

            /// A number of a named table as the register image of @p type; the message says what is wrong with it, for the entry @p index of table @p name
            static std::expected<RegisterImage, String> NamedTableEntry(StringView name, size_t index, double value, ProgramType type) {
                const auto refuse = [&](StringView why) { return std::unexpected(Format("the table '{}' holds {} at entry {}, which is {}", name, value, index, why)); };
                switch (type) {
                    case ProgramType::Double:
                        return ImageOf<double>(value);
                    case ProgramType::Float:
                        if (std::isfinite(value) && std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
                            return refuse("out of the range of a float");
                        }
                        return ImageOf<float>(static_cast<float>(value));
                    case ProgramType::Int32:
                    case ProgramType::Int64: {
                        // 2^31 and 2^63 are the first values past the range, and exact in a double
                        const double limit = type == ProgramType::Int32 ? static_cast<double>(kInt32MagnitudeLimit) : static_cast<double>(kInt64MagnitudeLimit);
                        if (!std::isfinite(value) || std::trunc(value) != value) {
                            return refuse(Format("no whole number, and {} needs one; round it in the table or ask for a float", Typed(type)));
                        }
                        if (value < -limit || value >= limit) {
                            return refuse(Format("out of the range of {}", Typed(type)));
                        }
                        return type == ProgramType::Int32 ? ImageOf<int32_t>(static_cast<int32_t>(value)) : ImageOf<int64_t>(static_cast<int64_t>(value));
                    }
                    default:
                        return std::unexpected(String("not a number type"));
                }
            }

            /**
             * `ByLevel('Name', i)`: entry `i` (an `int`, 0-based, clamped to the ends) of the table the provider has under that name. The result has the type of the operand beside it or
             * of the field when that is a number, else `double`; the entries are copied into constant nodes, so what follows is an inline table.
             */
            TypedResult CheckNamedTable(const ExprNode& node, Optional<ExprType> hint) {
                const String subject = Format("the function '{}'", kNamedTableFunction);
                if (node.argumentCount != 2) {
                    return Refuse(node.column, Format("{} takes the name of a table and an index, as {}('Name', i), but {} {} given", subject, kNamedTableFunction, node.argumentCount,
                                                      node.argumentCount == 1 ? "argument is" : "arguments are"));
                }
                const ExprNodeId nameSource = _ast.arguments[node.firstArgument];
                const ExprNodeId indexSource = _ast.arguments[node.firstArgument + 1];
                if (_ast[nameSource].kind != ExprNodeKind::String) {
                    return Refuse(_ast[nameSource].column, Format("{} needs the name of the table as a 'text' literal, as {}('Name', i)", subject, kNamedTableFunction));
                }
                const String name = _ast[nameSource].text;
                if (hint.has_value() && !IsNumberType(hint->program)) {
                    return Refuse(node.column, Format("{} gives a number, but {} is needed here: a named table holds numbers, use at(i, ...) for other types", subject, Typed(*hint)));
                }
                const ProgramType type = hint.has_value() ? hint->program : ProgramType::Double;
                if (_options.tables == nullptr) {
                    return Refuse(node.column,
                                  Format("the table '{}' is named, but no tables are declared where this formula is read: open a ValueLinkScope with UseTables, or pass CompileOptions::tables", name));
                }
                const Optional<Span<const double>> found = _options.tables->Find(name);
                if (!found.has_value()) {
                    Vector<String> known;
                    _options.tables->Names(known);
                    const String hintText = IDeserializer::MakeDidYouMeanHint(name, {&known});
                    return Refuse(_ast[nameSource].column, Format("there is no table '{}'{}; the tables are: {}", name, hintText, known.empty() ? String("none") : JoinNames(known)));
                }
                if (found->empty()) {
                    return Refuse(_ast[nameSource].column, Format("the table '{}' is empty", name));
                }
                if (found->size() > kMaxTableEntries) {
                    return Refuse(_ast[nameSource].column, Format("the table '{}' has {} entries, but a table has at most {}", name, found->size(), kMaxTableEntries));
                }
                const TypedResult index = CheckTableIndex(indexSource, subject);
                if (!index.has_value()) {
                    return index;
                }
                TypedNode table;
                table.kind = TypedNodeKind::Operation;
                table.type = type;
                table.opcode = Opcode::LoadTable;
                table.column = node.column;
                table.operands[table.operandCount++] = *index;
                for (size_t i = 0; i < found->size(); ++i) {
                    const auto image = NamedTableEntry(name, i, (*found)[i], type);
                    if (!image.has_value()) {
                        return Refuse(_ast[nameSource].column, image.error());
                    }
                    TypedNode entry;
                    entry.type = type;
                    entry.constant = *image;
                    entry.column = node.column;
                    table.entries.push_back(Emit(std::move(entry)));
                }
                return Emit(std::move(table));
            }

            /// `vec3(a, b, c)`: one argument per component, each of the component's type; a literal takes it
            TypedResult CheckPack(const ExprNode& node, const ExprFunction& function, const Vector<ExprNodeId>& arguments, const String& subject) {
                const ExprType componentType = ComponentsOf(function.target).type;
                TypedNode result;
                result.kind = TypedNodeKind::Operation;
                result.type = function.target;
                result.opcode = OpcodeOf(ProgramOperation::Pack, function.target);
                result.column = node.column;
                for (size_t i = 0; i < arguments.size(); ++i) {
                    const TypedResult operand = Check(arguments[i], componentType);
                    if (!operand.has_value()) {
                        return operand;
                    }
                    if (TypeOf(*operand) != componentType) {
                        const String convert = IsNumberType(TypeOf(*operand).program) ? Format("; convert it with {}(x)", ProgramTypeName(componentType.program)) : String();
                        return Refuse(_ast[arguments[i]].column,
                                      Format("{} takes {} arguments, but argument {} is {}{}", subject, DescribeExprType(componentType), i + 1, Typed(TypeOf(*operand)), convert));
                    }
                    result.operands[result.operandCount++] = *operand;
                }
                return Emit(std::move(result));
            }

            /// `lerp(a, b, t)`: two values of one type and the weight, a float (the type itself for a float or a double); `t` is not clamped, a `quat` is blended by `Slerp`
            TypedResult CheckBlend(const ExprNode& node, const Vector<ExprNodeId>& arguments, Optional<ExprType> hint, const String& subject) {
                const bool pairIsUntyped = IsUntypedNumber(arguments[0]) && IsUntypedNumber(arguments[1]);
                Optional<ExprType> pairHint = hint;
                Optional<TypedNodeId> weight;
                if (pairIsUntyped && _hasName[arguments[2]]) {
                    // The weight tells which number the literals beside it are
                    const TypedResult named = Check(arguments[2], std::nullopt);
                    if (!named.has_value()) {
                        return named;
                    }
                    weight = *named;
                    if (IsFloatType(TypeOf(*named).program)) {
                        pairHint = TypeOf(*named);
                    }
                } else if (pairIsUntyped && !(hint.has_value() && IsNumberType(hint->program)) && ContainsFloatLiteral(arguments[2])) {
                    pairHint = ProgramType::Float;
                }
                const auto pair = CheckSameType({arguments[0], arguments[1]}, pairHint, subject);
                if (!pair.has_value()) {
                    return std::unexpected(pair.error());
                }
                const ExprType type = TypeOf((*pair)[0]);
                const Opcode opcode = OpcodeOf(ProgramOperation::Lerp, type.program);
                if (opcode == Opcode::Invalid) {
                    return NotDefined(node.column, subject, ProgramOperation::Lerp, type);
                }
                const ExprType weightType = IsFloatType(type.program) ? type : ExprType(ProgramType::Float);
                if (!weight.has_value()) {
                    const TypedResult checked = Check(arguments[2], weightType);
                    if (!checked.has_value()) {
                        return checked;
                    }
                    weight = *checked;
                }
                if (TypeOf(*weight) != weightType) {
                    const uint32_t column = _ast[arguments[2]].column;
                    if (IsFloatType(type.program)) {
                        return Refuse(column, Format("{} needs operands of one type, but they are {} and {}: there is no implicit conversion between types{}", subject, DescribeExprType(type),
                                                     DescribeExprType(TypeOf(*weight)), ConvertHint(TypeOf(*weight), type)));
                    }
                    return Refuse(column, Format("{} needs a float as its third argument when it blends {}, but it is {}{}", subject, DescribeExprType(type), Typed(TypeOf(*weight)),
                                                 ConvertHint(TypeOf(*weight), weightType)));
                }
                return EmitOperation(opcode, type, node.column, {(*pair)[0], (*pair)[1], *weight});
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
            const CompileOptions& _options;
            Vector<bool> _hasName;
            Vector<LoopScope> _scopes;
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

    std::expected<TypedExpr, ProgramError> CheckExpression(const ExprAst& ast, const ContextDecl& declaration, const TypeDesc& expected, const CompileOptions& options) {
        Checker checker(ast, declaration, options);
        return checker.Run(expected);
    }

}  // namespace BECore
