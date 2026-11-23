#include "ExprAst.h"

namespace BECore {

    namespace {

        void AppendView(String& out, StringView view) {
            out.append(view.data(), view.size());
        }

        bool NeedsParenthesesAsOperand(const ExprNode& node) {
            return node.kind == ExprNodeKind::Binary;
        }

        /// A base of `.member` that would read as a longer name, a sign or a number when printed bare
        bool NeedsParenthesesAsBase(const ExprNode& node) {
            return node.kind != ExprNodeKind::Call && node.kind != ExprNodeKind::Member && node.kind != ExprNodeKind::Index;
        }

        /// A base of `[index]`: a name or another postfix node reads back as itself, anything else would bind the bracket to its last part
        bool NeedsParenthesesAsIndexBase(const ExprNode& node) {
            return node.kind != ExprNodeKind::Name && node.kind != ExprNodeKind::Call && node.kind != ExprNodeKind::Member && node.kind != ExprNodeKind::Index;
        }

        class Printer {
        public:
            explicit Printer(const ExprAst& ast) : _ast(ast) {}

            void Print(ExprNodeId id, String& out) const {
                const ExprNode& node = _ast[id];
                switch (node.kind) {
                    case ExprNodeKind::Number:
                    case ExprNodeKind::Name:
                        AppendView(out, node.text);
                        return;
                    case ExprNodeKind::Bool:
                        out += node.boolValue ? "true" : "false";
                        return;
                    case ExprNodeKind::String:
                        out += '\'';
                        AppendView(out, node.text);
                        out += '\'';
                        return;
                    case ExprNodeKind::Call:
                        AppendView(out, node.text);
                        out += '(';
                        for (uint32_t i = 0; i < node.argumentCount; ++i) {
                            out += i == 0 ? "" : ", ";
                            Print(_ast.arguments[node.firstArgument + i], out);
                        }
                        out += ')';
                        return;
                    case ExprNodeKind::Member:
                        Wrapped(node.first, NeedsParenthesesAsBase(_ast[node.first]), out);
                        out += '.';
                        AppendView(out, node.text);
                        return;
                    case ExprNodeKind::Index:
                        Wrapped(node.first, NeedsParenthesesAsIndexBase(_ast[node.first]), out);
                        out += '[';
                        Print(node.second, out);
                        out += ']';
                        return;
                    case ExprNodeKind::Unary: {
                        AppendView(out, OperatorSymbol(node.operation));
                        const ExprNodeKind operand = _ast[node.first].kind;
                        Wrapped(node.first, operand == ExprNodeKind::Unary || operand == ExprNodeKind::Binary, out);
                        return;
                    }
                    case ExprNodeKind::Binary:
                        Wrapped(node.first, NeedsParenthesesAsOperand(_ast[node.first]), out);
                        out += ' ';
                        AppendView(out, OperatorSymbol(node.operation));
                        out += ' ';
                        Wrapped(node.second, NeedsParenthesesAsOperand(_ast[node.second]), out);
                        return;
                }
            }

        private:
            void Wrapped(ExprNodeId id, bool parentheses, String& out) const {
                if (parentheses) {
                    out += '(';
                }
                Print(id, out);
                if (parentheses) {
                    out += ')';
                }
            }

            const ExprAst& _ast;
        };

    }  // namespace

    StringView OperatorSymbol(ProgramOperation operation) {
        switch (operation) {
            case ProgramOperation::Add:
                return "+";
            case ProgramOperation::Sub:
            case ProgramOperation::Neg:
                return "-";
            case ProgramOperation::Mul:
                return "*";
            case ProgramOperation::Div:
                return "/";
            case ProgramOperation::Mod:
                return "%";
            case ProgramOperation::Eq:
                return "==";
            case ProgramOperation::Ne:
                return "!=";
            case ProgramOperation::Lt:
                return "<";
            case ProgramOperation::Le:
                return "<=";
            case ProgramOperation::Gt:
                return ">";
            case ProgramOperation::Ge:
                return ">=";
            case ProgramOperation::And:
                return "&&";
            case ProgramOperation::Or:
                return "||";
            case ProgramOperation::Not:
                return "!";
            default:
                return EnumUtils<ProgramOperation>::ToString(operation);
        }
    }

    String PrintExpression(const ExprAst& ast) {
        String out;
        if (ast.nodes.empty()) {
            return out;
        }
        Printer(ast).Print(ast.root, out);
        return out;
    }

}  // namespace BECore
