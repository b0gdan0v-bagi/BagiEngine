#include "GamePanelAssetModel.h"

#include <Games/GameManager.h>
#include <Games/IGame.h>
#include <Generated/GamePanelAssetModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Icons/IconsLucide.h>
#include <imgui.h>

namespace BECore {

    namespace {

        /// The index a button passes as text; none for anything that is not a plain number
        Optional<size_t> ParseIndex(StringView text) {
            return StringUtils::ParseNumber<size_t>(text);
        }

    }  // namespace

    IGame* GameChoice::Get() const {
        return _fixed ? _game : GameManager::GetInstance().GetActive();
    }

    void GameChoice::Fix(IGame* game) {
        _game = game;
        _fixed = true;
    }

    TypedRef GamePanelAssetModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void GamePanelAssetModel::SetGame(IGame* game) {
        _choice.Fix(game);
        Refresh();
    }

    void GamePanelAssetModel::Update() {
        Refresh();
    }

    void GamePanelAssetModel::Refresh() {
        _model.Sync(_choice.Get());
        // The model compares with its own copy of the filter and does nothing when it is the same
        _model.SetFilter(_view.filter);
        if (!_viewBuilt || _model.GetVersion() != _builtVersion) {
            RebuildView();
        }
    }

    bool GamePanelAssetModel::RunCommand(StringView id, StringView argument) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        const bool reset = is(EditorCommandIds::kGamePanelReset);
        const bool perform = is(EditorCommandIds::kGamePanelPerform);
        const bool call = is(EditorCommandIds::kGamePanelCall);
        const bool copyState = is(EditorCommandIds::kGamePanelCopyState);
        if (!reset && !perform && !call && !copyState) {
            return false;
        }
        // The game may have changed since the last Update; the model must not act on a game that is gone
        Refresh();
        if (reset) {
            _model.Reset();
        } else if (copyState) {
            ImGui::SetClipboardText(_model.GetStateText().c_str());
        } else if (const Optional<size_t> index = ParseIndex(argument)) {
            if (perform) {
                _model.Perform(*index);
            } else if (*index < _view.methods.size()) {
                _model.Call(*index, StringView(_view.methods[*index].arguments));
            }
        }
        Refresh();
        return true;
    }

    void GamePanelAssetModel::RebuildView() {
        const IGame* game = _model.GetGame();
        const bool controllable = _model.IsControllable();
        const Vector<GamePanelAction>& actions = _model.GetActions();
        const Vector<size_t>& visible = _model.GetVisibleActions();
        const Vector<GamePanelMethod>& methods = _model.GetMethods();

        _view.hasGame = game != nullptr;
        _view.isControllable = controllable;
        _view.notControllable = game != nullptr && !controllable;
        _view.showMethods = !methods.empty();
        _view.hasResult = !_model.GetLastResult().empty();
        _view.resultOk = _view.hasResult && !_model.IsLastResultError();
        _view.resultError = _view.hasResult && _model.IsLastResultError();
        _view.noActionsLegal = controllable && actions.empty();
        _view.noActionMatches = controllable && !actions.empty() && visible.empty();
        _view.showActionsList = !visible.empty();

        _view.gameName.clear();
        if (game != nullptr) {
            _view.gameName.append(ICON_LC_GAMEPAD_2 " ");
            const StringView name = _model.GetGameName().ToStringView();
            _view.gameName.append(name.data(), name.size());
        }
        _view.versionText.clear();
        if (controllable) {
            _view.versionText = Format("v{}", _model.GetStateVersion());
        }
        _view.countText = Format("{} / {}", visible.size(), actions.size());
        _view.lastResult = _model.GetLastResult();
        _view.stateText = _model.GetStateText();

        _view.actions.resize(visible.size());
        for (size_t i = 0; i < visible.size(); ++i) {
            _view.actions[i].label = actions[visible[i]].label;
            _view.actions[i].index = static_cast<uint32_t>(visible[i]);
        }

        // What the user typed for a method stays while the game and its methods stay
        if (_methodsGame != game || _view.methods.size() != methods.size()) {
            _methodsGame = game;
            _view.methods.clear();
            _view.methods.resize(methods.size());
        }
        for (size_t i = 0; i < methods.size(); ++i) {
            GamePanelMethodRow& row = _view.methods[i];
            row.signature = methods[i].signature;
            row.callable = methods[i].callable;
            row.hasParams = methods[i].hasParams;
            row.index = static_cast<uint32_t>(i);
        }

        _builtVersion = _model.GetVersion();
        _viewBuilt = true;
    }

    TypedRef ActiveGameSourceModel::GetObject() {
        IGame* game = _choice.Get();
        return game != nullptr ? TypedRef::Of(*game) : TypedRef{};
    }

    bool ActiveGameSourceModel::RunCommand(StringView /*id*/, StringView /*argument*/) {
        return false;
    }

    void ActiveGameSourceModel::SetGame(IGame* game) {
        _choice.Fix(game);
    }

}  // namespace BECore
