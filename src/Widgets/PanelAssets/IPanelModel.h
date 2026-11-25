#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief The C++ half of a panel asset: owns the data a panel binds to and the commands its buttons emit
     *
     * An asset names its model in `<sources>` (`model="FileChangesPanelModel"`); `AssetPanelWidget` makes one through `CreatePanelModel` and
     * registers `GetObject()` under the source's name. Main thread only.
     */
    class IPanelModel : public RefCounted {
        BE_CLASS(IPanelModel, FACTORY_BASE)
    public:
        IPanelModel() = default;
        ~IPanelModel() override = default;

        /// The reflected object the asset binds to; the model itself unless it keeps its view in a member. Asked at every read, so it is never kept
        [[nodiscard]] virtual TypedRef GetObject();

        /// Once, right after the widget has made the model and before its object is read or a command reaches it. The place for the subscriptions, the
        /// document source and the slots a model needs; a model kept over a reload of the asset is not initialized again. A test calls it itself
        virtual void Initialize() {}

        /// Once per frame while the panel is open, before it draws. Allocates nothing when nothing changed
        virtual void Update() {}

        /// After the panel's window of this frame is drawn, paired with the `Update` of the same frame (a frame without an `Update` has none). A model that
        /// holds something across the draw, such as a paused counter, lets go of it here. It follows the `OnOpenChanged` that the frame's close button caused
        virtual void EndUpdate() {}

        /// The panel's open flag changed, however it changed: the open command, the window's close button, the asset's `open` at start. Told once per change,
        /// at the start of `Update` (or after the window when its close button was the cause), and `false` when the widget or the model's source goes away
        /// while open, so a model that switches a global thing on while open always gets the call that switches it off. A model is not updated while closed
        /// and could not see a close button by itself. A model made while the panel is open gets `true` right after `Initialize`
        virtual void OnOpenChanged(bool /*open*/) {}

        /// Runs @p id when this model owns it and returns true. @p argument is the text of the clicked button's `argument`, empty when it has none
        virtual bool RunCommand(StringView id, StringView argument) = 0;
    };

    /// Whether a model class of this name is registered. Never interns: the name comes from a file
    [[nodiscard]] bool IsKnownPanelModel(StringView name);

    /// A new model of the class @p name, or null when none is registered
    [[nodiscard]] IntrusivePtr<IPanelModel> CreatePanelModel(StringView name);

}  // namespace BECore
