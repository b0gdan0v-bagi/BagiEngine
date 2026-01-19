#pragma once

#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class WidgetManager {
    public:
        WidgetManager() = default;
        ~WidgetManager() = default;

        void RegisterWidget(IntrusivePtr<IWidget> widget);
        void UpdateAll() const;
        void DrawAll() const;
        void CreateWidgets();

    private:

        eastl::vector<IntrusivePtr<IWidget>> _widgets;
    };

}  // namespace BECore
