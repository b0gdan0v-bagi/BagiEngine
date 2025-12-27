#include "WidgetManager.h"

#include <Widgets/ImGuiWidget.h>
#include <Core/Utils/New.h>
#include <boost/property_tree/ptree.hpp>

namespace Core {

    void WidgetManager::RegisterWidget(IntrusivePtr<IWidget> widget) {
        if (widget) {
            _widgets.push_back(std::move(widget));
        }
    }

    void WidgetManager::DrawAll() {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->Draw();
            }
        }
    }

    void WidgetManager::CreateWidgets(const XmlConfig& config) {
        // Пробуем разные пути к списку виджетов
        auto widgetsNode = config.GetChild("root.widgets");
        if (!widgetsNode) {
            widgetsNode = config.GetChild("widgets");
        }
        if (!widgetsNode) {
            widgetsNode = config.GetChild("root");
        }

        if (!widgetsNode) {
            return;
        }

        const auto& tree = *widgetsNode;
        
        // Итерируемся по дочерним узлам
        for (const auto& node : tree) {
            // Ищем узлы с именем "widget"
            if (node.first == "widget") {
                const auto& widgetNode = node.second;
                
                // Получаем тип виджета из атрибута или из дочернего узла
                std::string type = widgetNode.get<std::string>("<xmlattr>.type", "");
                if (type.empty()) {
                    type = widgetNode.get<std::string>("type", "");
                }
                
                if (!type.empty()) {
                    auto widget = CreateWidgetByType(type, widgetNode);
                    if (widget) {
                        RegisterWidget(widget);
                    }
                }
            }
        }
    }

    IntrusivePtr<IWidget> WidgetManager::CreateWidgetByType(const std::string& type, const boost::property_tree::ptree& widgetNode) {
        if (type == "ImGuiWidget") {
            return Core::New<ImGuiWidget>();
        }

        return {};
    }

}  // namespace Core

