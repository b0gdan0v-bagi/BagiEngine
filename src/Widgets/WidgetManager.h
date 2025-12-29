#pragma once

#include "IWidget.h"
#include <Core/Utils/IntrusivePtr.h>
#include <Core/Config/XmlConfig.h>


namespace Core {

    enum class WidgetType {
        ImGuiWidget,
        ClearScreenWidget
    };

    class WidgetManager {
    public:
        WidgetManager() = default;
        ~WidgetManager() = default;

        void RegisterWidget(IntrusivePtr<IWidget> widget);
        void UpdateAll() const;
        void DrawAll() const;
        void CreateWidgets(const XmlConfig& config);

    private:
        static IntrusivePtr<IWidget> CreateWidgetByType(WidgetType type);

        std::vector<IntrusivePtr<IWidget>> _widgets;
    };

}  // namespace Core

