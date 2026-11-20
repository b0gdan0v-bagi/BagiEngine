// The type checker over lists (Values/Programs/ExprChecker, Lists/CLAUDE.md): `size(list)`, `list[i]` and the fields of an element, as typed nodes with their offsets and
// descriptors, and the refusal of a list or an element used as a value; then the folds with their scopes, result types and errors.

#include <BECore/ProgramListTypes.h>
#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramListData;

    namespace {

        constexpr LevelIndex kHand = 0;
        constexpr LevelIndex kRefused = 1;
        constexpr LevelIndex kStock = 2;

        struct ListChecker {
            ContextDecl declaration;

            ListChecker() {
                EXPECT_EQ(declaration.Add<ProgramListHand>("hand").value_or(LevelIndex{255}), kHand);
                EXPECT_EQ(declaration.Add<ProgramListRefused>("refused").value_or(LevelIndex{255}), kRefused);
                EXPECT_EQ(declaration.Add<ProgramListStock>("stock").value_or(LevelIndex{255}), kStock);
            }
        };

        template <typename T, typename Member>
        uint32_t OffsetIn(const T& object, const Member& member) {
            return static_cast<uint32_t>(reinterpret_cast<const char*>(&member) - reinterpret_cast<const char*>(&object));
        }

        std::expected<TypedExpr, ProgramError> Check(const ContextDecl& declaration, StringView text, const TypeDesc& expected) {
            const auto parsed = ParseExpression(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str() << ": " << (parsed.has_value() ? String{} : parsed.error().message).c_str();
            if (!parsed.has_value()) {
                return std::unexpected(parsed.error());
            }
            return CheckExpression(*parsed, declaration, expected);
        }

        TypedExpr Accepted(const ContextDecl& declaration, StringView text, const TypeDesc& expected) {
            auto checked = Check(declaration, text, expected);
            EXPECT_TRUE(checked.has_value()) << String(text).c_str() << ": " << (checked.has_value() ? String{} : checked.error().message).c_str();
            return checked.has_value() ? std::move(*checked) : TypedExpr{};
        }

        /// The message must say every one of @p words and the column is the first byte of @p at in the text
        ProgramError Refused(const ContextDecl& declaration, StringView text, const TypeDesc& expected, StringView at, std::initializer_list<StringView> words) {
            const auto checked = Check(declaration, text, expected);
            EXPECT_FALSE(checked.has_value()) << String(text).c_str();
            if (checked.has_value()) {
                return {};
            }
            const size_t position = text.find(at);
            EXPECT_NE(position, StringView::npos) << String(at).c_str();
            EXPECT_EQ(checked.error().column, position + 1) << String(text).c_str() << ": " << checked.error().message.c_str();
            for (const StringView word : words) {
                EXPECT_TRUE(Contains(checked.error().message, word)) << String(text).c_str() << ": " << checked.error().message.c_str() << " does not say '" << String(word).c_str() << "'";
            }
            return checked.error();
        }

        const TypedNode& Root(const TypedExpr& expr) {
            return expr[expr.root];
        }

        /// The operand @p index of @p node
        const TypedNode& Operand(const TypedExpr& expr, const TypedNode& node, size_t index) {
            EXPECT_LT(index, node.operandCount);
            return expr[node.operands[index < node.operandCount ? index : 0]];
        }

        template <typename T>
        T ConstantOf(const TypedNode& node) {
            EXPECT_EQ(node.kind, TypedNodeKind::Constant);
            T value{};
            std::memcpy(&value, node.constant.bytes.data(), sizeof(T));
            return value;
        }

        ListDescriptor DescriptorAt(uint32_t offset, const TypeDesc& list) {
            const auto descriptor = MakeListDescriptor(offset, list);
            EXPECT_TRUE(descriptor.has_value());
            return descriptor.value_or(ListDescriptor{});
        }

    }  // namespace

    TEST(ProgramListCheckerTest, SizeReadsTheListOfALevelOfEveryContainer) {
        const ListChecker context;
        const ProgramListHand hand{};
        const TypedExpr tiles = Accepted(context.declaration, "size(hand.tiles)", DescribeType<int32_t>());
        ASSERT_EQ(tiles.nodes.size(), 2U);
        EXPECT_EQ(Root(tiles).kind, TypedNodeKind::Operation);
        EXPECT_EQ(Root(tiles).opcode, Opcode::ListSize);
        EXPECT_EQ(Root(tiles).type, ExprType(ProgramType::Int32));
        const TypedNode& list = Operand(tiles, Root(tiles), 0);
        EXPECT_EQ(list.kind, TypedNodeKind::List);
        EXPECT_EQ(list.slot.level, kHand);
        EXPECT_EQ(list.list, DescriptorAt(OffsetIn(hand, hand.tiles), DescribeType<Array<ProgramListTile, 14>>()));
        EXPECT_EQ(list.list.layout, ListLayout::Inline);
        EXPECT_EQ(list.list.count, 14U);

        struct Row {
            StringView text;
            ListLayout layout;
            uint32_t stride;
        };
        const Row rows[] = {{"size(hand.sets)", ListLayout::Pointers, sizeof(ProgramListSet)},
                            {"size(hand.counts)", ListLayout::Pointers, sizeof(int32_t)},
                            {"size(hand.weights)", ListLayout::Pointers, sizeof(float)},
                            {"size(hand.flags)", ListLayout::Inline, sizeof(bool)}};
        for (const Row& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.text, DescribeType<int32_t>());
            ASSERT_EQ(Root(expr).operandCount, 1U) << String(row.text).c_str();
            const TypedNode& node = Operand(expr, Root(expr), 0);
            EXPECT_EQ(node.list.layout, row.layout) << String(row.text).c_str();
            EXPECT_EQ(node.list.stride, row.stride) << String(row.text).c_str();
        }
        const TypedExpr nested = Accepted(context.declaration, "size(refused.set.tiles)", DescribeType<int32_t>());
        const ProgramListRefused refused{};
        EXPECT_EQ(Operand(nested, Root(nested), 0).slot.level, kRefused);
        EXPECT_EQ(Operand(nested, Root(nested), 0).list.offset, OffsetIn(refused, refused.set.tiles));
    }

    TEST(ProgramListCheckerTest, ASizeIsAnIntThatTakesAPartInArithmetic) {
        const ListChecker context;
        const TypedExpr expr = Accepted(context.declaration, "size(hand.sets) * 2 + hand.before", DescribeType<int32_t>());
        EXPECT_EQ(Root(expr).opcode, OpcodeOf(ProgramOperation::Add, ProgramType::Int32));
        // The literal beside a size is an int and beside a float read it is a float
        const TypedExpr product = Accepted(context.declaration, "hand.weights[0] * 2", DescribeType<float>());
        EXPECT_EQ(ConstantOf<float>(Operand(product, Root(product), 1)), 2.0f);
    }

    TEST(ProgramListCheckerTest, AnElementOfAListOfObjectsIsReadFieldByField) {
        const ListChecker context;
        const ProgramListTile tile{};
        const TypedExpr suit = Accepted(context.declaration, "hand.tiles[3].suit", DescribeType<ProgramListSuit>());
        EXPECT_EQ(Root(suit).opcode, Opcode::LoadElemEnum);
        EXPECT_EQ(Root(suit).immediate, OffsetIn(tile, tile.suit));
        EXPECT_EQ(Root(suit).type, ExprType::OfEnum(DescribeType<ProgramListSuit>()));
        const TypedNode& element = Operand(suit, Root(suit), 0);
        ASSERT_EQ(element.kind, TypedNodeKind::Element);
        ASSERT_EQ(element.operandCount, 2U);
        EXPECT_EQ(Operand(suit, element, 0).kind, TypedNodeKind::List);
        EXPECT_EQ(ConstantOf<int32_t>(Operand(suit, element, 1)), 3);

        const struct {
            StringView text;
            const TypeDesc& type;
            Opcode opcode;
            uint32_t offset;
        } rows[] = {{"hand.tiles[0].rank", DescribeType<int32_t>(), Opcode::LoadElemInt32, OffsetIn(tile, tile.rank)},
                    {"hand.tiles[0].terminal", DescribeType<bool>(), Opcode::LoadElemBool, OffsetIn(tile, tile.terminal)},
                    {"hand.tiles[1].weight", DescribeType<float>(), Opcode::LoadElemFloat, OffsetIn(tile, tile.weight)},
                    {"hand.tiles[1].name", DescribeType<PoolString>(), Opcode::LoadElemString, OffsetIn(tile, tile.name)},
                    {"hand.tiles[1].position", DescribeType<Vec3>(), Opcode::LoadElemVec3, OffsetIn(tile, tile.position)}};
        for (const auto& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.text, row.type);
            EXPECT_EQ(Root(expr).opcode, row.opcode) << String(row.text).c_str();
            EXPECT_EQ(Root(expr).immediate, row.offset) << String(row.text).c_str();
        }
    }

    TEST(ProgramListCheckerTest, ComponentsAndEmbeddedObjectsOfAnElementAreOnePath) {
        const ListChecker context;
        const ProgramListTile tile{};
        const ProgramListCrate crate{};
        const TypedExpr y = Accepted(context.declaration, "hand.tiles[2].position.y", DescribeType<float>());
        EXPECT_EQ(Root(y).opcode, Opcode::LoadElemFloat) << "a component stored as the program computes with it is read where it is, with no Extract";
        EXPECT_EQ(Root(y).immediate, OffsetIn(tile, tile.position) + sizeof(float));
        const TypedExpr rank = Accepted(context.declaration, "stock.crates[0].tile.rank", DescribeType<int32_t>());
        EXPECT_EQ(Root(rank).immediate, OffsetIn(crate, crate.tile) + OffsetIn(tile, tile.rank));
        // The element of a list of scalars is already a value, and the component is taken from it
        const TypedExpr scalarZ = Accepted(context.declaration, "refused.points[1].z", DescribeType<float>());
        EXPECT_EQ(Root(scalarZ).opcode, OpcodeOf(ProgramOperation::Extract, ProgramType::Vec3));
        EXPECT_EQ(Root(scalarZ).immediate, 2U);
        EXPECT_EQ(Operand(scalarZ, Root(scalarZ), 0).opcode, Opcode::LoadElemVec3);
    }

    TEST(ProgramListCheckerTest, AnElementOfTheListOfAnElementIsReachedThroughItsOwner) {
        const ListChecker context;
        const ProgramListTile tile{};
        const ProgramListSet set{};
        const TypedExpr expr = Accepted(context.declaration, "hand.sets[1].tiles[2].rank", DescribeType<int32_t>());
        EXPECT_EQ(Root(expr).opcode, Opcode::LoadElemInt32);
        EXPECT_EQ(Root(expr).immediate, OffsetIn(tile, tile.rank));
        const TypedNode& inner = Operand(expr, Root(expr), 0);
        ASSERT_EQ(inner.kind, TypedNodeKind::Element);
        EXPECT_EQ(ConstantOf<int32_t>(Operand(expr, inner, 1)), 2);
        const TypedNode& tiles = Operand(expr, inner, 0);
        ASSERT_EQ(tiles.kind, TypedNodeKind::List);
        EXPECT_EQ(tiles.list, DescriptorAt(OffsetIn(set, set.tiles), DescribeType<Array<ProgramListTile, 4>>()));
        ASSERT_EQ(tiles.operandCount, 1U) << "the list of an element names the element it is in";
        const TypedNode& outer = Operand(expr, tiles, 0);
        ASSERT_EQ(outer.kind, TypedNodeKind::Element);
        EXPECT_EQ(ConstantOf<int32_t>(Operand(expr, outer, 1)), 1);
        EXPECT_EQ(Operand(expr, outer, 0).kind, TypedNodeKind::List);
        EXPECT_EQ(Operand(expr, outer, 0).slot.level, kHand);
    }

    TEST(ProgramListCheckerTest, AListOfScalarsGivesTheValueAndTheIndexIsAnyInt) {
        const ListChecker context;
        const TypedExpr counts = Accepted(context.declaration, "hand.counts[1]", DescribeType<int32_t>());
        EXPECT_EQ(Root(counts).opcode, Opcode::LoadElemInt32);
        EXPECT_EQ(Root(counts).immediate, 0U);
        EXPECT_EQ(Operand(counts, Root(counts), 0).kind, TypedNodeKind::Element);

        const TypedExpr indexed = Accepted(context.declaration, "hand.counts[hand.counts[hand.before]]", DescribeType<int32_t>());
        const TypedNode& outer = Operand(indexed, Root(indexed), 0);
        EXPECT_EQ(Operand(indexed, outer, 1).opcode, Opcode::LoadElemInt32) << "the index is a value read from the list";
        const TypedExpr flags = Accepted(context.declaration, "hand.flags[2]", DescribeType<bool>());
        EXPECT_EQ(Root(flags).opcode, Opcode::LoadElemBool);
        const TypedExpr points = Accepted(context.declaration, "refused.points[0]", DescribeType<Vec3>());
        EXPECT_EQ(Root(points).opcode, Opcode::LoadElemVec3);
        const TypedExpr names = Accepted(context.declaration, "refused.names[0] == 'x'", DescribeType<bool>());
        EXPECT_EQ(Operand(names, Root(names), 0).opcode, Opcode::LoadElemString);
    }

    TEST(ProgramListCheckerTest, AListUsedAsAValueSaysHowToReadIt) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "hand.tiles", integer, "hand.tiles", {"'hand.tiles' is a list of ProgramListTile", "size(hand.tiles)", "hand.tiles[i]"});
        Refused(context.declaration, "size(hand.tiles) + hand.sets", integer, "hand.sets", {"'hand.sets' is a list of ProgramListSet"});
        Refused(context.declaration, "hand.counts == 1", DescribeType<bool>(), "hand.counts", {"a list of int"});
        Refused(context.declaration, "hand.sets[0].tiles", integer, ".tiles", {"'hand.sets[0].tiles' is a list of ProgramListTile"});
    }

    TEST(ProgramListCheckerTest, AnElementUsedAsAValueSaysWhichFieldToRead) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        // The column of an element is its `[`
        Refused(context.declaration, "hand.tiles[0]", integer, "[0]", {"'hand.tiles[0]'", "ProgramListTile", "read a field of it", "hand.tiles[0].suit"});
        Refused(context.declaration, "hand.sets[1].tiles[2] + 1", integer, "[2]", {"ProgramListTile", "hand.sets[1].tiles[2].suit"});
        Refused(context.declaration, "stock.crates[0] == 1", DescribeType<bool>(), "[0]", {"ProgramListCrate", "stock.crates[0].tile"});
    }

    TEST(ProgramListCheckerTest, AnIndexIsAnIntAndOnlyAListHasElements) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "hand.counts[hand.weights[0]]", integer, "[0]", {"the list 'hand.counts'", "an int as its index", "a float", "int(x)"});
        Refused(context.declaration, "hand.counts[1.5]", integer, "1.5", {"float literal", "an int is needed"});
        Refused(context.declaration, "hand.tiles[hand.flags[0]]", integer, "[0]", {"an int as its index", "a bool"});
        Refused(context.declaration, "hand.counts[0][1]", integer, "[1]", {"'hand.counts[0]' is an int and has no elements"});
        Refused(context.declaration, "hand.before[0]", integer, "[0]", {"'hand.before' is an int and has no elements"});
        Refused(context.declaration, "hand.tiles[0][1]", integer, "[1]", {"'hand.tiles[0]' is ProgramListTile, an element of a list and has no elements"});
        Refused(context.declaration, "size(hand.tiles)[0]", integer, "[0]", {"'size(...)' is an int"});
    }

    TEST(ProgramListCheckerTest, AFieldThatIsMissingOrNoValueNamesWhatIsThere) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "hand.tiles[0].rnk", integer, ".rnk", {"'ProgramListTile'", "no field 'rnk'", "did you mean 'rank'", "its fields are: suit, terminal, rank"});
        Refused(context.declaration, "hand.tiles[0].position.w", integer, ".position.w", {"'.w' is not a component of vec3", "x, y, z"});
        Refused(context.declaration, "hand.tiles[0].rank.x", integer, ".rank.x", {"an int has no components"});
        Refused(context.declaration, "stock.crates[0].tile", DescribeType<bool>(), ".tile", {"the type ProgramListTile", "a formula reads only fields of the types"});
        Refused(context.declaration, "stock.crates[0].small", integer, ".small", {"uint8"});
        Refused(context.declaration, "stock.bytes[0]", integer, "[0]", {"'stock.bytes[0]'", "uint8", "a formula reads only values of the types"});
        Refused(context.declaration, "hand.sets[0].tiles.suit", integer, ".tiles.suit", {"needs the live object"});
    }

    TEST(ProgramListCheckerTest, ALevelThatDoesNotHoldTheListIsRefusedWithTheLevels) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "size(caster.tiles)", integer, "caster.tiles", {"unknown level 'caster'", "hand, refused, stock"});
        Refused(context.declaration, "size(refused.tiles)", integer, "refused.tiles", {"no field 'tiles'", "its fields are"});
        Refused(context.declaration, "size(refused.texts)", integer, "refused.texts", {"strings that own their text"});
        Refused(context.declaration, "size(hand.tiles[0].rank.x)", integer, ".rank.x", {"an int has no components"});
    }

    TEST(ProgramListCheckerTest, SizeNeedsOneListAndSaysWhatItGotInstead) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "size(5)", integer, "5", {"the function 'size' needs a list as its first argument", "'5' is an int"});
        Refused(context.declaration, "size(hand.before)", integer, "hand.before", {"'hand.before' is an int"});
        Refused(context.declaration, "size(hand.tiles[0])", integer, "[0]", {"ProgramListTile, an element of a list"});
        Refused(context.declaration, "size(hand.tiles[0].rank)", integer, ".rank", {"'hand.tiles[0].rank' is an int"});
        Refused(context.declaration, "size()", integer, "size", {"'size' takes 1 argument, but 0 are given"});
        Refused(context.declaration, "size(hand.tiles, 1)", integer, "size", {"'size' takes 1 argument, but 2 are given"});
        Refused(context.declaration, "size + 1", integer, "size", {"'size' is a function"});
        Refused(context.declaration, "size(hand.tiles)", DescribeType<float>(), "size", {"the formula is an int", "float"});
    }

    TEST(ProgramListCheckerTest, TheListFunctionsAreInTheMessageOfAnUnknownFunction) {
        const ListChecker context;
        Refused(context.declaration, "frobnicate(1)", DescribeType<int32_t>(), "frobnicate", {"unknown function 'frobnicate'", "size", "count", "any", "all", "sum"});
    }

    TEST(ProgramListCheckerTest, EachFoldHasItsResultType) {
        const ListChecker context;
        const struct {
            StringView text;
            const TypeDesc& expected;
            FoldKind fold;
            ProgramType type;
        } rows[] = {{"count(hand.sets, s, s.pung)", DescribeType<int32_t>(), FoldKind::Count, ProgramType::Int32},
                    {"any(hand.tiles, t, t.terminal)", DescribeType<bool>(), FoldKind::Any, ProgramType::Bool},
                    {"all(hand.tiles, t, t.rank > 3)", DescribeType<bool>(), FoldKind::All, ProgramType::Bool},
                    {"sum(hand.tiles, t, t.rank)", DescribeType<int32_t>(), FoldKind::Sum, ProgramType::Int32},
                    {"sum(hand.counts, c, int64(c))", DescribeType<int64_t>(), FoldKind::Sum, ProgramType::Int64},
                    {"sum(hand.tiles, t, t.weight)", DescribeType<float>(), FoldKind::Sum, ProgramType::Float},
                    {"sum(hand.tiles, t, double(t.weight))", DescribeType<double>(), FoldKind::Sum, ProgramType::Double},
                    {"min(hand.tiles, t, t.rank)", DescribeType<int32_t>(), FoldKind::Min, ProgramType::Int32},
                    {"max(hand.weights, w, w)", DescribeType<float>(), FoldKind::Max, ProgramType::Float}};
        for (const auto& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.text, row.expected);
            ASSERT_EQ(Root(expr).kind, TypedNodeKind::Fold) << String(row.text).c_str();
            EXPECT_EQ(Root(expr).fold, row.fold) << String(row.text).c_str();
            EXPECT_EQ(Root(expr).type, ExprType(row.type)) << String(row.text).c_str();
            ASSERT_EQ(Root(expr).operandCount, 1U);
            EXPECT_EQ(Operand(expr, Root(expr), 0).kind, TypedNodeKind::List) << String(row.text).c_str();
            EXPECT_LT(Root(expr).body, expr.root) << "the body comes before the fold, like an operand";
            EXPECT_EQ(Root(expr).loop, 0U);
        }
    }

    TEST(ProgramListCheckerTest, TheBodyReadsTheElementThroughTheVariable) {
        const ListChecker context;
        const ProgramListTile tile{};
        const TypedExpr expr = Accepted(context.declaration, "sum(hand.tiles, t, t.rank)", DescribeType<int32_t>());
        const TypedNode& body = expr[Root(expr).body];
        EXPECT_EQ(body.opcode, Opcode::LoadElemInt32);
        EXPECT_EQ(body.immediate, OffsetIn(tile, tile.rank));
        const TypedNode& variable = Operand(expr, body, 0);
        ASSERT_EQ(variable.kind, TypedNodeKind::Element);
        EXPECT_EQ(variable.operandCount, 0U) << "a variable has no operands: it is the element of its loop";
        EXPECT_EQ(variable.loop, Root(expr).loop);

        // A list of scalars: the variable alone is the value
        const TypedExpr scalar = Accepted(context.declaration, "sum(hand.counts, c, c)", DescribeType<int32_t>());
        EXPECT_EQ(scalar[Root(scalar).body].opcode, Opcode::LoadElemInt32);
        EXPECT_EQ(scalar[Root(scalar).body].immediate, 0U);
        const TypedExpr flags = Accepted(context.declaration, "any(hand.flags, f, f)", DescribeType<bool>());
        EXPECT_EQ(flags[Root(flags).body].opcode, Opcode::LoadElemBool);
        // A component of a scalar element and a field through an embedded object
        const TypedExpr points = Accepted(context.declaration, "sum(refused.points, p, p.y)", DescribeType<float>());
        EXPECT_EQ(points[Root(points).body].opcode, Opcode::LoadElemFloat) << "the component of a variable is read where it is";
        EXPECT_EQ(points[Root(points).body].immediate, sizeof(float));
        const ProgramListCrate crate{};
        const TypedExpr crates = Accepted(context.declaration, "sum(stock.crates, k, k.tile.rank)", DescribeType<int32_t>());
        EXPECT_EQ(crates[Root(crates).body].immediate, OffsetIn(crate, crate.tile) + OffsetIn(tile, tile.rank));
        const TypedExpr position = Accepted(context.declaration, "sum(hand.tiles, t, t.position.z)", DescribeType<float>());
        EXPECT_EQ(position[Root(position).body].opcode, Opcode::LoadElemFloat);
        EXPECT_EQ(position[Root(position).body].immediate, OffsetIn(tile, tile.position) + 2 * sizeof(float));
    }

    TEST(ProgramListCheckerTest, ALiteralBodyTakesTheExpectedTypeElseInt) {
        const ListChecker context;
        const TypedExpr plain = Accepted(context.declaration, "sum(hand.counts, c, 1)", DescribeType<int32_t>());
        EXPECT_EQ(ConstantOf<int32_t>(plain[Root(plain).body]), 1);
        const TypedExpr wide = Accepted(context.declaration, "sum(hand.counts, c, 1)", DescribeType<int64_t>());
        EXPECT_EQ(Root(wide).type, ExprType(ProgramType::Int64));
        EXPECT_EQ(ConstantOf<int64_t>(wide[Root(wide).body]), 1);
        const TypedExpr real = Accepted(context.declaration, "max(hand.counts, c, 2.5)", DescribeType<float>());
        EXPECT_EQ(ConstantOf<float>(real[Root(real).body]), 2.5f);
        // Beside the fold the literal is an int, as beside any int
        const TypedExpr compared = Accepted(context.declaration, "count(hand.sets, s, s.pung) == 3", DescribeType<bool>());
        EXPECT_EQ(ConstantOf<int32_t>(Operand(compared, Root(compared), 1)), 3);
    }

    TEST(ProgramListCheckerTest, ALiteralBesideTheVariableTakesItsType) {
        const ListChecker context;
        const TypedExpr product = Accepted(context.declaration, "sum(hand.weights, w, w * 2)", DescribeType<float>());
        EXPECT_EQ(ConstantOf<float>(Operand(product, product[Root(product).body], 1)), 2.0f);
        const TypedExpr left = Accepted(context.declaration, "any(hand.weights, w, 2 * w > 3)", DescribeType<bool>());
        EXPECT_EQ(left[Root(left).body].opcode, OpcodeOf(ProgramOperation::Gt, ProgramType::Float));
        // A table beside the bare variable has no type of its own, so the variable is read first: the entries are floats, not ints
        const TypedExpr table = Accepted(context.declaration, "any(hand.weights, w, at(0, 1, 2) + w > 1)", DescribeType<bool>());
        EXPECT_EQ(table[Root(table).body].opcode, OpcodeOf(ProgramOperation::Gt, ProgramType::Float));
    }

    TEST(ProgramListCheckerTest, LoopsNestAndNumberThemselvesInTheOrderTheyAreMet) {
        const ListChecker context;
        const TypedExpr expr = Accepted(context.declaration, "count(hand.sets, s, count(s.tiles, t, t.rank > 3 && s.pung) > 1)", DescribeType<int32_t>());
        ASSERT_EQ(Root(expr).kind, TypedNodeKind::Fold);
        EXPECT_EQ(Root(expr).loop, 0U);
        const TypedNode& outerBody = expr[Root(expr).body];
        EXPECT_EQ(outerBody.opcode, OpcodeOf(ProgramOperation::Gt, ProgramType::Int32));
        const TypedNode& inner = Operand(expr, outerBody, 0);
        ASSERT_EQ(inner.kind, TypedNodeKind::Fold);
        EXPECT_EQ(inner.loop, 1U);
        const TypedNode& innerList = Operand(expr, inner, 0);
        ASSERT_EQ(innerList.kind, TypedNodeKind::List);
        ASSERT_EQ(innerList.operandCount, 1U);
        EXPECT_EQ(Operand(expr, innerList, 0).kind, TypedNodeKind::Element);
        EXPECT_EQ(Operand(expr, innerList, 0).loop, 0U) << "the list of the inner loop is a field of the outer element";
        // The inner body reads both variables
        const TypedNode& both = expr[inner.body];
        EXPECT_EQ(both.opcode, OpcodeOf(ProgramOperation::And, ProgramType::Bool));
        EXPECT_EQ(Operand(expr, Operand(expr, Operand(expr, both, 0), 0), 0).loop, 1U);
        EXPECT_EQ(Operand(expr, Operand(expr, both, 1), 0).loop, 0U);

        // Two loops side by side may use one name, and each has a number of its own
        const TypedExpr siblings = Accepted(context.declaration, "count(hand.sets, s, s.pung) + count(hand.sets, s, !s.pung)", DescribeType<int32_t>());
        EXPECT_EQ(Operand(siblings, Root(siblings), 0).loop, 0U);
        EXPECT_EQ(Operand(siblings, Root(siblings), 1).loop, 1U);
    }

    TEST(ProgramListCheckerTest, AnElementNameIsOneThingAndAFoldTakesAPlainOne) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "count(hand.sets, s, count(s.tiles, s, s.terminal) > 1)", integer, "s, s.terminal", {"'s' is already the element of the enclosing loop over 'hand.sets'"});
        Refused(context.declaration, "count(hand.tiles, hand, true)", integer, "hand, true", {"'hand' is a level of the context", "a name of its own"});
        Refused(context.declaration, "count(hand.tiles, min, true)", integer, "min, true", {"'min' is a function"});
        Refused(context.declaration, "count(hand.tiles, size, true)", integer, "size, true", {"'size' is a function"});
        Refused(context.declaration, "count(hand.tiles, int, true)", integer, "int, true", {"'int' is a function"});
        Refused(context.declaration, "count(hand.tiles, bool, true)", integer, "bool, true", {"'bool' is the name of a type"});
        Refused(context.declaration, "count(hand.tiles, string, true)", integer, "string, true", {"the name of a type"});
        Refused(context.declaration, "count(hand.tiles, true, true)", integer, "true, true", {"a plain name"});
        Refused(context.declaration, "count(hand.tiles, 5, true)", integer, "5, true", {"a plain name"});
        Refused(context.declaration, "count(hand.tiles, t.x, true)", integer, "t.x", {"a plain name", "not a value or a path"});
        // The variable is the loop's alone: after the call it is an unknown name again, and the list cannot use it
        Refused(context.declaration, "t + count(hand.tiles, t, true)", integer, "t", {"unknown name 't'"});
        Refused(context.declaration, "count(t.tiles, t, true)", integer, "t.tiles", {"unknown level 't'"});
    }

    TEST(ProgramListCheckerTest, TheBodyOfAFoldHasTheTypeItNeeds) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "any(hand.tiles, t, t.rank)", DescribeType<bool>(), "t.rank", {"the function 'any' needs a bool body", "an int", "compare it"});
        Refused(context.declaration, "count(hand.tiles, t, t.weight)", integer, "t.weight", {"the function 'count'", "a float"});
        Refused(context.declaration, "all(hand.tiles, t, 1)", DescribeType<bool>(), "1)", {"the function 'all' needs a bool body", "an int"});
        Refused(context.declaration, "sum(hand.tiles, t, t.terminal)", integer, "t.terminal", {"the function 'sum' needs a number body", "a bool"});
        Refused(context.declaration, "max(hand.tiles, t, t.position)", integer, "t.position", {"number body", "a vec3"});
        Refused(context.declaration, "min(hand.tiles, t, t.suit)", integer, "t.suit", {"number body", "an enum"});
        Refused(context.declaration, "sum(hand.tiles, t, t.name)", integer, "t.name", {"number body", "a string", "int(x)"});
        // The result is the body's type, with no conversion
        Refused(context.declaration, "sum(hand.tiles, t, t.rank)", DescribeType<float>(), "sum", {"the formula is an int", "float"});
        Refused(context.declaration, "count(hand.tiles, t, t.terminal) + sum(hand.tiles, t, t.weight)", integer, "sum", {"one type", "int and float"});
    }

    TEST(ProgramListCheckerTest, AListOrAnElementIsNoBodyAndNoOperand) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        Refused(context.declaration, "sum(hand.sets, s, s.tiles)", integer, "s.tiles)", {"'s.tiles' is a list of ProgramListTile"});
        Refused(context.declaration, "any(hand.tiles, t, t)", DescribeType<bool>(), "t)", {"'t' is ProgramListTile, an element of a list", "t.suit"});
        Refused(context.declaration, "hand.tiles + count(hand.tiles, t, t.rank > 3)", integer, "hand.tiles", {"a list of ProgramListTile"});
        Refused(context.declaration, "sum(hand.tiles, t, t.rank) == hand.sets[0]", DescribeType<bool>(), "[0]", {"ProgramListSet", "hand.sets[0].tiles"});
        Refused(context.declaration, "count(hand.tiles[0], t, true)", integer, "[0]", {"the function 'count' needs a list as its first argument", "ProgramListTile, an element of a list"});
        Refused(context.declaration, "count(hand.sets, s, s.tiles.suit == 0)", integer, "s.tiles.suit", {"needs the live object"});
    }

    TEST(ProgramListCheckerTest, TheArityDecidesBetweenTheRowAndTheFold) {
        const ListChecker context;
        const TypeDesc& integer = DescribeType<int32_t>();
        const TypedExpr row = Accepted(context.declaration, "min(hand.before, 2)", integer);
        EXPECT_EQ(Root(row).kind, TypedNodeKind::Operation);
        EXPECT_EQ(Root(row).opcode, OpcodeOf(ProgramOperation::Min, ProgramType::Int32));
        const TypedExpr fold = Accepted(context.declaration, "max(hand.tiles, t, t.rank)", integer);
        EXPECT_EQ(Root(fold).kind, TypedNodeKind::Fold);
        Refused(context.declaration, "min(1, 2, 3)", integer, "1,", {"the function 'min' needs a list as its first argument", "'1' is an int"});
        Refused(context.declaration, "min(hand.before)", integer, "min", {"'min' takes 2 arguments, but 1 is given"});
        Refused(context.declaration, "max(hand.before, 1, 2, 3)", integer, "max", {"'max' takes 2 arguments, but 4 are given"});
        Refused(context.declaration, "count(hand.tiles, t)", integer, "count", {"'count' takes 3 arguments, but 2 are given"});
        // `all`, not `any`: the toy operations of ProgramDomainTypes.h, registered for the whole process by other cases, have an `any` of one argument
        Refused(context.declaration, "all(hand.tiles)", DescribeType<bool>(), "all", {"'all' takes 3 arguments, but 1 is given"});
        Refused(context.declaration, "sum(hand.tiles, t, t.rank, 1)", integer, "sum", {"'sum' takes 3 arguments, but 4 are given"});
        Refused(context.declaration, "count + 1", integer, "count", {"'count' is a function"});
    }

}  // namespace BECore::Tests
