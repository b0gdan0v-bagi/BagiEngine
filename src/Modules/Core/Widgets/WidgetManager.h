#pragma once

#include <Core/Widgets/IWidget.h>
#include <Core/RefCounted/IntrusivePtr.h>

namespace Core {

    CORE_ENUM(WidgetType, uint8_t, ImGuiWidget, ClearScreenWidget)

    class WidgetManager {
    public:
        WidgetManager() = default;
        ~WidgetManager() = default;

        void RegisterWidget(IntrusivePtr<IWidget> widget);
        void UpdateAll() const;
        void DrawAll() const;
        void CreateWidgets();

    private:
        static IntrusivePtr<IWidget> CreateWidgetByType(WidgetType type);

        eastl::vector<IntrusivePtr<IWidget>> _widgets;
    };

}  // namespace Core
