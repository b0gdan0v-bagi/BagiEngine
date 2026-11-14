#pragma once

#include <BECore/Widgets/IWidget.h>

namespace BECore {

    /**
     * @brief The widgets of a WidgetsConfig / GameWidgetsConfig file, in file order
     *
     * Filled by MakeFromConfig<WidgetList>(configName): the loader creates each `<widget type="...">` through
     * AbstractFactory<IWidget>, reads its fields and calls IWidget::Initialize(IDeserializer&) inside the element
     * scope. An unknown type or an Initialize that returns false drops the widget and is reported by the deserializer.
     */
    struct WidgetList : RefCounted {
        BE_CLASS(WidgetList)

        BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<IWidget>> widgets;
    };

}  // namespace BECore
