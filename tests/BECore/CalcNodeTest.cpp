// The node classes of the tree form (Values/Nodes): a tree built by hand and the text it prints are the same formula, the registry makes every class by name, and the limits stop a
// hostile tree while it is built.

#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Values/Nodes/CalcNodes.h>
#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <BECore/Values/Programs/Program.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>
#include <initializer_list>

namespace BECore::Tests {

    namespace {

        using Node = IntrusivePtr<CalcNode>;

        Node Number(StringView text) {
            auto node = New<CalcNumber>();
            node->text = String(text);
            return node;
        }

        Node Boolean(bool value) {
            auto node = New<CalcBool>();
            node->value = value;
            return node;
        }

        Node Text(StringView text) {
            auto node = New<CalcText>();
            node->text = String(text);
            return node;
        }

        Node Read(StringView path) {
            auto node = New<CalcRead>();
            node->path = String(path);
            return node;
        }

        Node Unary(ProgramOperation op, Node operand) {
            auto node = New<CalcUnary>();
            node->op = op;
            node->operand = std::move(operand);
            return node;
        }

        Node Binary(ProgramOperation op, Node left, Node right) {
            auto node = New<CalcBinary>();
            node->op = op;
            node->left = std::move(left);
            node->right = std::move(right);
            return node;
        }

        Node Call(StringView function, std::initializer_list<Node> args) {
            auto node = New<CalcCall>();
            node->function = String(function);
            for (const Node& arg : args) {
                node->args.push_back(arg);
            }
            return node;
        }

        Node Member(Node base, StringView name) {
            auto node = New<CalcMember>();
            node->base = std::move(base);
            node->name = String(name);
            return node;
        }

        Node ByLevel(StringView table, Node index) {
            auto node = New<CalcByLevel>();
            node->table = String(table);
            node->index = std::move(index);
            return node;
        }

        /// The tree without its columns: a tree and the text it prints have the same nodes, whatever bytes the text puts them at
        bool SameTree(const ExprAst& a, const ExprAst& b) {
            if (a.nodes.size() != b.nodes.size() || a.root != b.root || a.arguments != b.arguments) {
                return false;
            }
            for (size_t i = 0; i < a.nodes.size(); ++i) {
                const ExprNode& x = a.nodes[i];
                const ExprNode& y = b.nodes[i];
                if (x.kind != y.kind || x.operation != y.operation || x.isFloat != y.isFloat || x.boolValue != y.boolValue || x.first != y.first || x.second != y.second ||
                    x.firstArgument != y.firstArgument || x.argumentCount != y.argumentCount || x.text != y.text) {
                    return false;
                }
            }
            return true;
        }

        std::expected<ExprAst, ProgramError> BuildAst(const CalcNode& root) {
            ExprAst ast;
            CalcBuildState state;
            const auto id = root.Append(ast, state);
            if (!id.has_value()) {
                return std::unexpected(id.error());
            }
            ast.root = *id;
            return ast;
        }

        struct FormulaCase {
            StringView text;
            Node tree;
        };

        /// The error of a tree that must be refused, with the pool as it was
        String ExpectRefused(const CalcNode& root) {
            const size_t pooled = ProgramPool::Count();
            const auto text = BuildFormulaText(root);
            EXPECT_FALSE(text.has_value());
            EXPECT_EQ(ProgramPool::Count(), pooled);
            return text.has_value() ? String() : text.error().message;
        }

        constexpr ProgramOperation kBinaryOperations[] = {ProgramOperation::Add, ProgramOperation::Sub, ProgramOperation::Mul, ProgramOperation::Div, ProgramOperation::Mod,
                                                          ProgramOperation::Eq,  ProgramOperation::Ne,  ProgramOperation::Lt,  ProgramOperation::Le,  ProgramOperation::Gt,
                                                          ProgramOperation::Ge,  ProgramOperation::And, ProgramOperation::Or};

        /// A chain of @p levels `CalcUnary` nodes over a number: the root is level 0, the number level @p levels
        Node DeepChain(size_t levels) {
            Node node = Number("1");
            for (size_t i = 0; i < levels; ++i) {
                node = Unary(ProgramOperation::Neg, std::move(node));
            }
            return node;
        }

    }  // namespace

    TEST(CalcNodeTest, TreeAndTextAreTheSameFormula) {
        const FormulaCase cases[] = {
            {"12", Number("12")},
            {"1.5", Number("1.5")},
            {"1e3", Number("1e3")},
            {"true", Boolean(true)},
            {"false", Boolean(false)},
            {"'Fire'", Text("Fire")},
            {"''", Text("")},
            {"caster.attack", Read("caster.attack")},
            {"Fire", Read("Fire")},
            {"-caster.attack", Unary(ProgramOperation::Neg, Read("caster.attack"))},
            {"!caster.shielded", Unary(ProgramOperation::Not, Read("caster.shielded"))},
            {"-(-x)", Unary(ProgramOperation::Neg, Unary(ProgramOperation::Neg, Read("x")))},
            {"caster.attack * 2", Binary(ProgramOperation::Mul, Read("caster.attack"), Number("2"))},
            {"(a + b) * c", Binary(ProgramOperation::Mul, Binary(ProgramOperation::Add, Read("a"), Read("b")), Read("c"))},
            {"a * (b + c)", Binary(ProgramOperation::Mul, Read("a"), Binary(ProgramOperation::Add, Read("b"), Read("c")))},
            {"(a - b) - c", Binary(ProgramOperation::Sub, Binary(ProgramOperation::Sub, Read("a"), Read("b")), Read("c"))},
            {"a - (b - c)", Binary(ProgramOperation::Sub, Read("a"), Binary(ProgramOperation::Sub, Read("b"), Read("c")))},
            {"-(a + b)", Unary(ProgramOperation::Neg, Binary(ProgramOperation::Add, Read("a"), Read("b")))},
            {"min(caster.attack, 10)", Call("min", {Read("caster.attack"), Number("10")})},
            {"max(a, b)", Call("max", {Read("a"), Read("b")})},
            {"clamp(x, 0, 1)", Call("clamp", {Read("x"), Number("0"), Number("1")})},
            {"lerp(a, b, t)", Call("lerp", {Read("a"), Read("b"), Read("t")})},
            {"select(caster.shielded, 1, 2)", Call("select", {Read("caster.shielded"), Number("1"), Number("2")})},
            {"float(caster.level) * 0.5", Binary(ProgramOperation::Mul, Call("float", {Read("caster.level")}), Number("0.5"))},
            {"vec3(1, 0, 0)", Call("vec3", {Number("1"), Number("0"), Number("0")})},
            {"color(255, 0, 0, 255)", Call("color", {Number("255"), Number("0"), Number("0"), Number("255")})},
            {"at(caster.level, 10, 20, 30)", Call("at", {Read("caster.level"), Number("10"), Number("20"), Number("30")})},
            {"at(caster.element, 'a', 'b')", Call("at", {Read("caster.element"), Text("a"), Text("b")})},
            {"ByLevel('FireballDamage', ability.level)", ByLevel("FireballDamage", Read("ability.level"))},
            {"ByLevel('FireballDamage', ability.level) * 2", Binary(ProgramOperation::Mul, ByLevel("FireballDamage", Read("ability.level")), Number("2"))},
            {"lerp(a, b, t).x", Member(Call("lerp", {Read("a"), Read("b"), Read("t")}), "x")},
            {"(a).b", Member(Read("a"), "b")},
            {"(a + b).x", Member(Binary(ProgramOperation::Add, Read("a"), Read("b")), "x")},
            {"caster.element == Fire", Binary(ProgramOperation::Eq, Read("caster.element"), Read("Fire"))},
            {"title == 'Boss'", Binary(ProgramOperation::Eq, Read("title"), Text("Boss"))},
            {"min(at(i, 1, 2), ByLevel('T', j))", Call("min", {Call("at", {Read("i"), Number("1"), Number("2")}), ByLevel("T", Read("j"))})},
            {"f(g(a, b), h(c))", Call("f", {Call("g", {Read("a"), Read("b")}), Call("h", {Read("c")})})},
            {"f()", Call("f", {})},
        };
        for (const FormulaCase& formula : cases) {
            SCOPED_TRACE(String(formula.text).c_str());
            const auto parsed = ParseExpression(formula.text);
            ASSERT_TRUE(parsed.has_value());
            // The cases are written in the canonical form, so the text is also what the printer makes of the parsed text
            EXPECT_EQ(PrintExpression(*parsed), String(formula.text));

            const auto built = BuildAst(*formula.tree);
            ASSERT_TRUE(built.has_value()) << built.error().message.c_str();
            EXPECT_TRUE(SameTree(*built, *parsed));

            const auto printed = BuildFormulaText(*formula.tree);
            ASSERT_TRUE(printed.has_value()) << printed.error().message.c_str();
            EXPECT_EQ(*printed, String(formula.text));
        }
    }

    TEST(CalcNodeTest, EveryBinaryOperatorPrintsAndParsesBack) {
        for (const ProgramOperation op : kBinaryOperations) {
            const Node tree = Binary(op, Read("a"), Read("b"));
            const auto printed = BuildFormulaText(*tree);
            ASSERT_TRUE(printed.has_value()) << printed.error().message.c_str();
            EXPECT_EQ(*printed, Format("a {} b", OperatorSymbol(op)));
            const auto parsed = ParseExpression(*printed);
            ASSERT_TRUE(parsed.has_value());
            const auto built = BuildAst(*tree);
            ASSERT_TRUE(built.has_value());
            EXPECT_TRUE(SameTree(*built, *parsed)) << EnumUtils<ProgramOperation>::ToString(op).data();
        }
    }

    TEST(CalcNodeTest, ANumberKeepsItsFloatness) {
        const auto whole = BuildAst(*Number("12"));
        const auto fractional = BuildAst(*Number("12.0"));
        ASSERT_TRUE(whole.has_value());
        ASSERT_TRUE(fractional.has_value());
        EXPECT_FALSE(whole->nodes.front().isFloat);
        EXPECT_TRUE(fractional->nodes.front().isFloat);
    }

    TEST(CalcNodeTest, EveryClassIsMadeByNameWithItsFields) {
        struct ClassCase {
            StringView name;
            Vector<StringView> fields;
        };
        const ClassCase classes[] = {
            {"CalcNumber", {"text"}},
            {"CalcBool", {"value"}},
            {"CalcText", {"text"}},
            {"CalcRead", {"path"}},
            {"CalcUnary", {"op", "operand"}},
            {"CalcBinary", {"op", "left", "right"}},
            {"CalcCall", {"function", "args"}},
            {"CalcMember", {"base", "name"}},
            {"CalcByLevel", {"table", "index"}},
        };
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        for (const ClassCase& entry : classes) {
            SCOPED_TRACE(String(entry.name).c_str());
            const Node made = registry.CreateAs<CalcNode>(entry.name);
            ASSERT_TRUE(made);
            EXPECT_EQ(made->GetTypeMeta().typeName, entry.name);

            const TypeInfo* info = registry.Find(entry.name);
            ASSERT_NE(info, nullptr);
            Vector<StringView> names;
            for (const FieldRef& field : registry.AllFields(*info)) {
                names.push_back(field.field->name);
            }
            EXPECT_EQ(names, entry.fields);
        }
        EXPECT_FALSE(registry.CreateAs<CalcNode>("CalcFoo"));
        EXPECT_FALSE(registry.CreateAs<CalcNode>("CalcNode")) << "the base is abstract: a tree is never made of it";
    }

    TEST(CalcNodeTest, AMissingChildIsNamedByItsPath) {
        auto binary = New<CalcBinary>();
        binary->op = ProgramOperation::Add;
        binary->left = Read("a");
        EXPECT_TRUE(Contains(ExpectRefused(*binary), "CalcBinary: 'right' is missing (at tree)"));

        auto call = New<CalcCall>();
        call->function = "min";
        call->args.push_back(Read("a"));
        call->args.push_back(Node());
        EXPECT_TRUE(Contains(ExpectRefused(*call), "CalcCall: 'args[1]' is missing (at tree)"));

        const Node nested = Binary(ProgramOperation::Mul, Read("a"), Call("min", {Read("b"), Node()}));
        EXPECT_TRUE(Contains(ExpectRefused(*nested), "CalcCall: 'args[1]' is missing (at tree.right)"));

        auto unary = New<CalcUnary>();
        EXPECT_TRUE(Contains(ExpectRefused(*unary), "'operand' is missing"));
        auto member = New<CalcMember>();
        member->name = "x";
        EXPECT_TRUE(Contains(ExpectRefused(*member), "'base' is missing"));
        auto byLevel = New<CalcByLevel>();
        byLevel->table = "T";
        EXPECT_TRUE(Contains(ExpectRefused(*byLevel), "'index' is missing"));
    }

    TEST(CalcNodeTest, AnOperatorOfAnotherArityIsRefused) {
        EXPECT_TRUE(Contains(ExpectRefused(*Unary(ProgramOperation::Add, Read("a"))), "CalcUnary: 'op' Add is not a unary operator"));
        EXPECT_TRUE(Contains(ExpectRefused(*Binary(ProgramOperation::Neg, Read("a"), Read("b"))), "CalcBinary: 'op' Neg is not a binary operator"));
        EXPECT_TRUE(Contains(ExpectRefused(*Binary(ProgramOperation::Not, Read("a"), Read("b"))), "CalcBinary: 'op' Not"));
        EXPECT_TRUE(Contains(ExpectRefused(*Binary(ProgramOperation::Min, Read("a"), Read("b"))), "CalcBinary: 'op' Min"));
    }

    TEST(CalcNodeTest, ALeafThatWouldReadAsMoreThanOneTokenIsRefused) {
        EXPECT_TRUE(Contains(ExpectRefused(*Number("1 + 2")), "CalcNumber: 'text'"));
        EXPECT_TRUE(Contains(ExpectRefused(*Number("-2")), "a sign"));
        EXPECT_TRUE(Contains(ExpectRefused(*Number("")), "CalcNumber"));
        EXPECT_TRUE(Contains(ExpectRefused(*Number("1..2")), "CalcNumber"));
        EXPECT_TRUE(Contains(ExpectRefused(*Read("a + b")), "CalcRead: 'path'"));
        EXPECT_TRUE(Contains(ExpectRefused(*Read("a.")), "CalcRead: 'path'"));
        EXPECT_TRUE(Contains(ExpectRefused(*Read("")), "CalcRead: 'path'"));
        EXPECT_TRUE(Contains(ExpectRefused(*Read("true")), "CalcBool"));
        EXPECT_TRUE(Contains(ExpectRefused(*Text("a' + 'b")), "CalcText: 'text' cannot hold a quote"));
        EXPECT_TRUE(Contains(ExpectRefused(*ByLevel("T') + (1", Read("i"))), "CalcByLevel: 'table' cannot hold a quote"));
        EXPECT_TRUE(Contains(ExpectRefused(*Member(Read("a"), "x.y")), "CalcMember: 'name'"));
        EXPECT_TRUE(Contains(ExpectRefused(*Call("min(", {})), "CalcCall: 'function'"));
    }

    TEST(CalcNodeTest, ATextTheParserRefusesIsTheParsersToReport) {
        // The builder checks only that a leaf is one token; whether it is a name the language knows, a string it can hold or a number in range is the parser's and the checker's
        const Node tree = Text("caf\xC3\xA9");
        const auto printed = BuildFormulaText(*tree);
        ASSERT_TRUE(printed.has_value());
        EXPECT_FALSE(ParseExpression(*printed).has_value());
    }

    TEST(CalcNodeTest, TheHeightLimitStopsTheWalk) {
        const Node atLimit = DeepChain(ExprLimits::kMaxTreeHeight);
        const auto fits = BuildFormulaText(*atLimit);
        EXPECT_TRUE(fits.has_value()) << (fits.has_value() ? String() : fits.error().message).c_str();

        const Node over = DeepChain(ExprLimits::kMaxTreeHeight + 1);
        const String message = ExpectRefused(*over);
        EXPECT_TRUE(Contains(message, "levels high")) << message.c_str();
        EXPECT_TRUE(Contains(message, "(at tree.operand.operand")) << message.c_str();
    }

    TEST(CalcNodeTest, AVeryDeepTreeNeverRecursesToItsEnd) {
        const size_t levels = 1000;
        const Node deep = DeepChain(levels);
        EXPECT_TRUE(Contains(ExpectRefused(*deep), "levels high"));
    }

    TEST(CalcNodeTest, TheNodeLimitStopsAWideTree) {
        auto call = New<CalcCall>();
        call->function = "f";
        for (size_t i = 0; i < ExprLimits::kMaxNodes; ++i) {
            call->args.push_back(Number("1"));
        }
        // The call itself is the 513th part
        const String message = ExpectRefused(*call);
        EXPECT_TRUE(Contains(message, "more than 512 parts")) << message.c_str();

        auto fits = New<CalcCall>();
        fits->function = "f";
        for (size_t i = 0; i + 1 < ExprLimits::kMaxNodes; ++i) {
            fits->args.push_back(Number("1"));
        }
        EXPECT_TRUE(BuildFormulaText(*fits).has_value());
    }

}  // namespace BECore::Tests
