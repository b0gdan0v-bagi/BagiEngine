#include "WidgetManager.h"

#include <Widgets/ImGuiWidget.h>
#include <Widgets/ClearScreenWidget.h>
#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>

#include <Generated/EnumWidget.gen.hpp>

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

    void WidgetManager::DrawAll() const {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->Draw();
            }
        }
    }

    void WidgetManager::CreateWidgets() {
        // Получаем конфиг через ConfigManager
        const auto rootNode = CoreManager::GetConfigManager().GetConfig("WidgetsConfig"_intern);
        
        if (!rootNode) {
            return;
        }

        const auto widgetsNode = rootNode.GetChild("widgets");
        if (!widgetsNode) {
            return;
        }

        for (const auto widgetNode : widgetsNode.Children()) {
            auto name = widgetNode.Name();
            if (name != "widget") {
                continue;
            }
            auto widgetType = widgetNode.ParseAttribute<WidgetType>("type");
            if (!widgetType) {
                continue;
            }
            const auto widgetPtr = WidgetFactory::Create(*widgetType);
            if (!widgetPtr) {
                continue;
            }
            widgetPtr->Initialize(widgetNode);
            RegisterWidget(widgetPtr);
        }
    }
}  // namespace BECore

