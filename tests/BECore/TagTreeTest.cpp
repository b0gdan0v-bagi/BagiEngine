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
        EXPECT_NE(result.error().find("a..b"), String::npos);
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
        EXPECT_EQ(tree.Range(static_cast<TagId>(kMaxTagCount - 1)), (TagRange{65534, 65534}));
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

    TEST(TagTreeTest, CompileTimeChecks) {
        static_assert(sizeof(TagId) == 2);
        static_assert(TagRange{}.IsEmpty());
        static_assert(TagRange{2, 5}.Contains(5));
        static_assert(!TagRange{2, 5}.Contains(6));
    }

}  // namespace BECore::Tests
