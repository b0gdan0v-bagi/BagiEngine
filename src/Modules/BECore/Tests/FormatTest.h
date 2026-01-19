#pragma once

#include <BECore/Tests/ITest.h>

namespace BECore::Tests {

    /**
     * @brief Test for BECore::Format functionality
     */
    class FormatTest : public ITest {
        BE_CLASS(FormatTest)
    public:
        FormatTest() = default;
        ~FormatTest() override = default;

        bool Run() override;
    };

}  // namespace BECore::Tests
