#pragma once

#include "PoolStringChain.h"
#include <cassert>
#include <iostream>

namespace Core::Tests {

    inline void PoolStringTest() {
        // --- Compile-time tests ---
        static_assert(PoolStringChain("test").View() == "test");
        static_assert(PoolStringChain("").Empty());
        static_assert(PoolStringChain("hello").Size() == 5);

        std::cout << "[PoolStringTest] Starting runtime tests..." << std::endl;

        // --- Runtime tests ---

        // 1. Literal test
        PoolStringChain lit("literal");
        assert(lit.View() == "literal");
        assert(lit.Size() == 7);
        assert(!lit.IsSingle());

        // 2. Atom test
        PoolString ps1 = PoolString::Intern("atom");
        PoolStringChain atom(ps1);
        assert(atom.View() == "atom");
        assert(atom.IsSingle());
        assert(atom.Size() == 4);

        // 3. Concat test
        PoolStringChain chain = PoolStringChain::Concat(ps1, 42);
        assert(chain.Size() == 6);
        assert(chain.Materialize() == "atom42");

        // 4. operator+= tests
        chain += PoolString::Intern("_suffix");
        assert(chain.Size() == 13);
        assert(chain.Materialize() == "atom42_suffix");

        // 5. Auto-materialization test
        chain += 777;
        assert(chain.Materialize() == "atom42_suffix777");
        assert(chain.Size() == 16);

        // 6. Equality tests
        PoolStringChain a("same");
        PoolStringChain b("same");
        assert(a == b);

        PoolStringChain c = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        PoolStringChain d = PoolStringChain::Concat(PoolString::Intern("v"), 1);
        assert(c == d);
        assert(c != a);

        // 7. Hash tests
        assert(a.GetHash() == b.GetHash());
        assert(c.GetHash() == d.GetHash());

        std::cout << "[PoolStringTest] All tests passed!" << std::endl;
    }

} // namespace Core::Tests
