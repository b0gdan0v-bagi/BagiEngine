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
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

    private:
        /**
         * @brief Compile-time tests using static_assert
         */
        static constexpr void TestCompileTime() {
            // PoolStringChain compile-time tests
            static_assert(PoolStringChain("test").View() == "test");
            static_assert(PoolStringChain("").Empty());
            static_assert(PoolStringChain("hello").Size() == 5);
            static_assert(!PoolStringChain("abc").Empty());
            static_assert(PoolStringChain("ab").Size() + PoolStringChain("cd").Size() == 4);
        }
    };

    // Compile-time validation of test class
    static_assert(ValidTest<PoolStringTest>);

}  // namespace BECore::Tests
