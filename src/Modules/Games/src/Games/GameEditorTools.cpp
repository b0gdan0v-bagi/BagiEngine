#include "GameEditorTools.h"

#if BE_WITH_EDITOR

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Memory/MemTag.h>
#include <EASTL/algorithm.h>
#include <Games/IGame.h>

namespace BECore {

    namespace {

        bool IsNameChar(char symbol) {
            return (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z') || (symbol >= '0' && symbol <= '9') || symbol == '_';
        }

        /// A command name, a panel id or a parameter name. No dot, so `<prefix>.<name>` has exactly one
        bool IsToolName(StringView name) {
            return !name.empty() && eastl::all_of(name.begin(), name.end(), IsNameChar);
        }

        /// No engine method has a dot in its name, so a prefixed method shadows none of them
        String MakePrefix(StringView gameName) {
            String prefix(gameName);
            for (char& symbol : prefix) {
                if (symbol >= 'A' && symbol <= 'Z') {
                    symbol = static_cast<char>(symbol - 'A' + 'a');
                } else if (!IsNameChar(symbol)) {
                    symbol = '_';
                }
            }
            return prefix;
        }

        /// The type as `list_methods` names it (ControlParamInfo::type)
        StringView ControlTypeName(GameCommandParamType type) {
            switch (type) {
                case GameCommandParamType::String:
                    return "string";
                case GameCommandParamType::Integer:
                    return "integer";
                case GameCommandParamType::Number:
                    return "number";
                case GameCommandParamType::Boolean:
                    return "boolean";
                default:
                    return "any";
            }
        }

        bool HasDeclaredType(JsonView arguments, const GameCommandParam& param) {
            switch (param.type) {
                case GameCommandParamType::String:
                    return GetParamString(arguments, param.name).has_value();
                case GameCommandParamType::Integer:
                    return GetParamInt(arguments, param.name).has_value();
                case GameCommandParamType::Number:
                    return GetParamNumber(arguments, param.name).has_value();
                case GameCommandParamType::Boolean:
                    return GetParamBool(arguments, param.name).has_value();
                default:
                    return true;
            }
        }

        /// The one way into a callback, for a control request and for Invoke()
        std::expected<void, ControlError> RunCommand(GameToolCommandEntry& entry, JsonView arguments, JsonWriter& result) {
            for (const GameCommandParam& param : entry.command.params) {
                if (!FindParam(arguments, param.name)) {
                    if (param.required) {
                        return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' is required", param.name)));
                    }
                    continue;
                }
                if (!HasDeclaredType(arguments, param)) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be of type {}", param.name, ControlTypeName(param.type))));
                }
            }
            BE_MEM_TAG(Game);
            return entry.command.run(arguments, result);
        }

        /// Why @p command cannot be registered, whatever else is; empty when nothing is wrong with it
        StringView FindCommandFault(const GameToolCommand& command) {
            if (!IsToolName(command.name)) {
                return "its name is not [A-Za-z0-9_]+";
            }
            if (!command.run) {
                return "it has no callback";
            }
            for (auto param = command.params.begin(); param != command.params.end(); ++param) {
                if (!IsToolName(param->name)) {
                    return "a parameter name is not [A-Za-z0-9_]+";
                }
                if (eastl::any_of(command.params.begin(), param, [param](const GameCommandParam& earlier) { return earlier.name == param->name; })) {
                    return "a parameter name is repeated";
                }
            }
            return {};
        }

    }  // namespace

    void GameEditorTools::Activate(IGame& game, ControlService& control) {
        Clear();
        _control = &control;
        _gameName = game.GetName();
        _prefix = MakePrefix(_gameName.ToStringView());
        ++_version;
        BE_MEM_TAG(Game);
        game.RegisterEditorTools(*this);
    }

    void GameEditorTools::Clear() {
        if (_control == nullptr) {
            return;
        }
        ControlService& control = *std::exchange(_control, nullptr);
        // Out of the registry before anything dies: the destructor of a callback's capture may reach back here
        const Vector<IntrusivePtr<GameToolCommandEntry>> commands = std::move(_commands);
        const Vector<IntrusivePtr<GameToolPanelEntry>> panels = std::move(_panels);
        _prefix.clear();
        _gameName = PoolString();
        ++_version;
        for (const IntrusivePtr<GameToolCommandEntry>& entry : commands) {
            control.UnregisterHandler(entry->method);
        }
    }

    void GameEditorTools::AddCommand(GameToolCommand command) {
        StringView fault = FindCommandFault(command);
        String method = Format("{}.{}", _prefix, command.name);
        if (_control == nullptr) {
            fault = "no game is activated";
        } else if (_prefix.empty()) {
            fault = "the game has no name to make a prefix of";
        } else if (fault.empty() && FindCommand(method)) {
            fault = "the command is registered already";
        }
        if (!fault.empty()) {
            LOG_ERROR("[GameEditorTools] the command '{}' is refused: {}", command.name, fault);
            return;
        }

        if (command.title.empty()) {
            command.title = command.name;
        }
        ControlMethodInfo info;
        info.summary = command.summary;
        info.params.reserve(command.params.size());
        for (const GameCommandParam& param : command.params) {
            info.params.push_back(ControlParamInfo{param.name, String(ControlTypeName(param.type)), param.required, param.description});
        }

        IntrusivePtr<GameToolCommandEntry> entry = New<GameToolCommandEntry>();
        entry->method = std::move(method);
        entry->command = std::move(command);
        _commands.push_back(entry);
        _control->DescribeMethod(entry->method, std::move(info));
        _control->RegisterHandler(entry->method, [entry](ControlCall& call) {
            // The capture holds the entry until the call returns, even when the command unregisters its own method (a switch of the game)
            call.TryReply([&entry, &call](JsonWriter& result) { return RunCommand(*entry, call.Params(), result); });
        });
        ++_version;
    }

    void GameEditorTools::AddPanel(GameToolPanel panel) {
        StringView fault;
        if (_control == nullptr) {
            fault = "no game is activated";
        } else if (_prefix.empty()) {
            fault = "the game has no name to make a prefix of";
        } else if (!IsToolName(panel.id)) {
            fault = "its id is not [A-Za-z0-9_]+";
        } else if (!panel.draw) {
            fault = "it has no draw callback";
        } else if (eastl::any_of(_panels.begin(), _panels.end(), [&panel](const IntrusivePtr<GameToolPanelEntry>& entry) { return entry->panel.id == panel.id; })) {
            fault = "the panel is registered already";
        }
        if (!fault.empty()) {
            LOG_ERROR("[GameEditorTools] the panel '{}' is refused: {}", panel.id, fault);
            return;
        }

        if (panel.title.empty()) {
            panel.title = panel.id;
        }
        IntrusivePtr<GameToolPanelEntry> entry = New<GameToolPanelEntry>();
        entry->panel = std::move(panel);
        _panels.push_back(std::move(entry));
        ++_version;
    }

    std::expected<void, ControlError> GameEditorTools::Invoke(StringView method, JsonView arguments, JsonWriter& result) {
        const IntrusivePtr<GameToolCommandEntry> entry = FindCommand(method);
        if (!entry) {
            return std::unexpected(MakeControlError(ControlErrors::kUnknownMethod, Format("unknown method '{}'", ShownClientText(method))));
        }
        return RunCommand(*entry, arguments, result);
    }

    IntrusivePtr<GameToolCommandEntry> GameEditorTools::FindCommand(StringView method) const {
        for (const IntrusivePtr<GameToolCommandEntry>& entry : _commands) {
            if (entry->method == method) {
                return entry;
            }
        }
        return {};
    }

}  // namespace BECore

#endif  // BE_WITH_EDITOR
