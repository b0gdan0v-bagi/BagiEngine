#include <BECore/Rules/TagTree.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        TagTree MakeBaked(std::initializer_list<StringView> paths) {
            TagTree tree;
            for (StringView path : paths) {
                EXPECT_TRUE(tree.Add(path).has_value()) << ToStd(path);
            }
            tree.Bake();
            return tree;
        }

        /// The error text of a failed result, empty for a success.
        template <class T>
        String ErrorOf(const std::expected<T, String>& result) {
            return result.has_value() ? String{} : result.error();
        }

        bool Mentions(const String& text, StringView part) {
            return text.find(String(part)) != String::npos;
        }

        /// "prefix" is a path prefix of "path" at a dot boundary (or the same path).
        bool IsPathPrefix(StringView prefix, StringView path) {
            if (path.size() < prefix.size() || path.substr(0, prefix.size()) != prefix) {
                return false;
            }
            return path.size() == prefix.size() || path[prefix.size()] == '.';
        }

    }  // namespace

    TEST(TagTreeTest, IdsArePreorderInOrderOfFirstAdd) {
        const TagTree tree = MakeBaked({"a.x", "b", "a.y"});
        EXPECT_EQ(tree.Count(), 4u);
        EXPECT_EQ(tree.Find("a"), TagId{0});
        EXPECT_EQ(tree.Find("a.x"), TagId{1});
        EXPECT_EQ(tree.Find("a.y"), TagId{2});
        EXPECT_EQ(tree.Find("b"), TagId{3});
    }

    TEST(TagTreeTest, RangeCoversTheSubtree) {
        const TagTree tree = MakeBaked({"a.x", "b", "a.y"});
        EXPECT_EQ(tree.Range(0), (TagRange{0, 2}));
        EXPECT_EQ(tree.Range(1), (TagRange{1, 1}));
        EXPECT_EQ(tree.Range(3), (TagRange{3, 3}));
        EXPECT_EQ(tree.DescendantRange(0), (TagRange{1, 2}));
        EXPECT_TRUE(tree.DescendantRange(1).IsEmpty());
        EXPECT_TRUE(tree.DescendantRange(3).IsEmpty());
        EXPECT_FALSE(tree.DescendantRange(3).Contains(3));
        EXPECT_TRUE(tree.Range(0).Contains(2));
        EXPECT_FALSE(tree.Range(0).Contains(3));
    }

    TEST(TagTreeTest, EverySubtreeIsOneContiguousRange) {
        const StringView paths[] = {"match.3",
                                    "match.4.line",
                                    "match.4.l",
                                    "match.5",
                                    "special.bomb",
                                    "special.rocket.h",
                                    "special.rocket.v",
                                    "special",
                                    "special2",
                                    "specialist.x",
                                    "blocker.stone",
                                    "blocker.ice",
                                    "blocker",
                                    "a.b.c.d.e",
                                    "a.b.c.f",
                                    "a.b.g",
                                    "a.h",
                                    "z",
                                    "m.n",
                                    "m.o.p",
                                    "m.o.q",
                                    "m.o",
                                    "k",
                                    "k.k",
                                    "k.k.k",
                                    "match",
                                    "q.r",
                                    "q.r.s",
                                    "q.t",
                                    "u"};
        TagTree tree;
        for (StringView path : paths) {
            ASSERT_TRUE(tree.Add(path).has_value());
        }
        tree.Bake();
        ASSERT_GE(tree.Count(), 30u);

        for (size_t a = 0; a < tree.Count(); ++a) {
            const TagId tag = static_cast<TagId>(a);
            const TagRange range = tree.Range(tag);
            EXPECT_EQ(range.lo, tag);
            for (size_t b = 0; b < tree.Count(); ++b) {
                const TagId other = static_cast<TagId>(b);
                EXPECT_EQ(range.Contains(other), IsPathPrefix(tree.Name(tag), tree.Name(other))) << ToStd(tree.Name(tag)) << " vs " << ToStd(tree.Name(other));
                EXPECT_EQ(tree.DescendantRange(tag).Contains(other), other != tag && IsPathPrefix(tree.Name(tag), tree.Name(other)));
            }
        }
    }

    TEST(TagTreeTest, DepthAndName) {
        const TagTree tree = MakeBaked({"match.3.line", "special"});
        EXPECT_EQ(tree.Depth(*tree.Find("match")), 1u);
        EXPECT_EQ(tree.Depth(*tree.Find("match.3")), 2u);
        EXPECT_EQ(tree.Depth(*tree.Find("match.3.line")), 3u);
        EXPECT_EQ(tree.Name(*tree.Find("match.3")), "match.3");
        EXPECT_EQ(tree.Name(*tree.Find("special")), "special");
    }

    TEST(TagTreeTest, FindOfAnUnknownPathIsEmpty) {
        const TagTree tree = MakeBaked({"match.3"});
        EXPECT_FALSE(tree.Find("match.4").has_value());
        EXPECT_FALSE(tree.Find("mat").has_value());
        EXPECT_FALSE(tree.Find("match.").has_value());
        EXPECT_FALSE(tree.Find("").has_value());
        EXPECT_FALSE(tree.Find("match.3.x").has_value());
    }

    TEST(TagTreeTest, DeepAddCreatesTheAncestors) {
        const TagTree tree = MakeBaked({"a.b.c"});
        EXPECT_EQ(tree.Count(), 3u);
        EXPECT_EQ(tree.Find("a"), TagId{0});
        EXPECT_EQ(tree.Find("a.b"), TagId{1});
        EXPECT_EQ(tree.Find("a.b.c"), TagId{2});
    }

    TEST(TagTreeTest, AddTwiceIsTheSameTag) {
        TagTree tree;
        ASSERT_TRUE(tree.Add("a.b").has_value());
        ASSERT_TRUE(tree.Add("c").has_value());
        ASSERT_TRUE(tree.Add("a.b").has_value());
        ASSERT_TRUE(tree.Add("a").has_value());
        EXPECT_EQ(tree.Count(), 3u);
        tree.Bake();
        EXPECT_EQ(tree.Find("a"), TagId{0});
        EXPECT_EQ(tree.Find("a.b"), TagId{1});
        EXPECT_EQ(tree.Find("c"), TagId{2});
    }

    TEST(TagTreeTest, BadPathsAreErrorsAndLeaveTheTreeUnchanged) {
        TagTree tree;
        ASSERT_TRUE(tree.Add("a.b").has_value());
        const StringView bad[] = {"", "a..b", ".a", "a.", ".", "x.y..z", "x.*", "x y", "x\t.y", "*"};
        for (StringView path : bad) {
            const auto result = tree.Add(path);
            EXPECT_FALSE(result.has_value()) << "'" << ToStd(path) << "'";
            if (!result.has_value()) {
                EXPECT_FALSE(result.error().empty());
            }
        }
        EXPECT_EQ(tree.Count(), 2u);
        tree.Bake();
        EXPECT_EQ(tree.Count(), 2u);
        EXPECT_FALSE(tree.Find("x").has_value());
    }

    TEST(TagTreeTest, ErrorNamesThePath) {
        TagTree tree;
        const auto result = tree.Add("a..b");
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(Mentions(result.error(), "a..b"));
    }

    TEST(TagTreeTest, ACopyOfABakedTreeAnswersTheSame) {
        const TagTree original = MakeBaked({"a.x", "b", "a.y"});
        const TagTree copy = original;
        EXPECT_TRUE(copy.IsBaked());
        EXPECT_EQ(copy.Count(), original.Count());
        for (size_t i = 0; i < original.Count(); ++i) {
            const TagId tag = static_cast<TagId>(i);
            EXPECT_EQ(copy.Range(tag), original.Range(tag));
            EXPECT_EQ(copy.Name(tag), original.Name(tag));
            EXPECT_EQ(copy.Depth(tag), original.Depth(tag));
            EXPECT_EQ(copy.Find(original.Name(tag)), tag);
        }
    }

    TEST(TagTreeTest, BakeTwiceChangesNothing) {
        TagTree tree = MakeBaked({"a.x", "b"});
        tree.Bake();
        EXPECT_EQ(tree.Find("b"), TagId{2});
        EXPECT_EQ(tree.Range(0), (TagRange{0, 1}));
    }

    TEST(TagTreeTest, TooManyTagsIsAnError) {
        TagTree tree;
        for (size_t i = 0; i < kMaxTagCount; ++i) {
            ASSERT_TRUE(tree.Add(Format("t{}", i)).has_value()) << i;
        }
        EXPECT_EQ(tree.Count(), kMaxTagCount);
        EXPECT_TRUE(tree.Add("t0").has_value());

        const auto overflow = tree.Add(Format("t{}", kMaxTagCount));
        EXPECT_FALSE(overflow.has_value());
        // A deep path whose ancestors are new counts every new ancestor.
        EXPECT_FALSE(tree.Add("t0.child").has_value());
        EXPECT_EQ(tree.Count(), kMaxTagCount);

        tree.Bake();
        EXPECT_EQ(tree.Find(Format("t{}", kMaxTagCount - 1)), static_cast<TagId>(kMaxTagCount - 1));
        EXPECT_EQ(tree.Range(static_cast<TagId>(kMaxTagCount - 1)), (TagRange{static_cast<TagId>(kMaxTagCount - 1), static_cast<TagId>(kMaxTagCount - 1)}));
        EXPECT_TRUE(tree.DescendantRange(static_cast<TagId>(kMaxTagCount - 1)).IsEmpty());
    }

    TEST(TagTreeTest, TooManyTagsByDeepPathLeavesTheTreeUnchanged) {
        TagTree tree;
        for (size_t i = 0; i < kMaxTagCount - 1; ++i) {
            ASSERT_TRUE(tree.Add(Format("t{}", i)).has_value());
        }
        // Two new tags (`x` and `x.y`) do not fit into the one free place: nothing is created.
        EXPECT_FALSE(tree.Add("x.y").has_value());
        EXPECT_EQ(tree.Count(), kMaxTagCount - 1);
        EXPECT_TRUE(tree.Add("x").has_value());
        EXPECT_EQ(tree.Count(), kMaxTagCount);
    }

    TEST(TagTreeTest, ParentOfARootIsNoTag) {
        const TagTree tree = MakeBaked({"a.x", "b", "a.y"});
        EXPECT_EQ(tree.Parent(*tree.Find("a")), kNoTag);
        EXPECT_EQ(tree.Parent(*tree.Find("b")), kNoTag);
        EXPECT_EQ(tree.Parent(*tree.Find("a.x")), *tree.Find("a"));
        EXPECT_EQ(tree.Parent(*tree.Find("a.y")), *tree.Find("a"));
    }

    TEST(TagTreeTest, TheParentChainOfADeepTagEndsAtNoTag) {
        const TagTree tree = MakeBaked({"a.b.c.d"});
        TagId tag = *tree.Find("a.b.c.d");
        size_t steps = 0;
        while (tree.Parent(tag) != kNoTag) {
            tag = tree.Parent(tag);
            ++steps;
        }
        EXPECT_EQ(steps, 3u);
        EXPECT_EQ(tree.Name(tag), "a");
    }

    TEST(TagTreeTest, ParentAgreesWithRangeAndDepth) {
        const TagTree tree = MakeBaked({"a.b.c", "a.d", "e", "e.f.g", "h", "a.b.i"});
        for (size_t i = 0; i < tree.Count(); ++i) {
            const TagId tag = static_cast<TagId>(i);
            const TagId parent = tree.Parent(tag);
            if (parent == kNoTag) {
                EXPECT_EQ(tree.Depth(tag), 1u);
                continue;
            }
            EXPECT_LT(parent, tag);
            EXPECT_EQ(tree.Depth(tag), tree.Depth(parent) + 1);
            EXPECT_TRUE(tree.DescendantRange(parent).Contains(tag));
            EXPECT_TRUE(tree.Range(parent).Contains(tree.Range(tag).hi));
        }
    }

    TEST(TagTreeTest, TheLimitsAreInclusive) {
        TagTree tree;
        EXPECT_TRUE(tree.Add("a.b.c.d.e.f.g.h").has_value());  // kMaxTagDepth segments
        EXPECT_TRUE(tree.Add(String(kMaxTagNameLength, 'n')).has_value());
        tree.Bake();
        EXPECT_EQ(tree.Depth(*tree.Find("a.b.c.d.e.f.g.h")), kMaxTagDepth);
        EXPECT_TRUE(tree.Find(String(kMaxTagNameLength, 'n')).has_value());
    }

    TEST(TagTreeTest, TooDeepAndTooLongNamesAreErrorsThatNameTheTag) {
        TagTree tree;
        const String tooDeep = "a.b.c.d.e.f.g.h.i";
        const auto deep = tree.Add(tooDeep);
        ASSERT_FALSE(deep.has_value());
        EXPECT_TRUE(Mentions(deep.error(), tooDeep));
        EXPECT_TRUE(Mentions(deep.error(), Format("{}", kMaxTagDepth)));

        const String tooLong(kMaxTagNameLength + 1, 'n');
        const auto longName = tree.Add(tooLong);
        ASSERT_FALSE(longName.has_value());
        EXPECT_TRUE(Mentions(longName.error(), tooLong));
        EXPECT_TRUE(Mentions(longName.error(), Format("{}", kMaxTagNameLength)));
        EXPECT_EQ(tree.Count(), 0u);
    }

    TEST(TagTreeTest, SegmentsAreLettersDigitsAndUnderscores) {
        TagTree tree;
        EXPECT_TRUE(tree.Add("Match_3.line2").has_value());
        EXPECT_TRUE(tree.Add("_0.9").has_value());

        const StringView bad[] = {"a-b", "a@b", "a*b", "a b", "a.b-c", "a/b", "caf\xC3\xA9"};
        for (StringView path : bad) {
            const auto result = tree.Add(path);
            ASSERT_FALSE(result.has_value()) << ToStd(path);
            EXPECT_TRUE(Mentions(result.error(), path)) << ToStd(result.error());
        }
        EXPECT_EQ(tree.Count(), 4u);
    }

    TEST(TagTreeTest, EveryAddErrorNamesTheTag) {
        TagTree tree;
        const StringView bad[] = {"", "a..b", ".a", "a.", "x y", "x.*", "*", "x-y"};
        for (StringView path : bad) {
            const auto result = tree.Add(path);
            ASSERT_FALSE(result.has_value());
            EXPECT_TRUE(Mentions(result.error(), Format("'{}'", path))) << ToStd(result.error());
        }

        TagTree full;
        for (size_t i = 0; i < kMaxTagCount; ++i) {
            ASSERT_TRUE(full.Add(Format("t{}", i)).has_value());
        }
        const auto overflow = full.Add("another");
        ASSERT_FALSE(overflow.has_value());
        EXPECT_TRUE(Mentions(overflow.error(), "another"));
        EXPECT_TRUE(Mentions(overflow.error(), Format("{}", kMaxTagCount)));
    }

    TEST(TagTreeTest, TheNameBlobHasALimit) {
        // Names of the longest length: each takes kMaxTagNameLength + 1 bytes with its NUL.
        constexpr size_t kFit = kMaxTagNameBlob / (kMaxTagNameLength + 1);
        TagTree tree;
        for (size_t i = 0; i < kFit; ++i) {
            ASSERT_TRUE(tree.Add(Format("t{:063}", i)).has_value()) << i;
        }
        ASSERT_EQ(tree.Count(), kFit);

        const String doesNotFit = Format("t{:063}", kFit);
        const auto overflow = tree.Add(doesNotFit);
        ASSERT_FALSE(overflow.has_value());
        EXPECT_TRUE(Mentions(overflow.error(), doesNotFit));
        EXPECT_TRUE(Mentions(overflow.error(), Format("{}", kMaxTagNameBlob)));
        EXPECT_EQ(tree.Count(), kFit);

        // The rest of the blob is exactly kMaxTagNameBlob - kFit * 65 bytes: a failed Add took nothing, and a name that fits exactly does.
        constexpr size_t kRest = kMaxTagNameBlob - kFit * (kMaxTagNameLength + 1);
        const String tooBig(kRest, 'z');  // kRest characters and a NUL: one byte over
        EXPECT_FALSE(tree.Add(tooBig).has_value());
        const String exact(kRest - 1, 'z');
        EXPECT_TRUE(tree.Add(exact).has_value());
        EXPECT_FALSE(tree.Add("y").has_value());
    }

    TEST(TagTreeTest, ImpliedAncestorsCountTowardsTheNameBlob) {
        constexpr size_t kFit = kMaxTagNameBlob / (kMaxTagNameLength + 1);
        TagTree tree;
        for (size_t i = 0; i + 1 < kFit; ++i) {
            ASSERT_TRUE(tree.Add(Format("t{:063}", i)).has_value()) << i;
        }
        // One name of the longest length still fits (65 bytes of the 67 left); with its parent `pp` (3 bytes) it does not.
        EXPECT_FALSE(tree.Add(String("pp.") + String(kMaxTagNameLength - 3, 'a')).has_value());
        EXPECT_EQ(tree.Count(), kFit - 1);
        EXPECT_TRUE(tree.Add(String(kMaxTagNameLength, 'a')).has_value());
    }

    TEST(TagTreeTest, ResolveOfAPlainNameIsOneId) {
        const TagTree tree = MakeBaked({"a.x", "a.y", "b"});
        EXPECT_EQ(tree.Resolve("a.y"), (TagRange{2, 2}));
        EXPECT_EQ(tree.Resolve("a"), (TagRange{0, 0}));
        EXPECT_EQ(tree.Resolve("b"), (TagRange{3, 3}));
    }

    TEST(TagTreeTest, AWildcardSuffixIncludesTheTagItself) {
        const TagTree tree = MakeBaked({"a.x", "a.y", "b", "a.x.deep"});
        const auto a = tree.Resolve("a.*");
        ASSERT_TRUE(a.has_value());
        EXPECT_EQ(*a, tree.Range(*tree.Find("a")));
        EXPECT_TRUE(a->Contains(*tree.Find("a")));
        EXPECT_TRUE(a->Contains(*tree.Find("a.x.deep")));
        EXPECT_FALSE(a->Contains(*tree.Find("b")));
        EXPECT_EQ(tree.Resolve("a.x.*"), (TagRange{*tree.Find("a.x"), *tree.Find("a.x.deep")}));
    }

    TEST(TagTreeTest, ALeafWildcardIsTheLeafAlone) {
        const TagTree tree = MakeBaked({"a.x", "b"});
        const TagId leaf = *tree.Find("a.x");
        EXPECT_EQ(tree.Resolve("a.x.*"), (TagRange{leaf, leaf}));
        EXPECT_EQ(tree.Resolve("b.*"), (TagRange{2, 2}));
    }

    TEST(TagTreeTest, AStarIsEveryTag) {
        const TagTree tree = MakeBaked({"a.x", "b", "a.y"});
        EXPECT_EQ(tree.Resolve("*"), (TagRange{0, 3}));

        const TagTree empty = MakeBaked({});
        const auto none = empty.Resolve("*");
        ASSERT_TRUE(none.has_value());
        EXPECT_TRUE(none->IsEmpty());
        EXPECT_FALSE(none->Contains(0));
    }

    TEST(TagTreeTest, ResolveTreatsASubtreeAsIncludingTheTagUnlikeDescendantRange) {
        const TagTree tree = MakeBaked({"match.3", "match.4", "special"});
        const TagId match = *tree.Find("match");
        EXPECT_EQ(tree.Resolve("match.*"), tree.Range(match));
        EXPECT_NE(tree.Resolve("match.*"), tree.DescendantRange(match));
    }

    TEST(TagTreeTest, EveryResolveErrorNamesThePattern) {
        const TagTree tree = MakeBaked({"a.x", "b"});
        const StringView bad[] = {"nope", "nope.*", "a.nope", "a.nope.*", "", ".*", "a..b", "a.*.b", "a*", "a.**", "*.x", "*.*", "a b", "a.x-y"};
        for (StringView pattern : bad) {
            const auto result = tree.Resolve(pattern);
            ASSERT_FALSE(result.has_value()) << "'" << ToStd(pattern) << "'";
            EXPECT_TRUE(Mentions(result.error(), Format("'{}'", pattern))) << ToStd(result.error());
        }
        EXPECT_TRUE(Mentions(ErrorOf(tree.Resolve("nope.*")), "nope"));
    }

    TEST(TagTreeTest, ResolveTagRefusesWildcardsAndUnknownNames) {
        const TagTree tree = MakeBaked({"a.x", "b"});
        const auto ax = tree.ResolveTag("a.x");
        ASSERT_TRUE(ax.has_value());
        EXPECT_EQ(*ax, *tree.Find("a.x"));
        const auto b = tree.ResolveTag("b");
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(*b, TagId{2});
        for (StringView path : {"*", "a.*", "nope", "", "a..x", "a b"}) {
            const auto result = tree.ResolveTag(path);
            ASSERT_FALSE(result.has_value()) << "'" << ToStd(path) << "'";
            EXPECT_TRUE(Mentions(result.error(), Format("'{}'", path))) << ToStd(result.error());
        }
    }

    TEST(TagTreeTest, CompileTimeChecks) {
        static_assert(sizeof(TagId) == 2);
        static_assert(TagRange{}.IsEmpty());
        static_assert(TagRange{2, 5}.Contains(5));
        static_assert(!TagRange{2, 5}.Contains(6));
    }

}  // namespace BECore::Tests
