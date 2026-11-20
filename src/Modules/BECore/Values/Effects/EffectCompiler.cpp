#include "EffectCompiler.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Values/Effects/EffectProgramBuilder.h>
#include <BECore/Values/Effects/ScriptParser.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ExprFunctions.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <BECore/Values/Programs/ProgramCompiler.h>

namespace BECore {

    namespace {

        using Failure = std::unexpected<ProgramError>;
        using Status = std::expected<void, ProgramError>;

        Failure Refuse(uint32_t column, String message) {
            return std::unexpected(ProgramError{std::move(message), column});
        }

        /**
         * The text handed to the formula compiler and where the user's own bytes sit in it: the whole text of an assignment's right side, or the same inside what the compiler
         * wrote (`target + (` and `)`), or nothing of the user's at all (`target + 1`)
         */
        struct FormulaText {
            String text;
            uint32_t prefix = 0;
            uint32_t length = 0;
            uint32_t suffix = 0;
            /// The 0-based byte of the script where the user's bytes start
            uint32_t begin = 0;
            /// The column an error outside the user's bytes points at: the statement's operator or keyword
            uint32_t fallback = 0;

            [[nodiscard]] static FormulaText Slice(StringView script, ScriptRange range, uint32_t fallback) {
                return Around(script, range, {}, {}, fallback);
            }

            [[nodiscard]] static FormulaText Around(StringView script, ScriptRange range, StringView before, StringView after, uint32_t fallback) {
                FormulaText formula;
                formula.text = String(before);
                formula.text.append(script.data() + range.begin, range.end - range.begin);
                formula.text.append(after.data(), after.size());
                formula.prefix = static_cast<uint32_t>(before.size());
                formula.length = range.end - range.begin;
                formula.suffix = static_cast<uint32_t>(after.size());
                formula.begin = range.begin;
                formula.fallback = fallback;
                return formula;
            }

            /// Text the compiler wrote entirely: every error is the statement's
            [[nodiscard]] static FormulaText Written(String text, uint32_t fallback) {
                FormulaText formula;
                formula.text = std::move(text);
                formula.fallback = fallback;
                return formula;
            }

            [[nodiscard]] StringView Slice() const {
                return StringView(text).substr(prefix, length);
            }

            /// A column of the slice alone (1-based, `length + 1` is the end of it) as a column of the script
            [[nodiscard]] uint32_t FromSlice(uint32_t column) const {
                return column == 0 || column > length + 1 ? fallback : begin + column;
            }

            /// A column of `text` as a column of the script. The end of the text is a place of the user's only when nothing follows their bytes
            [[nodiscard]] uint32_t FromText(uint32_t column) const {
                if (length == 0 || column <= prefix) {
                    return fallback;
                }
                const uint32_t inside = column - prefix;
                return inside > length + (suffix == 0 ? 1 : 0) ? fallback : begin + inside;
            }
        };

        /// The type a command's parameter has in a formula
        const TypeDesc* DescriptorOf(const DomainValueType& type) {
            switch (type.type) {
                case ProgramType::Enum:
                    return type.enumType;
                case ProgramType::Domain:
                    return type.IsDomain() ? ProgramDomain::Type(type.domain).descriptor : nullptr;
                default:
                    break;
            }
#define BE_DESCRIPTOR_OF(Unused, Name, Cpp)                                                                                                                                                            \
    case ProgramType::Name:                                                                                                                                                                            \
        return &DescribeType<Cpp>();
            switch (type.type) {
                BE_PROGRAM_TYPES_SCALARS(BE_DESCRIPTOR_OF, Unused)
                BE_PROGRAM_TYPES_MATH(BE_DESCRIPTOR_OF, Unused)
                BE_PROGRAM_TYPES_STRING(BE_DESCRIPTOR_OF, Unused)
                default:
                    break;
            }
#undef BE_DESCRIPTOR_OF
            return nullptr;
        }

        /// The width of a field of a type that does not say it (an enum's underlying type, a registered type's size); 0 for the other types
        uint8_t FieldBytesOf(const ExprType& type) {
            if (type.program == ProgramType::Enum && type.enumeration != nullptr) {
                return static_cast<uint8_t>(type.enumeration->enumBytes);
            }
            if (type.program == ProgramType::Domain && type.domain != kNoDomainType) {
                return static_cast<uint8_t>(ProgramDomain::Type(type.domain).size);
            }
            return 0;
        }

        String DescribeParameter(const DomainValueType& type) {
            if (type.IsDomain()) {
                return String(ProgramDomain::Type(type.domain).name.ToStringView());
            }
            if (type.type == ProgramType::Enum && type.enumType != nullptr) {
                String names;
                for (const StringView name : type.enumType->names) {
                    names += names.empty() ? "" : ", ";
                    names.append(name.data(), name.size());
                }
                return Format("enum {{{}}}", names);
            }
            return String(ProgramTypeName(type.type));
        }

        String Join(Span<const String> words) {
            String text;
            for (const String& word : words) {
                text += text.empty() ? "" : ", ";
                text += word;
            }
            return text;
        }

        String Label(const ScriptStatement& statement) {
            switch (statement.kind) {
                case ScriptStatementKind::Assign:
                    return Format("assignment to '{}'", statement.target);
                case ScriptStatementKind::AddAssign:
                    return Format("'+=' on '{}'", statement.target);
                case ScriptStatementKind::SubAssign:
                    return Format("'-=' on '{}'", statement.target);
                case ScriptStatementKind::Increment:
                    return Format("'++' on '{}'", statement.target);
                case ScriptStatementKind::Decrement:
                    return Format("'--' on '{}'", statement.target);
                case ScriptStatementKind::Toggle:
                    return Format("'toggle' of '{}'", statement.target);
                case ScriptStatementKind::If:
                    return "'if' condition";
                case ScriptStatementKind::Emit:
                    return Format("'emit {}'", statement.target);
            }
            return {};
        }

        /// A field a script writes: where it is, its type and the width a store needs
        struct WriteTarget {
            SlotRef slot;
            const TypeDesc* type = nullptr;
            ExprType expression;
            uint8_t bytes = 0;
        };

        /// One pass over the script. The first pass (`commit` false) only parses and checks every expression, so nothing reaches the pool of programs until the whole text is known to
        /// be good; the second compiles them and builds the steps.
        class ScriptCompiler {
        public:
            ScriptCompiler(StringView script, const ScriptAst& ast, const ContextDecl& declaration, const EffectOptions& options, bool commit)
                : _script(script), _ast(ast), _declaration(declaration), _options(options), _commit(commit) {
                _compile.tables = options.tables;
                _compile.fuse = options.fuse;
            }

            Status Run() {
                return Block(_ast.root);
            }

            EffectProgramBuilder& Builder() {
                return _builder;
            }

        private:
            Status Block(ScriptBlock block) {
                for (uint32_t i = 0; i < block.count; ++i) {
                    if (const Status done = Statement(_ast.At(block, i)); !done.has_value()) {
                        return done;
                    }
                }
                return {};
            }

            Status Statement(ScriptStatementId id) {
                const ScriptStatement& statement = _ast[id];
                switch (statement.kind) {
                    case ScriptStatementKind::If:
                        return If(id);
                    case ScriptStatementKind::Emit:
                        return Emit(statement);
                    default:
                        return Assignment(statement);
                }
            }

            /// `target = e`, and the four statements that are the assignment of an expression the compiler writes
            Status Assignment(const ScriptStatement& statement) {
                const String label = Label(statement);
                const auto target = ResolveTarget(statement, label);
                if (!target.has_value()) {
                    return std::unexpected(target.error());
                }
                const StringView path = statement.target;
                FormulaText formula;
                switch (statement.kind) {
                    case ScriptStatementKind::Assign:
                        formula = FormulaText::Slice(_script, statement.expression, statement.column);
                        break;
                    case ScriptStatementKind::AddAssign:
                        formula = FormulaText::Around(_script, statement.expression, Format("{} + (", path), ")", statement.column);
                        break;
                    case ScriptStatementKind::SubAssign:
                        formula = FormulaText::Around(_script, statement.expression, Format("{} - (", path), ")", statement.column);
                        break;
                    case ScriptStatementKind::Increment:
                        formula = FormulaText::Written(Format("{} + 1", path), statement.column);
                        break;
                    case ScriptStatementKind::Decrement:
                        formula = FormulaText::Written(Format("{} - 1", path), statement.column);
                        break;
                    default:
                        formula = FormulaText::Written(Format("!{}", path), statement.column);
                        break;
                }
                const auto value = Formula(formula, *target->type, target->bytes, label);
                if (!value.has_value()) {
                    return std::unexpected(value.error());
                }
                _builder.AddStore(target->slot, target->expression.program, target->bytes, *value);
                return {};
            }

            /// `if (c) {...} else if (c) {...} else {...}`: an else block that is one `if` goes on in this loop, so a long chain does not nest the compiler
            Status If(ScriptStatementId first) {
                Vector<uint32_t> exits;
                ScriptStatementId current = first;
                while (true) {
                    const ScriptStatement& statement = _ast[current];
                    const auto condition = Formula(FormulaText::Slice(_script, statement.expression, statement.column), DescribeType<bool>(), 0, Label(statement));
                    if (!condition.has_value()) {
                        return std::unexpected(condition.error());
                    }
                    const uint32_t skip = _builder.AddJumpIfNot(*condition);
                    if (const Status body = Block(statement.thenBlock); !body.has_value()) {
                        return body;
                    }
                    if (!statement.hasElse) {
                        _builder.PatchJump(skip, _builder.NextStep());
                        break;
                    }
                    exits.push_back(_builder.AddJump());
                    _builder.PatchJump(skip, _builder.NextStep());
                    const ScriptBlock otherwise = statement.elseBlock;
                    if (otherwise.count == 1 && _ast[_ast.At(otherwise, 0)].kind == ScriptStatementKind::If) {
                        current = _ast.At(otherwise, 0);
                        continue;
                    }
                    if (const Status rest = Block(otherwise); !rest.has_value()) {
                        return rest;
                    }
                    break;
                }
                for (const uint32_t exit : exits) {
                    _builder.PatchJump(exit, _builder.NextStep());
                }
                return {};
            }

            Status Emit(const ScriptStatement& statement) {
                const String label = Label(statement);
                const Optional<CommandId> id = EffectCommands::Find(statement.target);
                if (!id.has_value()) {
                    return UnknownCommand(statement, label);
                }
                const EffectCommand command = EffectCommands::Command(*id);
                if (command.parameterCount != statement.argumentCount) {
                    String parameters;
                    for (uint8_t i = 0; i < command.parameterCount; ++i) {
                        parameters += i == 0 ? "" : ", ";
                        parameters += DescribeParameter(command.parameters[i]);
                    }
                    return Refuse(statement.targetColumn, Format("{}: the command takes {} argument(s) ({}), but {} are given", label, command.parameterCount, parameters, statement.argumentCount));
                }
                Array<EffectOperand, kMaxCommandArguments> arguments{};
                for (uint8_t i = 0; i < command.parameterCount; ++i) {
                    const DomainValueType& parameter = command.parameters[i];
                    const TypeDesc* descriptor = DescriptorOf(parameter);
                    if (descriptor == nullptr) {
                        return Refuse(statement.targetColumn, Format("{}: parameter {} has a type a formula cannot have", label, i + 1));
                    }
                    const ScriptRange range = _ast.arguments[statement.firstArgument + i];
                    const auto value = Formula(FormulaText::Slice(_script, range, statement.column), *descriptor, FieldBytesOf(ExprType::Of(parameter)), Format("{} argument {}", label, i + 1));
                    if (!value.has_value()) {
                        return std::unexpected(value.error());
                    }
                    arguments[i] = *value;
                }
                _builder.AddEmit(*id, Span<const EffectOperand>(arguments.data(), command.parameterCount));
                return {};
            }

            Status UnknownCommand(const ScriptStatement& statement, StringView label) const {
                const Vector<String> names = EffectCommands::Names();
                const String hint = IDeserializer::MakeDidYouMeanHint(statement.target, {&names});
                const String known = names.empty() ? String("no command is registered") : Format("the commands are: {}", Join(Span<const String>(names.data(), names.size())));
                return Refuse(statement.targetColumn, Format("{}: unknown command '{}'{}; {}", label, statement.target, hint, known));
            }

            /// The field a statement writes: it must link, be a whole field (no component), be one the host permits and have a type a formula can have
            std::expected<WriteTarget, ProgramError> ResolveTarget(const ScriptStatement& statement, StringView label) const {
                const StringView path = statement.target;
                const uint32_t column = statement.targetColumn != 0 ? statement.targetColumn : statement.column;
                if (const Optional<String> component = ComponentOf(path)) {
                    return Refuse(column, Format("{}: {}", label, *component));
                }
                const auto linked = LinkField(_declaration, path);
                if (!linked.has_value()) {
                    return Refuse(column, Format("{}: {}", label, linked.error().message));
                }
                if (_options.targets == nullptr || _options.targets->Empty()) {
                    return Refuse(column, Format("{}: the script has no write targets, so it writes nothing; the host declares what a script may write", label));
                }
                if (!_options.targets->Permits(path)) {
                    return Refuse(column, Format("{}: '{}' is not a path the host allows a script to write; it allows: {}", label, path, Join(_options.targets->Paths())));
                }
                const auto type = CheckExpectedType(*linked->type);
                if (!type.has_value()) {
                    return Refuse(column, Format("{}: '{}' cannot be written: {}", label, path, type.error().message));
                }
                return WriteTarget{linked->slot, linked->type, *type, FieldBytesOf(*type)};
            }

            /// The message when @p path ends in a component of a field (`cell.pos.x`): a script stores whole fields
            Optional<String> ComponentOf(StringView path) const {
                const size_t last = path.rfind('.');
                if (last == StringView::npos || path.find('.') == last) {
                    return std::nullopt;
                }
                const StringView field = path.substr(0, last);
                const StringView name = path.substr(last + 1);
                const auto linked = LinkField(_declaration, field);
                if (!linked.has_value()) {
                    return std::nullopt;
                }
                const auto type = CheckExpectedType(*linked->type);
                if (!type.has_value() || !ComponentsOf(type->program).Find(name).has_value()) {
                    return std::nullopt;
                }
                return Format("'{}' is a component of the field '{}', and a script writes a whole field: assign '{}' as a whole", path, field, field);
            }

            /// One expression of the script. @p expected is the type of its place; a failure is mapped back to the script's columns
            std::expected<EffectOperand, ProgramError> Formula(const FormulaText& formula, const TypeDesc& expected, uint8_t bytes, StringView label) {
                // The user's bytes alone first: a text the compiler wrapped in `target + (` and `)` would hide an unbalanced parenthesis in them
                if (formula.prefix > 0 || formula.suffix > 0) {
                    if (const auto alone = ParseExpression(formula.Slice()); !alone.has_value()) {
                        return Refuse(formula.FromSlice(alone.error().column), Format("{}: {}", label, alone.error().message));
                    }
                }
                if (!_commit) {
                    const auto ast = ParseExpression(formula.text);
                    if (!ast.has_value()) {
                        return Refuse(formula.FromText(ast.error().column), Format("{}: {}", label, ast.error().message));
                    }
                    if (const auto typed = CheckExpression(*ast, _declaration, expected, _compile); !typed.has_value()) {
                        return Refuse(formula.FromText(typed.error().column), Format("{}: {}", label, typed.error().message));
                    }
                    return EffectOperand{};
                }
                const auto compiled = CompileExpression(formula.text, _declaration, expected, _compile);
                if (!compiled.has_value()) {
                    return Refuse(formula.FromText(compiled.error().column), Format("{}: {}", label, compiled.error().message));
                }
                return EffectOperand::Of(*compiled, bytes);
            }

            StringView _script;
            const ScriptAst& _ast;
            const ContextDecl& _declaration;
            const EffectOptions& _options;
            bool _commit;
            CompileOptions _compile;
            EffectProgramBuilder _builder;
        };

    }  // namespace

    std::expected<EffectScript, ProgramError> CompileEffects(StringView text, const ContextDecl& declaration, const EffectOptions& options) {
        const auto ast = ParseScript(text);
        if (!ast.has_value()) {
            return std::unexpected(ast.error());
        }
        ScriptCompiler check(text, *ast, declaration, options, false);
        if (const Status checked = check.Run(); !checked.has_value()) {
            return std::unexpected(checked.error());
        }
        ScriptCompiler build(text, *ast, declaration, options, true);
        if (const Status built = build.Run(); !built.has_value()) {
            return std::unexpected(built.error());
        }
        auto program = build.Builder().Finish(String(text));
        if (!program.has_value()) {
            return std::unexpected(program.error());
        }
        return EffectScript(std::move(*program));
    }

}  // namespace BECore
