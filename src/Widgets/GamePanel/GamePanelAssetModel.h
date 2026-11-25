#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/GamePanel/GamePanelModel.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>

namespace BECore {

    /// The game a model of the Game panel works on: the active one of `GameManager`, or the one a test fixed
    class GameChoice {
    public:
        [[nodiscard]] IGame* Get() const;

        /// Fixes the game (null: none); the pointer must outlive the model. Tests only: it keeps them off `GameManager`
        void Fix(IGame* game);

    private:
        IGame* _game = nullptr;
        bool _fixed = false;
    };

    /// A row of the list of actions the filter lets through; `index` is the place in `GamePanelModel::GetActions()`, the argument of `gamePanel.perform`
    struct GamePanelActionRow {
        BE_CLASS(GamePanelActionRow)

        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD uint32_t index = 0;
    };

    /// A row of the methods of the game; `arguments` is what the user typed for it and survives every rebuild of the other rows
    struct GamePanelMethodRow {
        BE_CLASS(GamePanelMethodRow)

        BE_REFLECT_FIELD String signature;
        BE_REFLECT_FIELD bool callable = false;
        BE_REFLECT_FIELD bool hasParams = false;
        BE_REFLECT_FIELD String arguments;
        BE_REFLECT_FIELD uint32_t index = 0;
    };

    /**
     * @brief What `GamePanel.xml` binds to: the state of `GamePanelModel` as bools, texts and rows
     *
     * A condition of an asset is one bool (the format has no AND), so every combination the panel shows is a field of its own. `filter` is written
     * by the panel's input and read back by the model; the other texts are the model's.
     */
    struct GamePanelView {
        BE_CLASS(GamePanelView)

        BE_REFLECT_FIELD bool hasGame = false;
        BE_REFLECT_FIELD bool isControllable = false;
        BE_REFLECT_FIELD bool notControllable = false;
        BE_REFLECT_FIELD bool showMethods = false;
        BE_REFLECT_FIELD bool hasResult = false;
        BE_REFLECT_FIELD bool resultOk = false;
        BE_REFLECT_FIELD bool resultError = false;
        BE_REFLECT_FIELD bool noActionsLegal = false;
        BE_REFLECT_FIELD bool noActionMatches = false;
        BE_REFLECT_FIELD bool showActionsList = false;
        /// The name behind the game icon: a text of the format has no icon of its own
        BE_REFLECT_FIELD String gameName;
        BE_REFLECT_FIELD String versionText;
        BE_REFLECT_FIELD String countText;
        BE_REFLECT_FIELD String lastResult;
        BE_REFLECT_FIELD String stateText;
        BE_REFLECT_FIELD String filter;
        BE_REFLECT_FIELD Vector<GamePanelActionRow> actions;
        BE_REFLECT_FIELD Vector<GamePanelMethodRow> methods;
    };

    /**
     * @brief The model of the Game panel asset: `GamePanelModel` (no ImGui) behind a reflected view
     *
     * Commands: `gamePanel.reset`, `gamePanel.perform` and `gamePanel.call` (the argument is the `index` of an action or method row; a text that is no
     * index, or one past the list, does nothing) and `gamePanel.copyState` (the one ImGui call). The view is rebuilt only when the model's version
     * moved (a new state, a filter, a result), so a steady frame allocates nothing; the typed `arguments` of the method rows are kept until the game
     * or its method count changes. The active game is read at every `Update` and command, never kept past it.
     */
    class GamePanelAssetModel : public IPanelModel {
        BE_CLASS(GamePanelAssetModel)
    public:
        GamePanelAssetModel() = default;

        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        /// The game the panel shows instead of the active one of `GameManager`. It must outlive the model
        void SetGame(IGame* game);

        [[nodiscard]] const GamePanelView& GetView() const {
            return _view;
        }

    private:
        void Refresh();
        void RebuildView();

        GamePanelModel _model;
        GamePanelView _view;
        GameChoice _choice;
        const IGame* _methodsGame = nullptr;
        uint64_t _builtVersion = 0;
        bool _viewBuilt = false;
    };

    /**
     * @brief The source `game` of the Game panel: the active game itself, for `PanelFields bind="game"`
     *
     * `GetObject()` finds the game again at every read, so a game switched while the panel is open is followed and a game that is gone gives no
     * object. It has no command.
     */
    class ActiveGameSourceModel : public IPanelModel {
        BE_CLASS(ActiveGameSourceModel)
    public:
        ActiveGameSourceModel() = default;

        [[nodiscard]] TypedRef GetObject() override;
        bool RunCommand(StringView id, StringView argument) override;

        /// The game this source gives instead of the active one of `GameManager`. It must outlive the model
        void SetGame(IGame* game);

    private:
        GameChoice _choice;
    };

}  // namespace BECore
