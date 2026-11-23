#pragma once

#include <BECore/Memory/ScratchArena.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /**
     * A fixture whose case runs inside a `ScratchScope`. The front end of the compiler (`ParseExpression`, `CheckExpression`, `ParseScript`) returns containers of the thread's scratch
     * arena and opens no scope of its own, so a test that holds a tree or a typed result across its own lines needs the scope to outlive them: `TEST_F` over this class (or an alias
     * of it named like the suite) gives the memory back when the case ends.
     */
    class ScratchScopeFixture : public testing::Test {
    private:
        ScratchScope _scratch;
    };

}  // namespace BECore::Tests
