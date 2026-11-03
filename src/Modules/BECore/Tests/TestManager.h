#pragma once

#include <BECore/Tests/ITest.h>

namespace BECore {

    class TestManager {
    public:
        TestManager() = default;
        ~TestManager() = default;
        void RunAllTests();
    };

}  // namespace BECore
