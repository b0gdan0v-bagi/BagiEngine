#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore::Tests {

    /// A widget that records that Initialize ran, and fails when its `fail` attribute is set
    class ProbeWidget : public IWidget {
        BE_CLASS(ProbeWidget)
    public:
        ProbeWidget() = default;
        ~ProbeWidget() override = default;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override {}
        void Draw() override {}

        BE_REFLECT_FIELD int32_t id = 0;

        static inline int32_t initializeCalls = 0;

    private:
        bool _fail = false;
    };

    /// A runtime test that does nothing
    class ProbeRuntimeTest : public ITest {
        BE_CLASS(ProbeRuntimeTest)
    public:
        ProbeRuntimeTest() = default;
        ~ProbeRuntimeTest() override = default;

        bool Run() override {
            return true;
        }
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

        BE_REFLECT_FIELD int32_t id = 0;
    };

}  // namespace BECore::Tests
