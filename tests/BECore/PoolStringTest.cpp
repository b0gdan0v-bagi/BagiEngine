#include <BECore/PoolString/PoolStringChain.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(PoolStringChain("test").View() == "test");
    static_assert(PoolStringChain("").Empty());
    static_assert(PoolStringChain("hello").Size() == 5);
    static_assert(!PoolStringChain("abc").Empty());
    static_assert(PoolStringChain("ab").Size() + PoolStringChain("cd").Size() == 4);

    TEST(PoolStringTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    TEST(PoolStringTest, Literal) {
        PoolStringChain lit("literal");
        EXPECT_EQ(lit.View(), "literal");
        EXPECT_EQ(lit.Size(), 7u);
        EXPECT_FALSE(lit.IsSingle());
    }

    TEST(PoolStringTest, Atom) {
        PoolString ps = PoolString::Intern("atom");
        PoolStringChain atom(ps);
        EXPECT_EQ(atom.View(), "atom");
        EXPECT_TRUE(atom.IsSingle());
        EXPECT_EQ(atom.Size(), 4u);
    }

    TEST(PoolStringTest, Concat) {
        PoolString ps = PoolString::Intern("atom");
        PoolStringChain chain = PoolStringChain::Concat(ps, 42);
        EXPECT_EQ(chain.Size(), 6u);
        EXPECT_EQ(chain.Materialize(), "atom42");
    }

    TEST(PoolStringTest, AppendAndAutoMaterialization) {
        PoolStringChain chain = PoolStringChain::Concat(PoolString::Intern("atom"), 42);

        chain += PoolString::Intern("_suffix");
        EXPECT_EQ(chain.Size(), 13u);
        EXPECT_EQ(chain.Materialize(), "atom42_suffix");

        chain += 777;
        EXPECT_EQ(chain.Materialize(), "atom42_suffix777");
        EXPECT_EQ(chain.Size(), 16u);
    }

    TEST(PoolStringTest, Equality) {
        PoolStringChain a("same");
        PoolStringChain b("same");
        EXPECT_TRUE(a == b);

        PoolStringChain c = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        PoolStringChain d = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        EXPECT_TRUE(c == d);
        EXPECT_TRUE(c != a);
    }

    TEST(PoolStringTest, Hash) {
        PoolStringChain a("same");
        PoolStringChain b("same");
        EXPECT_EQ(a.GetHash(), b.GetHash());

        PoolStringChain c = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        PoolStringChain d = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        EXPECT_EQ(c.GetHash(), d.GetHash());
    }

}  // namespace BECore::Tests
