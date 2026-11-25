#include "EditorDialogsModel.h"

#include <Generated/EditorDialogsModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>

namespace BECore {

    namespace {

        constexpr StringView kInvalidNameWarning = "Use letters, digits, space, - and _ (no space at the ends)";
        constexpr StringView kExistsWarning = "A layout with this name exists; saving replaces it";

    }  // namespace

    TypedRef EditorDialogsModel::GetObject() {
        return TypedRef::Of(_view);
    }

    IEditorLayoutHost* EditorDialogsModel::FindHost() {
        IEditorLayoutHost* host = EditorLayoutHostRegistry::GetInstance().Get();
        const bool usable = host != nullptr && host->GetLayouts() != nullptr;
        _view.noFolder = !usable;
        return usable ? host : nullptr;
    }

    void EditorDialogsModel::Update() {
        IEditorLayoutHost* host = FindHost();
        if (!_view.saveLayoutOpen) {
            return;
        }
        if (host == nullptr) {
            Close();
            return;
        }
        if (_view.layoutName != _derivedFor) {
            Derive(*host->GetLayouts());
        }
    }

    bool EditorDialogsModel::RunCommand(StringView id, StringView /*argument*/) {
        using namespace EditorCommandIds;
        const auto is = [id](const auto& command) { return id == command.View(); };
        if (!is(kViewSaveLayout) && !is(kLayoutsSaveDialogSave) && !is(kLayoutsSaveDialogEnter) && !is(kLayoutsSaveDialogCancel)) {
            return false;
        }
        IEditorLayoutHost* host = FindHost();
        if (is(kViewSaveLayout)) {
            OpenSaveLayout(host);
        } else if (is(kLayoutsSaveDialogCancel)) {
            Close();
        } else if (host != nullptr && _view.saveLayoutOpen) {
            Save(*host, is(kLayoutsSaveDialogSave));
        }
        return true;
    }

    void EditorDialogsModel::OpenSaveLayout(IEditorLayoutHost* host) {
        if (host == nullptr) {
            return;
        }
        _view.layoutName.clear();
        _view.error.clear();
        _view.hasError = false;
        Derive(*host->GetLayouts());
        _view.saveLayoutOpen = true;
    }

    void EditorDialogsModel::Close() {
        _view.saveLayoutOpen = false;
    }

    void EditorDialogsModel::Derive(const LayoutStore& layouts) {
        _derivedFor = _view.layoutName;
        const bool isEmpty = _view.layoutName.empty();
        _view.canSave = IsValidLayoutName(_view.layoutName);
        _view.exists = _view.canSave && layouts.Exists(_view.layoutName);
        if (!isEmpty && !_view.canSave) {
            _view.warning.assign(kInvalidNameWarning.data(), kInvalidNameWarning.size());
        } else if (_view.exists) {
            _view.warning.assign(kExistsWarning.data(), kExistsWarning.size());
        } else {
            _view.warning.clear();
        }
        _view.hasWarning = !_view.warning.empty();
    }

    void EditorDialogsModel::Save(IEditorLayoutHost& host, bool replace) {
        // The input writes the field in the frame of the click, before this command runs: derive again instead of trusting the last frame's flags
        Derive(*host.GetLayouts());
        if (!_view.canSave || (_view.exists && !replace)) {
            return;
        }
        const std::expected<void, String> result = host.SaveCurrentLayoutAs(_view.layoutName);
        if (!result) {
            _view.error = result.error();
            _view.hasError = true;
            return;
        }
        _view.error.clear();
        _view.hasError = false;
        Close();
    }

}  // namespace BECore
