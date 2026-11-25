#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/IPanelModel.h>

namespace BECore {

    class IEditorLayoutHost;
    class LayoutStore;

    /**
     * @brief What `EditorDialogsPanel.xml` binds to: the Save Layout dialog
     *
     * The input writes `layoutName` at every change; the model reads it in its next `Update`. Every other field is the model's.
     */
    struct EditorDialogsView {
        BE_CLASS(EditorDialogsView)

        /// The `open` of the Save Layout popup
        BE_REFLECT_FIELD bool saveLayoutOpen = false;
        BE_REFLECT_FIELD String layoutName;
        /// Why the name cannot be used, or that it replaces a layout; empty when neither
        BE_REFLECT_FIELD String warning;
        BE_REFLECT_FIELD bool hasWarning = false;
        /// The sentence of the last save that failed
        BE_REFLECT_FIELD String error;
        BE_REFLECT_FIELD bool hasError = false;
        /// The name is a valid layout name
        BE_REFLECT_FIELD bool canSave = false;
        /// A layout of this name is in the store: the button says Overwrite
        BE_REFLECT_FIELD bool exists = false;
        /// There is no editor host or it has no layouts folder
        BE_REFLECT_FIELD bool noFolder = true;
    };

    /**
     * @brief The model of the editor's own dialogs (an asset of the window kind `Dialogs`): Save Layout As
     *
     * `view.saveLayout` opens the dialog (nothing opens without a layouts folder, as the menu item is disabled then); the name is checked by
     * `IsValidLayoutName` and `LayoutStore::Exists` only when it changed. A save goes through `IEditorLayoutHost::SaveCurrentLayoutAs`; a failure keeps the
     * dialog open with the sentence, a success closes it. Enter saves a name that is valid and new, replacing a layout needs the Overwrite button.
     *
     * Commands (`EditorCommandIds`): `layouts.saveDialog.save` (both buttons), `layouts.saveDialog.enter` (the input's `submitCommand`) and
     * `layouts.saveDialog.cancel`, which is also the popup's `closeCommand`.
     */
    class EditorDialogsModel : public IPanelModel {
        BE_CLASS(EditorDialogsModel)
    public:
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] const EditorDialogsView& GetView() const {
            return _view;
        }
        [[nodiscard]] EditorDialogsView& GetView() {
            return _view;
        }

    private:
        /// The host when it has a layouts folder; sets `noFolder` either way
        [[nodiscard]] IEditorLayoutHost* FindHost();
        void OpenSaveLayout(IEditorLayoutHost* host);
        void Close();
        /// Makes `canSave`, `exists` and the warning from `layoutName`
        void Derive(const LayoutStore& layouts);
        /// Saves under the current name when it is valid; @p replace says whether an existing layout may be overwritten
        void Save(IEditorLayoutHost& host, bool replace);

        EditorDialogsView _view;
        String _derivedFor;  // the name `canSave`, `exists` and the warning were made from
    };

}  // namespace BECore
