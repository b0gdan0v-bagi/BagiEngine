// The type checker over lists (Values/Programs/ExprChecker, Lists/CLAUDE.md): `size(list)`, `list[i]` and the fields of an element, as typed nodes with their offsets and
// descriptors, and the refusal of a list or an element used as a value. The folds are in the second half of the file.

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

}  // namespace BECore::Tests
