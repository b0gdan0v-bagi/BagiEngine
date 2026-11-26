#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <cstdint>

namespace BECore {

    class ISceneEditorHost;

    /**
     * @brief What the Scene Editor's toolbar and inspector bars bind to (`scene.<field>`)
     *
     * The name input and the layer stepper write `name` and `layer`; the model takes them in its next `Update`. Every other field is the model's.
     */
    struct SceneEditorPanelView {
        BE_CLASS(SceneEditorPanelView)

        /// The active scene has unsaved changes
        BE_REFLECT_FIELD bool dirty = false;
        BE_REFLECT_FIELD bool hasSelection = false;
        /// More than one node is selected; the inspector shows the primary (the last) one
        BE_REFLECT_FIELD bool multiSelection = false;
        BE_REFLECT_FIELD String multiText;
        BE_REFLECT_FIELD bool locked = false;
        /// The name, the layer or the node's components take no edit: the node is locked, or the host says so
        BE_REFLECT_FIELD bool nameReadOnly = false;
        BE_REFLECT_FIELD bool layerReadOnly = false;
        BE_REFLECT_FIELD bool structureReadOnly = false;
        /// An attached program refused the last edit
        BE_REFLECT_FIELD bool hasRefusal = false;
        BE_REFLECT_FIELD String refusalText;
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD int32_t layer = 0;
        /// The node is an instance of a node file
        BE_REFLECT_FIELD bool isInstance = false;
        BE_REFLECT_FIELD String instanceText;
    };

    /**
     * @brief The model of the Scene Editor's two bars (toolbar and inspector frame): the inspected node through `ISceneEditorHost`
     *
     * `Update` **applies before it refreshes**: a `name` or `layer` that differs from what the model last showed is the user's edit and goes to the host
     * (the name at every change, as a live field), then the view is copied from the host. `SceneNodeSelectedEvent` refreshes the view at once and drops an
     * edit not yet applied, so a new selection never shows, or receives, the old node's fields. With no host the view is every default (no selection).
     * The texts are made again only when the selection count, the node file or the refusal changed, so a steady `Update` allocates nothing.
     */
    class SceneEditorPanelModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(SceneEditorPanelModel)
    public:
        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] const SceneEditorPanelView& GetView() const {
            return _view;
        }
        [[nodiscard]] SceneEditorPanelView& GetView() {
            return _view;
        }

    private:
        void OnNodeSelected(const SceneNodeSelectedEvent& event);
        /// Sends the fields the user changed since the last refresh to @p host
        void ApplyEdits(ISceneEditorHost& host);
        /// Copies the host's inspected node into the view, or the defaults without a host
        void Refresh(const ISceneEditorHost* host);

        SceneEditorPanelView _view;
        String _shownName;           // the name the view last got from the host: a `name` that differs is the user's
        int32_t _shownLayer = 0;     // the same for `layer`
        size_t _shownCount = 0;      // the selection count `multiText` was made for
        PoolString _shownSourceRef;  // the node file `instanceText` was made for
        // What `refusalText` was made for
        String _shownRefusalCode;
        String _shownRefusalMessage;
    };

}  // namespace BECore
