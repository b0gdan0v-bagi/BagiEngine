#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelElement.h>

namespace BECore {

    /// How the editor opens the window of an asset: `Docked` in the dock space, `Floating` never docked, `Dialogs` a host with no visible window that draws the popups of a
    /// tool whose own window is hand-written, `Bar` a host with no window that a hand-written window draws into (a menu bar or a toolbar). A `Dialogs` or `Bar` asset is
    /// always open, so it must say `open="true"`, and its `command` is not used (nothing shows or hides it); only a `Bar` asset may hold menus
    CORE_ENUM(PanelWindowKind, uint8_t, Docked, Floating, Dialogs, Bar)

    /// A named source of a panel: `model` is the text the editor resolves to a model object (here it is only a text)
    struct PanelSource {
        BE_CLASS(PanelSource)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String model;
    };

    /**
     * @brief The root of a panel file (`config/panels/<Name>Panel.xml`): a panel described as data
     *
     * Read by `LoadPanelAsset`, which reports every problem as data; an asset that has issues is still safe to hold, but a panel
     * shows the issues and not the asset. `command` is the editor command that toggles the panel, `open` its state at start.
     */
    class PanelAsset : public RefCounted {
        BE_CLASS(PanelAsset)
    public:
        BE_REFLECT_FIELD String id;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String titleBind;  ///< a text field whose value replaces `title` in the window title; the window keeps its id
        BE_REFLECT_FIELD String command;
        BE_REFLECT_FIELD bool open = false;
        BE_REFLECT_FIELD PanelWindowKind window = PanelWindowKind::Docked;
        BE_REFLECT_FIELD float width = 0.0f;
        BE_REFLECT_FIELD float height = 0.0f;
        BE_REFLECT_FIELD Vector<PanelSource> sources;
        BE_REFLECT_FIELD Vector<IntrusivePtr<PanelElement>> elements;
    };

}  // namespace BECore
