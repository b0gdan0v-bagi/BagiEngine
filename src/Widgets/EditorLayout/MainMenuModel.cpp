#include "MainMenuModel.h"

#include <Generated/MainMenuModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>

namespace BECore {

    TypedRef MainMenuModel::GetObject() {
        return TypedRef::Of(_view);
    }

    IEditorLayoutHost* MainMenuModel::FindHost() {
        IEditorLayoutHost* host = EditorLayoutHostRegistry::GetInstance().Get();
        const bool usable = host != nullptr && host->GetLayouts() != nullptr;
        _view.hasStore = usable;
        if (!usable) {
            _scanned = false;
            _view.layouts.clear();
            _view.hasLayouts = false;
        }
        return usable ? host : nullptr;
    }

    void MainMenuModel::Rescan(IEditorLayoutHost& host) {
        _view.layouts = host.GetLayouts()->List();
        _view.hasLayouts = !_view.layouts.empty();
        _scanned = true;
    }

    void MainMenuModel::Update() {
        IEditorLayoutHost* host = FindHost();
        if (host != nullptr && !_scanned) {
            Rescan(*host);
        }
    }

    bool MainMenuModel::RunCommand(StringView id, StringView argument) {
        using namespace EditorCommandIds;
        const auto is = [id](const auto& command) { return id == command.View(); };
        if (!is(kLayoutsMenuOpened) && !is(kLayoutsLoad) && !is(kLayoutsDelete)) {
            return false;
        }
        IEditorLayoutHost* host = FindHost();
        if (host == nullptr) {
            return true;
        }
        LayoutStore& store = *host->GetLayouts();
        if (is(kLayoutsMenuOpened)) {
            Rescan(*host);
        } else if (!argument.empty() && store.Exists(argument)) {
            if (is(kLayoutsLoad)) {
                host->LoadLayout(argument);
            } else {
                if (const auto result = store.Delete(argument); !result) {
                    LOG_WARNING("Editor: cannot delete layout '{}': {}", argument, result.error().detail);
                }
                Rescan(*host);
            }
        }
        return true;
    }

}  // namespace BECore
