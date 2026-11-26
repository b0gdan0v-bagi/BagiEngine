#pragma once

#include <BECore/Control/ControlService.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/Feedback/ActivityPanelModel.h>

namespace BECore::Tests {

    /// The Activity panel's model over the control service of a test: the widget makes its models itself and gives a test no way in
    class ActivityTestModel : public ActivityPanelModel {
        BE_CLASS(ActivityTestModel)
    public:
        ActivityTestModel() {
            // Null outside the asset test: the type registry's tests make every creatable type
            if (service != nullptr) {
                SetService(*service);
            }
        }

        inline static ControlService* service = nullptr;
    };

}  // namespace BECore::Tests
