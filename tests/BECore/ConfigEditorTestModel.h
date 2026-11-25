#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/ConfigEditor/ConfigEditorPanelModel.h>

namespace BECore::Tests {

    /// The Config Editor's model on the manager a test names: the widget keeps its models private, so an asset hosted by a real widget would
    /// otherwise work on CoreManager's configs, the engine's own files
    class ConfigEditorTestModel : public ConfigEditorPanelModel {
        BE_CLASS(ConfigEditorTestModel)
    public:
        ConfigEditorTestModel() {
            UseConfigManager(manager);
        }

        inline static ConfigManager* manager = nullptr;
    };

}  // namespace BECore::Tests
