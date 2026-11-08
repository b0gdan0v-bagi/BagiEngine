#pragma once

#include <BECore/Widgets/IWidget.h>

namespace BECore::Tests {

    /// A widget that writes "create:<id>" (its Initialize) and "destroy:<id>" (its destructor) to a static log
    class LifecycleWidget : public IWidget {
        BE_CLASS(LifecycleWidget)
    public:
        LifecycleWidget() = default;
        ~LifecycleWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override {}
        void Draw() override {}

        BE_REFLECT_FIELD int32_t id = 0;

        static inline eastl::vector<eastl::string> log;
    };

}  // namespace BECore::Tests
