#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/PoolString/PoolStringChain.h>

namespace BECore::Tests {

    /**
     * @brief Test for PoolString and PoolStringChain functionality
     */
    class PoolStringTest : public ITest {
        BE_CLASS(PoolStringTest)
    public:
        PoolStringTest() = default;
        ~PoolStringTest() override = default;

        bool Run() override;
    };

}  // namespace BECore::Tests
