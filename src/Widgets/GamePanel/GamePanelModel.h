#pragma once

#include <BECore/Control/Json.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class IGame;
    class IControllableGame;

    /// One element of IControllableGame::GetActions(): its label and the object PerformAction takes as is
    struct GamePanelAction {
        String label;
        Json action;
    };

    /// One `BE_FUNCTION` of the active game's type or of an ancestor (the nearest wins a name)
    struct GamePanelMethod {
        StringView name;
        String signature;  // `Name(a, b)` with the declared parameter names
        bool callable = false;
        bool hasParams = false;
    };

    /**
     * @brief What the editor's Game panel shows of the active game, and the calls it makes; no ImGui.
     *
     * Sync() rebuilds the state text, the actions and the methods only when the game or its state version changed, so a steady frame
     * allocates nothing. The game is driven only through IGame, IControllableGame and the reflection registry: no game is named here.
     * Main thread only (the same frame stage as the widgets). The game pointer is not owned; Sync() with the active game every frame
     * before anything else is called keeps it alive (GameManager owns the games).
     */
    class GamePanelModel {
    public:
        /// Takes @p game (null: none is active); rebuilds when it or its state version moved. Returns true when it rebuilt
        bool Sync(IGame* game);

        [[nodiscard]] IGame* GetGame() const {
            return _game;
        }

        [[nodiscard]] PoolString GetGameName() const {
            return _gameName;
        }

        [[nodiscard]] bool IsControllable() const {
            return _controllable != nullptr;
        }

        [[nodiscard]] uint64_t GetStateVersion() const {
            return _stateVersion;
        }

        /// The state as indented JSON, or `code: message` when GetState() failed; empty for a game that is not controllable
        [[nodiscard]] const String& GetStateText() const {
            return _stateText;
        }

        [[nodiscard]] const Vector<GamePanelAction>& GetActions() const {
            return _actions;
        }

        /// The indices into GetActions() whose label holds the filter (upper and lower case alike); all of them for an empty filter
        [[nodiscard]] const Vector<size_t>& GetVisibleActions() const {
            return _visible;
        }

        void SetFilter(StringView filter);

        [[nodiscard]] const Vector<GamePanelMethod>& GetMethods() const {
            return _methods;
        }

        /// Performs the action at @p index of GetActions(); the outcome (or the error) becomes the last result
        void Perform(size_t index);

        /// IControllableGame::Reset with no options for a controllable game, IGame::Reset otherwise
        void Reset();

        /**
         * @brief Calls the method at @p index of GetMethods() with arguments typed as the Console's parameters (`key=value ...` or a JSON object).
         *
         * The developer's own text in the editor, so a `PoolString` parameter is allowed (the control API refuses it to a client).
         */
        void Call(size_t index, StringView arguments);

        /// The text of the last Perform, Reset or Call: the outcome as JSON, `ok`, or the error
        [[nodiscard]] const String& GetLastResult() const {
            return _lastResult;
        }

        [[nodiscard]] bool IsLastResultError() const {
            return _lastResultError;
        }

        /// Grows on every rebuild and every new result: a view caches what it draws against it
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

    private:
        void Rebuild();
        void RebuildVisible();
        void SetResult(String text, bool isError);

        IGame* _game = nullptr;
        IControllableGame* _controllable = nullptr;
        PoolString _gameName;
        uint64_t _stateVersion = 0;
        bool _synced = false;

        String _stateText;
        Vector<GamePanelAction> _actions;
        Vector<size_t> _visible;
        Vector<GamePanelMethod> _methods;
        String _filter;  // lower case

        String _lastResult;
        bool _lastResultError = false;
        uint64_t _version = 0;
    };

    /// The label of an action: `type key=value ...` when it is an object with a string `type` (the Console's syntax), else its compact JSON
    [[nodiscard]] String FormatGameActionLabel(const Json& action);

}  // namespace BECore
