#pragma once

#if BE_WITH_EDITOR

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class ControlService;
    class IGame;
    class JsonWriter;

    /// What the value of a command's parameter has to be; `Json` takes any value
    CORE_ENUM(GameCommandParamType, uint8_t, String, Integer, Number, Boolean, Json)

    struct GameCommandParam {
        String name;
        GameCommandParamType type = GameCommandParamType::String;
        bool required = false;
        String description;
    };

    /// A command's callback, with the contract of IControllableGame::PerformAction: it writes exactly one value into @p result (`Null()` for nothing to say) and
    /// returns success, or returns an error. The declared parameters in @p arguments have been checked; the view is valid for the call only
    using GameCommandCallback = MoveOnlyFunction<std::expected<void, ControlError>(JsonView arguments, JsonWriter& result)>;

    /// A command as a game declares it (GameEditorTools::AddCommand)
    struct GameToolCommand {
        String name;
        String title;    // for people; empty: the name
        String summary;  // one line
        Vector<GameCommandParam> params;
        GameCommandCallback run;
    };

    /// A debug panel as a game declares it (GameEditorTools::AddPanel)
    struct GameToolPanel {
        String id;
        String title;  // for people; empty: the id
        MoveOnlyFunction<void()> draw;
    };

    /// A registered command. Whoever calls it holds it, because the call may take it out of the registry
    struct GameToolCommandEntry : public RefCounted {
        String method;  // the control method, `<prefix>.<name>`
        GameToolCommand command;
    };

    /// A registered panel. Whoever draws it holds it, as with a command
    struct GameToolPanelEntry : public RefCounted {
        GameToolPanel panel;
        bool open = true;
    };

    /**
     * @brief The editor commands and debug panels of the active game (IGame::RegisterEditorTools).
     *
     * GameManager owns the one instance, activates it for the game that became active and clears it before that game stops. A command is a control method
     * `<prefix>.<name>` while it is registered. The contract, the naming and the lifetime: `.claude/rules/games-editor-tools.md`. Main thread only.
     */
    class GameEditorTools {
    public:
        GameEditorTools() = default;

        GameEditorTools(const GameEditorTools&) = delete;
        GameEditorTools& operator=(const GameEditorTools&) = delete;

        /// Clears, takes the prefix from the name of @p game and calls its hook; commands become methods of @p control, which has to outlive the activation
        void Activate(IGame& game, ControlService& control);

        /// Takes the methods out of the control service and drops every entry; nothing when no game is activated
        void Clear();

        [[nodiscard]] bool IsActive() const {
            return _control != nullptr;
        }

        /// Registers a command. Refused with one error line: no activated game, a game name that gives no prefix, a command or parameter name that is not
        /// `[A-Za-z0-9_]+` or is repeated, no callback
        void AddCommand(GameToolCommand command);

        /// Registers a panel; refused as a command is, by its id and its draw callback
        void AddPanel(GameToolPanel panel);

        /// Checks @p arguments against the parameters of the command of @p method and runs it: unknown_method, invalid_params, else what the callback returns.
        /// Whether the callback wrote one complete value is the caller's check. The command may switch the game
        [[nodiscard]] std::expected<void, ControlError> Invoke(StringView method, JsonView arguments, JsonWriter& result);

        /// The command of @p method, or null
        [[nodiscard]] IntrusivePtr<GameToolCommandEntry> FindCommand(StringView method) const;

        /// In the order of registration. A callback may change both lists (GetVersion() tells), so no iterator into one is kept across a callback
        [[nodiscard]] const Vector<IntrusivePtr<GameToolCommandEntry>>& GetCommands() const {
            return _commands;
        }

        [[nodiscard]] const Vector<IntrusivePtr<GameToolPanelEntry>>& GetPanels() const {
            return _panels;
        }

        /// The game name as the methods start with it; empty without an activated game
        [[nodiscard]] const String& GetPrefix() const {
            return _prefix;
        }

        /// The name of the activated game; empty without one
        [[nodiscard]] PoolString GetGameName() const {
            return _gameName;
        }

        /// Grows with every activation, clearing and registration; never goes back
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

    private:
        ControlService* _control = nullptr;
        String _prefix;
        PoolString _gameName;
        Vector<IntrusivePtr<GameToolCommandEntry>> _commands;
        Vector<IntrusivePtr<GameToolPanelEntry>> _panels;
        uint64_t _version = 0;
    };

}  // namespace BECore

#endif  // BE_WITH_EDITOR
