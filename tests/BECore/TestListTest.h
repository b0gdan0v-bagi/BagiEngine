#pragma once

#include <BECore/Tests/ITest.h>

namespace BECore::Tests {

    /// A runtime test that does nothing
    class ProbeRuntimeTest : public ITest {
        BE_CLASS(ProbeRuntimeTest)
    public:
        ProbeRuntimeTest() = default;
        ~ProbeRuntimeTest() override = default;

        bool Run() override {
            return true;
        }
        StringView GetName() override {
            return GetStaticTypeName();
        }

        BE_REFLECT_FIELD int32_t id = 0;
    };

}  // namespace BECore::Tests
