#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/ArtGen/ArtGenPanelModel.h>

namespace BECore::Tests {

    /// The Generate panel's model over the session, control service and control status a test names, never the editor's, and reachable by the test:
    /// the widget keeps its models private. The bridge and the control API's start are fakes that only count
    class ArtGenPanelTestModel : public ArtGenPanelModel {
        BE_CLASS(ArtGenPanelTestModel)
    public:
        ArtGenPanelTestModel() {
            ArtGenPanelServices services;
            services.session = session;
            services.control = control;
            services.controlStatus = [] { return status != nullptr ? *status : ControlServerStatus{}; };
            services.launchBridge = [](uint16_t /*port*/, String& /*error*/) { return true; };
            services.startControl = [] { ++controlStarts; };
            SetServices(std::move(services));
            instance = this;
        }

        ~ArtGenPanelTestModel() override {
            if (instance == this) {
                instance = nullptr;
            }
        }

        inline static ArtGenSession* session = nullptr;
        inline static ControlService* control = nullptr;
        inline static const ControlServerStatus* status = nullptr;
        inline static int controlStarts = 0;
        /// The model the hosted asset made last; null once it is gone
        inline static ArtGenPanelTestModel* instance = nullptr;
    };

}  // namespace BECore::Tests
