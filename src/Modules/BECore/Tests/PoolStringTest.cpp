#include "PoolStringTest.h"

#include <BECore/Logger/Logger.h>

namespace BECore::Tests {

    const char* PoolStringTest::GetName() const {
        return "PoolStringTest";
    }

    bool PoolStringTest::Run() {
        // --- Compile-time tests ---
        static_assert(PoolStringChain("test").View() == "test");
        static_assert(PoolStringChain("").Empty());
        static_assert(PoolStringChain("hello").Size() == 5);

        LOG_INFO("[PoolStringTest] Starting runtime tests...");

        // --- Runtime tests ---

        // 1. Literal test
        PoolStringChain lit("literal");
        ASSERT(lit.View() == "literal");
        ASSERT(lit.Size() == 7);
        ASSERT(!lit.IsSingle());

        // 2. Atom test
        PoolString ps1 = PoolString::Intern("atom");
        PoolStringChain atom(ps1);
        ASSERT(atom.View() == "atom");
        ASSERT(atom.IsSingle());
        ASSERT(atom.Size() == 4);

        // 3. Concat test
        PoolStringChain chain = PoolStringChain::Concat(ps1, 42);
        ASSERT(chain.Size() == 6);
        ASSERT(chain.Materialize() == "atom42");

        // 4. operator+= tests
        chain += PoolString::Intern("_suffix");
        ASSERT(chain.Size() == 13);
        ASSERT(chain.Materialize() == "atom42_suffix");

        // 5. Auto-materialization test
        chain += 777;
        ASSERT(chain.Materialize() == "atom42_suffix777");
        ASSERT(chain.Size() == 16);

        // 6. Equality tests
        PoolStringChain a("same");
        PoolStringChain b("same");
        ASSERT(a == b);

        PoolStringChain c = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        PoolStringChain d = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        ASSERT(c == d);
        ASSERT(c != a);

        // 7. Hash tests
        ASSERT(a.GetHash() == b.GetHash());
        ASSERT(c.GetHash() == d.GetHash());

        LOG_INFO("[PoolStringTest] All tests passed!");
        return true;
    }

}  // namespace BECore::Tests
