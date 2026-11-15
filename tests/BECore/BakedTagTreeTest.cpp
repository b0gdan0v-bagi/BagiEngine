#include <BECore/Rules/TagTree.h>
#include <gtest/gtest.h>

/// The baked, memcpy-able form of a TagTree: the same ids and answers as the tree it came from, byte-equal for the same input.
namespace BECore::Tests {

    namespace {

        constexpr StringView kReactionVocabulary[] = {"match", "match.adjacent", "match.direct", "match.direct.long", "booster.hammer", "special.rocket", "cover"};

        /// A baked tree of the paths, filled into a zeroed block.
        BakedTagTree BakeOf(Span<const StringView> paths) {
            TagTree tree;
            for (const StringView path : paths) {
                EXPECT_TRUE(tree.Add(path).has_value()) << ToStd(path);
            }
            tree.Bake();
            BakedTagTree block{};
            tree.FillBaked(block);
            return block;
        }

        BakedTagTree BakeOf(std::initializer_list<StringView> paths) {
            return BakeOf(Span<const StringView>{paths.begin(), paths.size()});
        }

        TagId IdOf(const BakedTagTree& tree, StringView path) {
            const auto id = tree.ResolveTag(path);
            EXPECT_TRUE(id.has_value()) << ToStd(path);
            return id.value_or(kNoTag);
        }

        TagRange RangeOf(const BakedTagTree& tree, StringView pattern) {
            const auto range = tree.Resolve(pattern);
            EXPECT_TRUE(range.has_value()) << ToStd(pattern);
            return range.value_or(TagRange{});
        }

        bool Mentions(const String& text, StringView part) {
            return text.find(String(part)) != String::npos;
        }

        String Text(StringView view) {
            return String(view);
        }

        /// True when `a` is `b` or one of the parents of `b`.
        bool IsAncestorOrSelf(const BakedTagTree& tree, TagId a, TagId b) {
            for (TagId id = b; id != kNoTag; id = tree.Parent(id)) {
                if (id == a) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST(BakedTagTreeTest, SubtreesAreNestedRanges) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        ASSERT_EQ(tree.Count(), 9u);  // booster and special are implied
        const TagId match = IdOf(tree, "match");
        const TagId adjacent = IdOf(tree, "match.adjacent");
        const TagId direct = IdOf(tree, "match.direct");
        const TagId directLong = IdOf(tree, "match.direct.long");
        const TagId booster = IdOf(tree, "booster");

        EXPECT_EQ(match, 0);
        EXPECT_EQ(adjacent, 1);
        EXPECT_EQ(direct, 2);
        EXPECT_EQ(directLong, 3);
        EXPECT_EQ(tree.Range(match), (TagRange{match, directLong}));
        EXPECT_EQ(tree.Range(direct), (TagRange{direct, directLong}));
        EXPECT_EQ(tree.Range(adjacent), (TagRange{adjacent, adjacent}));
        EXPECT_EQ(tree.Range(directLong), (TagRange{directLong, directLong}));
        EXPECT_EQ(booster, directLong + 1);
        EXPECT_EQ(tree.Parent(directLong), direct);
        EXPECT_EQ(tree.Parent(direct), match);
        EXPECT_EQ(tree.Parent(match), kNoTag);
    }

    TEST(BakedTagTreeTest, RangesAreNestedOrDisjointAndAgreeWithTheParentChain) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        for (TagId a = 0; a < tree.Count(); ++a) {
            const TagRange rangeA = tree.Range(a);
            ASSERT_FALSE(rangeA.IsEmpty());
            for (TagId b = 0; b < tree.Count(); ++b) {
                const TagRange rangeB = tree.Range(b);
                const bool nested = rangeA.Contains(rangeB.lo) && rangeA.Contains(rangeB.hi);
                const bool disjoint = rangeA.hi < rangeB.lo || rangeB.hi < rangeA.lo;
                const bool reverseNested = rangeB.Contains(rangeA.lo) && rangeB.Contains(rangeA.hi);
                EXPECT_TRUE(nested || disjoint || reverseNested) << ToStd(tree.Name(a)) << " / " << ToStd(tree.Name(b));
                EXPECT_EQ(rangeA.Contains(b), IsAncestorOrSelf(tree, a, b)) << ToStd(tree.Name(a)) << " contains " << ToStd(tree.Name(b));
            }
        }
    }

    TEST(BakedTagTreeTest, DescendantRangeLeavesOutTheTagAndIsEmptyForALeaf) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        EXPECT_EQ(tree.DescendantRange(IdOf(tree, "match")), (TagRange{1, 3}));
        EXPECT_TRUE(tree.DescendantRange(IdOf(tree, "cover")).IsEmpty());
        EXPECT_TRUE(tree.DescendantRange(IdOf(tree, "match.adjacent")).IsEmpty());
    }

    TEST(BakedTagTreeTest, ParentsAreImplied) {
        const BakedTagTree tree = BakeOf({"booster.hammer"});

        ASSERT_EQ(tree.Count(), 2u);
        EXPECT_EQ(tree.Name(0), "booster");
        EXPECT_EQ(tree.Name(1), "booster.hammer");
        EXPECT_EQ(tree.Parent(1), 0);
    }

    TEST(BakedTagTreeTest, DeclaringTheParentAfterTheChildIsNotADuplicate) {
        const BakedTagTree tree = BakeOf({"match.adjacent", "match"});

        ASSERT_EQ(tree.Count(), 2u);
        EXPECT_EQ(IdOf(tree, "match"), 0);
        EXPECT_EQ(IdOf(tree, "match.adjacent"), 1);
    }

    TEST(BakedTagTreeTest, IdsFollowTheOrderOfFirstMention) {
        const BakedTagTree tree = BakeOf({"b.x", "a", "b.y"});

        EXPECT_EQ(IdOf(tree, "b"), 0);
        EXPECT_EQ(IdOf(tree, "b.x"), 1);
        EXPECT_EQ(IdOf(tree, "b.y"), 2);
        EXPECT_EQ(IdOf(tree, "a"), 3);
    }

    TEST(BakedTagTreeTest, TheSameInputGivesByteEqualBlocksAndAnotherOrderGivesOtherIds) {
        const BakedTagTree a = BakeOf({"x.one", "y", "x.two"});
        const BakedTagTree b = BakeOf({"x.one", "y", "x.two"});
        const BakedTagTree c = BakeOf({"y", "x.one", "x.two"});

        EXPECT_EQ(memcmp(&a, &b, sizeof(BakedTagTree)), 0);
        EXPECT_NE(memcmp(&a, &c, sizeof(BakedTagTree)), 0);
        EXPECT_NE(IdOf(a, "y"), IdOf(c, "y"));
    }

    TEST(BakedTagTreeTest, EveryByteOfTheBlockIsDefinedAndFillingIntoADirtyBlockGivesTheSameBytes) {
        TagTree tree;
        ASSERT_TRUE(tree.Add("x.one").has_value());
        ASSERT_TRUE(tree.Add("y").has_value());
        tree.Bake();

        BakedTagTree clean{};
        tree.FillBaked(clean);
        BakedTagTree dirty;
        memset(&dirty, 0xAB, sizeof(BakedTagTree));
        tree.FillBaked(dirty);
        EXPECT_EQ(memcmp(&clean, &dirty, sizeof(BakedTagTree)), 0);

        for (size_t i = clean.count; i < kMaxTagCount; ++i) {
            ASSERT_EQ(clean.parent[i], 0) << i;
            ASSERT_EQ(clean.subtreeEnd[i], 0) << i;
            ASSERT_EQ(clean.nameOffset[i], 0) << i;
        }
        for (size_t i = clean.nameBlobSize; i < kMaxTagNameBlob; ++i) {
            ASSERT_EQ(clean.names[i], 0) << i;
        }
    }

    TEST(BakedTagTreeTest, ACopyByMemcpyAnswersTheSame) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);
        BakedTagTree copy{};
        memcpy(&copy, &tree, sizeof(BakedTagTree));

        EXPECT_EQ(memcmp(&copy, &tree, sizeof(BakedTagTree)), 0);
        EXPECT_EQ(copy.Name(IdOf(tree, "special.rocket")), "special.rocket");
        EXPECT_EQ(RangeOf(copy, "booster.*"), RangeOf(tree, "booster.*"));
    }

    TEST(BakedTagTreeTest, AnEmptyTreeBakesToAnEmptyBlock) {
        const BakedTagTree tree = BakeOf({});

        EXPECT_EQ(tree.Count(), 0u);
        EXPECT_EQ(tree.nameBlobSize, 0);
        EXPECT_TRUE(tree.Name(0).empty());
        EXPECT_EQ(tree.Parent(0), kNoTag);
        EXPECT_TRUE(tree.Range(0).IsEmpty());
        EXPECT_FALSE(tree.Find("a").has_value());
        const BakedTagTree zero{};
        EXPECT_EQ(memcmp(&tree, &zero, sizeof(BakedTagTree)), 0);
    }

    TEST(BakedTagTreeTest, DepthOfACorruptParentCycleEndsPastTheLimit) {
        BakedTagTree tree = BakeOf({"a", "b"});
        tree.parent[0] = 1;
        tree.parent[1] = 0;
        EXPECT_GT(tree.Depth(0), kMaxTagDepth);
    }

    TEST(BakedTagTreeTest, ALongestPathFillsTheDeepestAndLongestSlot) {
        const String longest(kMaxTagNameLength, 'x');
        const String deepest = "a.b.c.d.e.f.g.h";
        const BakedTagTree tree = BakeOf({longest, deepest});

        EXPECT_EQ(tree.Count(), 1u + kMaxTagDepth);
        EXPECT_EQ(tree.Name(IdOf(tree, longest)), longest);
        EXPECT_EQ(tree.Depth(IdOf(tree, deepest)), kMaxTagDepth);
        EXPECT_EQ(tree.Name(IdOf(tree, deepest)), deepest);
    }

    TEST(BakedTagTreeTest, AFullTreeOfMaxTagCountTagsFillsTheBlock) {
        TagTree tree;
        for (size_t i = 0; i < kMaxTagCount; ++i) {
            ASSERT_TRUE(tree.Add(Format("t{}", i)).has_value()) << i;
        }
        tree.Bake();
        BakedTagTree block{};
        tree.FillBaked(block);

        ASSERT_EQ(block.Count(), kMaxTagCount);
        for (size_t i = 0; i < kMaxTagCount; ++i) {
            const TagId id = static_cast<TagId>(i);
            ASSERT_EQ(block.Name(id), tree.Name(id)) << i;
            ASSERT_EQ(block.Find(Format("t{}", i)), id) << i;
            ASSERT_EQ(block.Range(id), (TagRange{id, id})) << i;
        }
        EXPECT_TRUE(block.Name(static_cast<TagId>(kMaxTagCount)).empty());
    }

    TEST(BakedTagTreeTest, ANameBlobAtItsLimitFillsTheBlob) {
        // Names of the longest length take kMaxTagNameLength + 1 bytes each; one short name takes the rest exactly.
        constexpr size_t kFit = kMaxTagNameBlob / (kMaxTagNameLength + 1);
        constexpr size_t kRest = kMaxTagNameBlob - kFit * (kMaxTagNameLength + 1);
        TagTree tree;
        for (size_t i = 0; i < kFit; ++i) {
            ASSERT_TRUE(tree.Add(Format("t{:063}", i)).has_value()) << i;
        }
        const String last(kRest - 1, 'z');
        ASSERT_TRUE(tree.Add(last).has_value());
        tree.Bake();
        BakedTagTree block{};
        tree.FillBaked(block);

        EXPECT_EQ(block.nameBlobSize, kMaxTagNameBlob);
        EXPECT_EQ(block.names[kMaxTagNameBlob - 1], '\0');
        ASSERT_EQ(block.Count(), kFit + 1);
        EXPECT_EQ(block.Name(static_cast<TagId>(kFit)), last);
        EXPECT_EQ(block.Name(static_cast<TagId>(kFit - 1)), Format("t{:063}", kFit - 1));
        EXPECT_EQ(block.Find(last), static_cast<TagId>(kFit));
    }

    TEST(BakedTagTreeTest, ABlockAnswersEveryQuestionLikeItsTree) {
        TagTree tree;
        for (const StringView path : kReactionVocabulary) {
            ASSERT_TRUE(tree.Add(path).has_value());
        }
        ASSERT_TRUE(tree.Add("a.b.c.d").has_value());
        tree.Bake();
        BakedTagTree block{};
        tree.FillBaked(block);

        ASSERT_EQ(block.Count(), tree.Count());
        for (size_t i = 0; i < tree.Count(); ++i) {
            const TagId id = static_cast<TagId>(i);
            EXPECT_EQ(block.Name(id), tree.Name(id));
            EXPECT_EQ(block.Parent(id), tree.Parent(id));
            EXPECT_EQ(block.Range(id), tree.Range(id));
            EXPECT_EQ(block.DescendantRange(id), tree.DescendantRange(id));
            EXPECT_EQ(block.Depth(id), tree.Depth(id));
            EXPECT_EQ(block.Find(tree.Name(id)), tree.Find(tree.Name(id)));
            EXPECT_EQ(block.ResolveTag(tree.Name(id)), tree.ResolveTag(tree.Name(id)));
            EXPECT_EQ(block.Resolve(Format("{}.*", tree.Name(id))), tree.Resolve(Format("{}.*", tree.Name(id))));
        }
        EXPECT_EQ(block.Resolve("*"), tree.Resolve("*"));
        EXPECT_EQ(block.Find("nope"), tree.Find("nope"));
    }

    TEST(BakedTagTreeTest, ResolveOfAPlainNameIsOneId) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        EXPECT_EQ(RangeOf(tree, "match"), (TagRange{0, 0}));
        const TagId id = IdOf(tree, "match.direct.long");
        EXPECT_EQ(RangeOf(tree, "match.direct.long"), (TagRange{id, id}));
    }

    TEST(BakedTagTreeTest, AWildcardSuffixIsTheSubtreeIncludingTheTag) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        const TagRange range = RangeOf(tree, "match.*");
        EXPECT_EQ(range.lo, IdOf(tree, "match"));
        EXPECT_EQ(range.hi, IdOf(tree, "match.direct.long"));
        EXPECT_TRUE(range.Contains(IdOf(tree, "match")));
        EXPECT_FALSE(range.Contains(IdOf(tree, "booster")));
        EXPECT_EQ(RangeOf(tree, "match.direct.*"), (TagRange{IdOf(tree, "match.direct"), IdOf(tree, "match.direct.long")}));
    }

    TEST(BakedTagTreeTest, ALeafWildcardIsTheLeafAlone) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);
        const TagId cover = IdOf(tree, "cover");

        EXPECT_EQ(RangeOf(tree, "cover.*"), (TagRange{cover, cover}));
    }

    TEST(BakedTagTreeTest, StarIsEveryTagAndAnEmptyTreeGivesAnEmptyRange) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);
        EXPECT_EQ(RangeOf(tree, "*"), (TagRange{0, static_cast<TagId>(tree.Count() - 1)}));

        const BakedTagTree empty = BakeOf({});
        const TagRange none = RangeOf(empty, "*");
        EXPECT_TRUE(none.IsEmpty());
        for (const TagId id : {TagId{0}, TagId{1}, kNoTag}) {
            EXPECT_FALSE(none.Contains(id));
        }
    }

    TEST(BakedTagTreeTest, AReactionRowForMatchWildcardAnswersOnlyForTheMatchSubtree) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);
        const TagRange row = RangeOf(tree, "match.*");

        for (TagId id = 0; id < tree.Count(); ++id) {
            const String name = Text(tree.Name(id));
            const bool inMatch = name == "match" || StringView(name).starts_with("match.");
            EXPECT_EQ(row.Contains(id), inMatch) << ToStd(name);
        }
    }

    TEST(BakedTagTreeTest, EveryResolveErrorNamesThePattern) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        for (const StringView pattern : {"nope", "nope.*", "match.nope", "match.nope.*", "", "a..b", "match..direct", ".match", "match.", "a.*.b", "a*", "a.**", "*.match", "match.*.*"}) {
            const auto range = tree.Resolve(pattern);
            ASSERT_FALSE(range.has_value()) << ToStd(pattern);
            EXPECT_TRUE(Mentions(range.error(), Format("'{}'", pattern))) << ToStd(range.error());
        }
    }

    TEST(BakedTagTreeTest, ResolveTagRefusesWildcardsAndUnknownNames) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        const auto rocket = tree.ResolveTag("special.rocket");
        ASSERT_TRUE(rocket.has_value());
        EXPECT_EQ(tree.Name(*rocket), "special.rocket");
        for (const StringView path : {"special.*", "*", "special.nope", ""}) {
            const auto result = tree.ResolveTag(path);
            ASSERT_FALSE(result.has_value()) << ToStd(path);
            EXPECT_FALSE(result.error().empty());
        }
        EXPECT_TRUE(Mentions(tree.ResolveTag("special.*").error(), "special.*"));
    }

    TEST(BakedTagTreeTest, ParentChainEndsAtNoTag) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        TagId id = IdOf(tree, "match.direct.long");
        Vector<String> chain;
        while (id != kNoTag) {
            chain.push_back(Text(tree.Name(id)));
            id = tree.Parent(id);
        }
        EXPECT_EQ(chain, (Vector<String>{"match.direct.long", "match.direct", "match"}));
    }

    TEST(BakedTagTreeTest, NameRoundTripsThroughResolve) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);

        for (TagId id = 0; id < tree.Count(); ++id) {
            EXPECT_EQ(RangeOf(tree, tree.Name(id)), (TagRange{id, id}));
            EXPECT_EQ(tree.Find(tree.Name(id)), id);
        }
    }

    TEST(BakedTagTreeTest, AnIdThatIsNotATagGivesNoTagAnEmptyNameAndAnEmptyRange) {
        const BakedTagTree tree = BakeOf(kReactionVocabulary);
        const TagId past = static_cast<TagId>(tree.Count());

        for (const TagId id : {past, kNoTag}) {
            EXPECT_EQ(tree.Parent(id), kNoTag);
            EXPECT_TRUE(tree.Name(id).empty());
            EXPECT_TRUE(tree.Range(id).IsEmpty());
            EXPECT_TRUE(tree.DescendantRange(id).IsEmpty());
            EXPECT_EQ(tree.Depth(id), 0u);
        }
    }

}  // namespace BECore::Tests
