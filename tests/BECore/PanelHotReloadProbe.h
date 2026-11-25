#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/PanelAssets/IPanelModel.h>

namespace BECore::Tests {

    /// A model that counts its own lifetime and what the widget asks of it, in statics the test resets: the widget keeps its models private
    class CountingPanelModel : public IPanelModel {
        BE_CLASS(CountingPanelModel)
    public:
        CountingPanelModel() {
            ++created;
            ++alive;
        }

        ~CountingPanelModel() override {
            --alive;
        }

        void Update() override {
            ++updates;
        }

        bool RunCommand(StringView /*id*/, StringView /*argument*/) override {
            ++commands;
            return true;
        }

        inline static int created = 0;
        inline static int alive = 0;
        inline static int updates = 0;
        inline static int commands = 0;

        static void Reset() {
            created = 0;
            alive = 0;
            updates = 0;
            commands = 0;
        }

        BE_REFLECT_FIELD bool flag = false;
    };

}  // namespace BECore::Tests
