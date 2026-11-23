#include "CalcNodes.h"

#include <BECore/Values/Programs/ExprFunctions.h>
#include <BECore/Values/Programs/ExprLexer.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Generated/CalcNodes.gen.hpp>

namespace BECore {

    namespace {

        using BuildResult = std::expected<ExprNodeId, ProgramError>;

        bool IsUnaryOperation(ProgramOperation operation) {
            return operation == ProgramOperation::Neg || operation == ProgramOperation::Not;
        }

        bool IsBinaryOperation(ProgramOperation operation) {
            switch (operation) {
                case ProgramOperation::Add:
                case ProgramOperation::Sub:
                case ProgramOperation::Mul:
                case ProgramOperation::Div:
                case ProgramOperation::Mod:
                case ProgramOperation::Eq:
                case ProgramOperation::Ne:
                case ProgramOperation::Lt:
                case ProgramOperation::Le:
                case ProgramOperation::Gt:
                case ProgramOperation::Ge:
                case ProgramOperation::And:
                case ProgramOperation::Or:
                    return true;
                default:
                    return false;
            }
        }

        /// The field's text as the lexer reads it, or the reason it is not what the node needs
        struct LeafCheck {
            bool ok = false;
            bool isFloat = false;
            String why;
        };

        /// @p text must lex as exactly one number
        LeafCheck CheckNumber(StringView text) {
            const auto tokens = LexExpression(text);
            if (!tokens.has_value()) {
                return {false, false, tokens.error().message};
            }
            if (tokens->size() != 2 || tokens->front().kind != ExprTokenKind::Number) {
                return {false, false, "is not one number (a sign is a CalcUnary, an operator a CalcBinary)"};
            }
            return {true, tokens->front().isFloat, {}};
        }

        /// @p text must lex as a name: one identifier, or identifiers joined by dots when @p dotted; `true` and `false` are literals, not names
        LeafCheck CheckName(StringView text, bool dotted) {
            const auto tokens = LexExpression(text);
            if (!tokens.has_value()) {
                return {false, false, tokens.error().message};
            }
            const Vector<ExprToken>& list = *tokens;
            const StringView reason = dotted ? "is not a name or a dotted path" : "is not one name";
            // The last token is `End`, so a dot always has a token after it
            size_t at = 0;
            if (list[at].kind != ExprTokenKind::Name) {
                return {false, false, String(reason)};
            }
            ++at;
            while (list[at].kind == ExprTokenKind::Dot) {
                if (list[at + 1].kind != ExprTokenKind::Name) {
                    return {false, false, String(reason)};
                }
                at += 2;
            }
            if (list[at].kind != ExprTokenKind::End || (!dotted && at != 1)) {
                return {false, false, String(reason)};
            }
            if (list.size() == 2 && (list.front().text == "true" || list.front().text == "false")) {
                return {false, false, "is a literal (use CalcBool)"};
            }
            return {true, false, {}};
        }

        BuildResult Refuse(const CalcBuildState& state, StringView owner, StringView field, const LeafCheck& check) {
            return state.Fail(Format("{}: '{}' {}", owner, field, check.why));
        }

        BuildResult RefuseQuote(const CalcBuildState& state, StringView owner, StringView field) {
            return state.Fail(Format("{}: '{}' cannot hold a quote (a string literal has no escapes)", owner, field));
        }

        bool HasQuote(StringView text) {
            return text.find('\'') != StringView::npos;
        }

        BuildResult AddString(ExprAst& ast, const CalcBuildState& state, StringView text) {
            ExprNode node;
            node.kind = ExprNodeKind::String;
            node.text = String(text);
            return state.Add(ast, std::move(node));
        }

    }  // namespace

    std::expected<ExprNodeId, ProgramError> CalcNumber::Append(ExprAst& ast, CalcBuildState& state) const {
        const LeafCheck check = CheckNumber(text);
        if (!check.ok) {
            return Refuse(state, "CalcNumber", "text", check);
        }
        ExprNode node;
        node.kind = ExprNodeKind::Number;
        node.isFloat = check.isFloat;
        node.text = text;
        return state.Add(ast, std::move(node));
    }

    std::expected<ExprNodeId, ProgramError> CalcBool::Append(ExprAst& ast, CalcBuildState& state) const {
        ExprNode node;
        node.kind = ExprNodeKind::Bool;
        node.boolValue = value;
        return state.Add(ast, std::move(node));
    }

    std::expected<ExprNodeId, ProgramError> CalcText::Append(ExprAst& ast, CalcBuildState& state) const {
        if (HasQuote(text)) {
            return RefuseQuote(state, "CalcText", "text");
        }
        return AddString(ast, state, text);
    }

    std::expected<ExprNodeId, ProgramError> CalcRead::Append(ExprAst& ast, CalcBuildState& state) const {
        const LeafCheck check = CheckName(path, true);
        if (!check.ok) {
            return Refuse(state, "CalcRead", "path", check);
        }
        ExprNode node;
        node.kind = ExprNodeKind::Name;
        node.text = path;
        return state.Add(ast, std::move(node));
    }

    std::expected<ExprNodeId, ProgramError> CalcUnary::Append(ExprAst& ast, CalcBuildState& state) const {
        if (!IsUnaryOperation(op)) {
            return state.Fail(Format("CalcUnary: 'op' {} is not a unary operator (Neg or Not)", EnumUtils<ProgramOperation>::ToString(op)));
        }
        const auto first = state.Build(ast, operand, "CalcUnary", "operand");
        if (!first.has_value()) {
            return first;
        }
        ExprNode node;
        node.kind = ExprNodeKind::Unary;
        node.operation = op;
        node.first = *first;
        return state.Add(ast, std::move(node));
    }

    std::expected<ExprNodeId, ProgramError> CalcBinary::Append(ExprAst& ast, CalcBuildState& state) const {
        if (!IsBinaryOperation(op)) {
            return state.Fail(Format("CalcBinary: 'op' {} is not a binary operator (Add Sub Mul Div Mod Eq Ne Lt Le Gt Ge And Or)", EnumUtils<ProgramOperation>::ToString(op)));
        }
        const auto first = state.Build(ast, left, "CalcBinary", "left");
        if (!first.has_value()) {
            return first;
        }
        const auto second = state.Build(ast, right, "CalcBinary", "right");
        if (!second.has_value()) {
            return second;
        }
        ExprNode node;
        node.kind = ExprNodeKind::Binary;
        node.operation = op;
        node.first = *first;
        node.second = *second;
        return state.Add(ast, std::move(node));
    }

    std::expected<ExprNodeId, ProgramError> CalcCall::Append(ExprAst& ast, CalcBuildState& state) const {
        const LeafCheck check = CheckName(function, true);
        if (!check.ok) {
            return Refuse(state, "CalcCall", "function", check);
        }
        Vector<ExprNodeId> built;
        built.reserve(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            const String key = Format("args[{}]", i);
            const auto id = state.Build(ast, args[i], "CalcCall", key);
            if (!id.has_value()) {
                return id;
            }
            built.push_back(*id);
        }
        // The arguments go to the list together, as the parser does, so those of a nested call do not interleave with these
        ExprNode node;
        node.kind = ExprNodeKind::Call;
        node.text = function;
        node.firstArgument = static_cast<uint32_t>(ast.arguments.size());
        node.argumentCount = static_cast<uint32_t>(built.size());
        ast.arguments.insert(ast.arguments.end(), built.begin(), built.end());
        return state.Add(ast, std::move(node));
    }

    std::expected<ExprNodeId, ProgramError> CalcMember::Append(ExprAst& ast, CalcBuildState& state) const {
        const LeafCheck check = CheckName(name, false);
        if (!check.ok) {
            return Refuse(state, "CalcMember", "name", check);
        }
        const auto first = state.Build(ast, base, "CalcMember", "base");
        if (!first.has_value()) {
            return first;
        }
        ExprNode node;
        node.kind = ExprNodeKind::Member;
        node.first = *first;
        node.text = name;
        return state.Add(ast, std::move(node));
    }

    std::expected<ExprNodeId, ProgramError> CalcByLevel::Append(ExprAst& ast, CalcBuildState& state) const {
        if (HasQuote(table)) {
            return RefuseQuote(state, "CalcByLevel", "table");
        }
        const auto tableName = AddString(ast, state, table);
        if (!tableName.has_value()) {
            return tableName;
        }
        const auto entry = state.Build(ast, index, "CalcByLevel", "index");
        if (!entry.has_value()) {
            return entry;
        }
        ExprNode node;
        node.kind = ExprNodeKind::Call;
        node.text = String(kNamedTableFunction);
        node.firstArgument = static_cast<uint32_t>(ast.arguments.size());
        node.argumentCount = 2;
        ast.arguments.push_back(*tableName);
        ast.arguments.push_back(*entry);
        return state.Add(ast, std::move(node));
    }

    bool ReadFormulaTree(IDeserializer& d, StringView key, String& text) {
        IntrusivePtr<CalcNode> root;
        const size_t reported = d.GetErrors().size();
        const bool opened = Ser::Load(d, root, key);
        if (!root) {
            // The factory reports an unknown class; a root with no `type` (the base is abstract) or one that is no object is silent
            if (d.GetErrors().size() == reported) {
                d.ReportError(key, ValueLinkScope::AtDocument(opened ? "A tree needs the class of its root node in 'type'" : "A tree is an element with the class of its root node in 'type'"));
            }
            return false;
        }
        auto printed = BuildFormulaText(*root);
        if (!printed.has_value()) {
            d.ReportError(key, ValueLinkScope::AtDocument(printed.error().message));
            return false;
        }
        text = std::move(*printed);
        return true;
    }

}  // namespace BECore
