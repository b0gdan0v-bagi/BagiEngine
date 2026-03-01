#include "WidgetManager.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Widgets/IWidget.h>

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
            auto widgetTypeName = widgetNode.ParseAttribute<eastl::string_view>("type");
            if (!widgetTypeName) {
                continue;
            }
            const auto widgetPtr = AbstractFactory<IWidget>::GetInstance().Create(*widgetTypeName);
            if (!widgetPtr) {
                continue;
            }

            XmlDeserializer deserializer;
            deserializer.LoadFromXmlNode(widgetNode);
            widgetPtr->Initialize(deserializer);

            RegisterWidget(widgetPtr);
        }
    }
}  // namespace BECore
