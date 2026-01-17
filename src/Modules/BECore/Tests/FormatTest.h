#pragma once

#include <BECore/Tests/ITest.h>

namespace BECore::Tests {

    /**
     * @brief Test for BECore::Format functionality
     */
    class FormatTest : public ITest {
    public:
        FormatTest() = default;
        ~FormatTest() override = default;

        const char* GetName() const override;
        bool Run() override;
    };

}  // namespace BECore::Tests
