#pragma once

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

}  // namespace BECore::Tests
