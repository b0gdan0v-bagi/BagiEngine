#include "WidgetManager.h"

#include <Widgets/ImGuiWidget.h>
#include <Widgets/ClearScreenWidget.h>
#include <BECore/Config/XmlConfig.h>

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
        XmlConfig config = XmlConfig::Create();
        constexpr std::string_view configPath = "config/WidgetsConfig.xml";
        if (!config.LoadFromVirtualPath(configPath)) {
            return;
        }

        const auto rootNode = config.GetRoot();
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
            const auto widgetPtr = CreateWidgetByType(*widgetType);
            if (!widgetPtr) {
                continue;
            }
            widgetPtr->Initialize(widgetNode);
            RegisterWidget(widgetPtr);
        }
    }

    IntrusivePtr<IWidget> WidgetManager::CreateWidgetByType(WidgetType type) {
        switch (type) {
            case WidgetType::ImGuiWidget:
                return BECore::New<ImGuiWidget>();
            case WidgetType::ClearScreenWidget:
                return BECore::New<ClearScreenWidget>();
            default:
                return {};
        }
    }

}  // namespace BECore

