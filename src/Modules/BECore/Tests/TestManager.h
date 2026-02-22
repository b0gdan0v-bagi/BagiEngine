#pragma once

#include <BECore/Tests/ITest.h>

namespace BECore {

    class TestManager {
    public:
        TestManager() = default;
        ~TestManager() = default;
        void RunAllTests();

    private:
        eastl::vector<IntrusivePtr<Tests::ITest>> _tests;
    };

}  // namespace BECore
