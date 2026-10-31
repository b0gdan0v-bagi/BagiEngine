#include "WidgetManager.h"

#include <BECore/Application/RunMode.h>
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
        const RunMode mode = ReadRunMode(CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern));
        LOG_INFO("Run mode: {}"_format(EnumUtils<RunMode>::ToString(mode)));

        const PoolString configName = GetWidgetsConfigName(mode);
        const auto rootNode = CoreManager::GetConfigManager().GetConfig(configName);

        if (!rootNode) {
            LOG_ERROR("WidgetManager: config '{}' not found, no widgets created"_format(configName.ToStringView()));
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
