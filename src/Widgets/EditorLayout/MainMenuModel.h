#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/IPanelModel.h>

namespace BECore {

    class IEditorLayoutHost;

    /// What `MainMenuPanel.xml` binds to: the saved layouts the View menu offers
    struct MainMenuView {
        BE_CLASS(MainMenuView)

        /// The host has a layouts folder; the three layout items are disabled without one
        BE_REFLECT_FIELD bool hasStore = false;
        /// At least one layout is saved (`layouts` is not empty)
        BE_REFLECT_FIELD bool hasLayouts = false;
        /// The names of the saved layouts, sorted, as of the last scan
        BE_REFLECT_FIELD Vector<String> layouts;
    };

    /**
     * @brief The model of the editor's menu bar (an asset of the window kind `Bar`): the saved layouts and the commands on them
     *
     * The folder is read when the first `Update` finds a layouts folder and again on `layouts.menuOpened`, the `openCommand` of the two layout menus, so an open menu
     * never scans on a draw and a layout saved or deleted elsewhere shows the next time the menu opens. `layouts.load` hands the name to the host
     * (`IEditorLayoutHost::LoadLayout`), `layouts.delete` removes the file; both do nothing for an empty name or a layout that is not saved. The other commands of the menus
     * (quit, the panels, `game.pause`) are the editor's, not the model's.
     */
    class MainMenuModel : public IPanelModel {
        BE_CLASS(MainMenuModel)
    public:
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] const MainMenuView& GetView() const {
            return _view;
        }

    private:
        /// The host when it has a layouts folder; sets `hasStore` either way
        [[nodiscard]] IEditorLayoutHost* FindHost();
        void Rescan(IEditorLayoutHost& host);

        MainMenuView _view;
        bool _scanned = false;
    };

}  // namespace BECore
