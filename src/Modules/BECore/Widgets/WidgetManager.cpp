#include "WidgetManager.h"

#include <BECore/Application/RunMode.h>
#include <BECore/Config/ConfigFactory.h>
#include <BECore/Widgets/WidgetList.h>

namespace BECore {

    void WidgetManager::RegisterWidget(IntrusivePtr<IWidget> widget) {
        if (widget) {
            _widgets.push_back(std::move(widget));
        }
    }

    void WidgetManager::UpdateAll() const {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->Update();
            }
        }
    }

    void WidgetManager::DrawScreenAll() const {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->DrawScreen();
            }
        }
    }

    void WidgetManager::DrawAll() const {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->Draw();
            }
        }
    }

    void WidgetManager::CreateWidgets() {
        const RunMode mode = ResolveRunMode(ReadRunMode(CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern)));
        LOG_INFO("Run mode: {}"_format(EnumUtils<RunMode>::ToString(mode)));

        const PoolString configName = GetWidgetsConfigName(mode);
        if (!CoreManager::GetConfigManager().HasConfig(configName)) {
            LOG_ERROR("WidgetManager: config '{}' not found, no widgets created"_format(configName.ToStringView()));
            return;
        }

        // Unknown types and widgets whose Initialize returns false are reported by MakeFromConfig and left out of the list
        const auto list = MakeFromConfig<WidgetList>(configName.ToStringView());
        for (const auto& widget : list->widgets) {
            RegisterWidget(widget);
        }
    }
}  // namespace BECore
