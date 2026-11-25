#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/PanelAssets/IPanelModel.h>

namespace BECore::Tests {

    /// A model of the tests: counts what the host asks of it
    class ProbePanelModel : public IPanelModel {
        BE_CLASS(ProbePanelModel)
    public:
        ProbePanelModel() = default;

        void Update() override {
            ++updates;
        }

        bool RunCommand(StringView id, StringView argument) override {
            lastCommand = String(id);
            lastArgument = String(argument);
            return id == "probe.run";
        }

        BE_REFLECT_FIELD int32_t updates = 0;
        BE_REFLECT_FIELD String lastCommand;
        BE_REFLECT_FIELD String lastArgument;
    };

}  // namespace BECore::Tests
