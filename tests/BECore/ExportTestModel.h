#pragma once

#include <BECore/ExportFakes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/Export/ExportPanelModel.h>

namespace BECore::Tests {

    /// The Export window's model over the fakes of a test, and reachable by it: the widget keeps its models private
    class ExportTestModel : public ExportPanelModel {
        BE_CLASS(ExportTestModel)
    public:
        ExportTestModel() {
            // Null outside the asset test: the type registry's tests make every creatable type
            if (fixture != nullptr) {
                fixture->Apply(*this);
                instance = this;
            }
        }

        ~ExportTestModel() override {
            if (instance == this) {
                instance = nullptr;
            }
        }

        inline static ExportFixture* fixture = nullptr;
        /// The model the hosted asset made last; null once it is gone
        inline static ExportTestModel* instance = nullptr;
    };

}  // namespace BECore::Tests
