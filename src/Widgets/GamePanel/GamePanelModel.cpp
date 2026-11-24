#include "GamePanelModel.h"

#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <Games/GameText.h>
#include <Games/IControllableGame.h>
#include <Games/IGame.h>
#include <Widgets/Console/ConsoleCommandLine.h>

namespace BECore {

    namespace {

        constexpr int kStateIndent = 2;
        constexpr StringView kOk = "ok";

        char ToLowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool ContainsIgnoreCase(StringView text, StringView needle) {
            if (needle.size() > text.size()) {
                return false;
            }
            for (size_t start = 0; start + needle.size() <= text.size(); ++start) {
                size_t i = 0;
                while (i < needle.size() && ToLowerAscii(text[start + i]) == ToLowerAscii(needle[i])) {
                    ++i;
                }
                if (i == needle.size()) {
                    return true;
                }
            }
            return false;
        }

        String FormatError(const ControlError& error) {
            return Format("{}: {}", error.code, error.message);
        }

        /// A value of `key=value`: a string without blanks as itself, anything else as JSON (what ParseConsoleParams reads back)
        void AppendValue(String& out, const Json& value) {
            if (value.is_string()) {
                const auto& text = value.get_ref<const Json::string_t&>();
                if (!text.empty() && text.find_first_of(" \t\"{}[]=") == Json::string_t::npos && !ParseJson(StringView(text.data(), text.size()))) {
                    out.append(text.data(), text.size());
                    return;
                }
            }
            out += DumpJson(value);
        }

    }  // namespace

    String FormatGameActionLabel(const Json& action) {
        const Json* type = FindJsonMember(action, "type");
        if (type == nullptr || !type->is_string()) {
            return DumpJson(action);
        }
        const auto& typeText = type->get_ref<const Json::string_t&>();
        String label(typeText.data(), typeText.size());
        for (auto it = action.begin(); it != action.end(); ++it) {
            if (it.key() == "type") {
                continue;
            }
            label += ' ';
            label.append(it.key().data(), it.key().size());
            label += '=';
            AppendValue(label, it.value());
        }
        return label;
    }

    bool GamePanelModel::Sync(IGame* game) {
        IControllableGame* controllable = game != nullptr ? game->AsControllable() : nullptr;
        const uint64_t stateVersion = controllable != nullptr ? controllable->GetStateVersion() : 0;
        if (_synced && game == _game && controllable == _controllable && stateVersion == _stateVersion) {
            return false;
        }
        const bool gameChanged = !_synced || game != _game;
        _synced = true;
        _game = game;
        _controllable = controllable;
        _stateVersion = stateVersion;
        if (gameChanged) {
            _gameName = game != nullptr ? game->GetName() : PoolString();
            _lastResult.clear();
            _lastResultError = false;
        }
        Rebuild();
        return true;
    }

    void GamePanelModel::Rebuild() {
        ++_version;
        _stateText.clear();
        _actions.clear();
        _methods.clear();

        if (_controllable != nullptr) {
            JsonWriter stateWriter(_stateText, kStateIndent);
            auto written = _controllable->WriteState(stateWriter);
            if (!written || !stateWriter.IsComplete()) {
                _stateText = written ? String("internal_error: the game wrote no complete state") : FormatError(written.error());
            }
            // The action list is a DOM here until the editor's readers move to the tape (the control API stage 5)
            if (const auto actionsText = ActionsText(*_controllable)) {
                auto actions = ParseJson(StringView(*actionsText));
                if (actions && actions->is_array()) {
                    _actions.reserve(actions->size());
                    for (Json& action : *actions) {
                        String label = FormatGameActionLabel(action);
                        _actions.push_back({std::move(label), std::move(action)});
                    }
                }
            }
        }
        RebuildVisible();

        if (_game == nullptr) {
            return;
        }
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypedRef object = TypedRef::Of(*_game, registry);
        for (const TypeInfo* info = object.type; info != nullptr; info = registry.Parent(*info)) {
            for (const MethodDesc& method : info->methods) {
                bool shadowed = false;
                for (const GamePanelMethod& known : _methods) {
                    shadowed = shadowed || known.name == method.name;
                }
                if (shadowed) {
                    continue;
                }
                GamePanelMethod entry;
                entry.name = method.name;
                entry.callable = method.callable;
                entry.hasParams = !method.params.empty();
                entry.signature.append(method.name.data(), method.name.size());
                entry.signature += '(';
                for (size_t i = 0; i < method.params.size(); ++i) {
                    if (i > 0) {
                        entry.signature += ", ";
                    }
                    entry.signature.append(method.params[i].name.data(), method.params[i].name.size());
                }
                entry.signature += ')';
                _methods.push_back(std::move(entry));
            }
        }
    }

    void GamePanelModel::SetFilter(StringView filter) {
        if (StringView(_filter.data(), _filter.size()) == filter) {
            return;
        }
        _filter.assign(filter.data(), filter.size());
        RebuildVisible();
        ++_version;
    }

    void GamePanelModel::RebuildVisible() {
        _visible.clear();
        const StringView filter(_filter.data(), _filter.size());
        for (size_t i = 0; i < _actions.size(); ++i) {
            if (filter.empty() || ContainsIgnoreCase(StringView(_actions[i].label.data(), _actions[i].label.size()), filter)) {
                _visible.push_back(i);
            }
        }
    }

    void GamePanelModel::SetResult(String text, bool isError) {
        _lastResult = std::move(text);
        _lastResultError = isError;
        ++_version;
    }

    void GamePanelModel::Perform(size_t index) {
        if (_controllable == nullptr || index >= _actions.size()) {
            return;
        }
        // A copy: the action list is rebuilt when the version moves, which the call itself may cause on the next Sync
        const String action = DumpJson(_actions[index].action);
        const auto outcome = PerformActionText(*_controllable, action);
        if (outcome) {
            SetResult(*outcome == "null" ? String(kOk) : *outcome, false);
        } else {
            SetResult(FormatError(outcome.error()), true);
        }
    }

    void GamePanelModel::Reset() {
        if (_controllable != nullptr) {
            auto reset = ResetText(*_controllable);
            if (reset) {
                SetResult(String(kOk), false);
            } else {
                SetResult(FormatError(reset.error()), true);
            }
            return;
        }
        if (_game != nullptr) {
            _game->Reset();
            SetResult(String(kOk), false);
        }
    }

    void GamePanelModel::Call(size_t index, StringView arguments) {
        if (_game == nullptr || index >= _methods.size()) {
            return;
        }
        auto params = ParseConsoleParams(arguments);
        if (!params) {
            SetResult(std::move(params.error()), true);
            return;
        }
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        JsonDeserializer reader(*params);
        JsonSerializer writer;
        const auto called = CallMethod(TypedRef::Of(*_game, registry), _methods[index].name, reader, writer, registry);
        if (!called) {
            SetResult(Format("{}: {}", EnumUtils<CallStatus>::ToString(called.error().status), called.error().message), true);
            return;
        }
        const Json written = writer.Take();
        const Json* result = FindJsonMember(written, "result");
        SetResult(result != nullptr ? DumpJson(*result) : String(kOk), false);
    }

}  // namespace BECore
