#include "GamePanelModel.h"

#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/Control/JsonTape.h>
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

        constexpr StringView kKeyType = "type";
        constexpr StringView kKeyResult = "result";
        constexpr StringView kBareStopChars = " \t\"{}[]=";

        /// A value of `key=value`: a string without blanks as itself, anything else as its text (what ParseConsoleParams reads back)
        void AppendValue(String& out, JsonView value) {
            if (value.IsString()) {
                String text;
                value.ReadText(text);
                JsonTape probe;
                if (!text.empty() && StringView(text).find_first_of(kBareStopChars) == StringView::npos && !probe.Parse(StringView(text))) {
                    out += text;
                    return;
                }
            }
            const StringView raw = value.Raw();
            out.append(raw.data(), raw.size());
        }

        struct LabelMember {
            String key;
            JsonView value;
            size_t order = 0;  ///< the place in the document: of equal keys the first counts
        };

    }  // namespace

    String FormatGameActionLabel(JsonView action) {
        const Optional<JsonView> typeView = action.Find(kKeyType);
        String label;
        if (!typeView || !typeView->ReadText(label)) {
            const StringView raw = action.Raw();
            return String(raw.data(), raw.size());
        }
        // The DOM kept its keys sorted; a rebuild is on a state change only, never per frame
        Vector<LabelMember> members;
        for (const JsonMember member : action.Members()) {
            LabelMember entry{String(), member.value, members.size()};
            member.key.ReadText(entry.key);
            if (entry.key != kKeyType) {
                members.push_back(std::move(entry));
            }
        }
        eastl::sort(members.begin(), members.end(), [](const LabelMember& a, const LabelMember& b) { return a.key != b.key ? a.key < b.key : a.order < b.order; });
        for (size_t i = 0; i < members.size(); ++i) {
            if (i > 0 && members[i].key == members[i - 1].key) {
                continue;  // of equal keys the first counts
            }
            label += ' ';
            label += members[i].key;
            label += '=';
            AppendValue(label, members[i].value);
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
            // A rebuild runs when the state version moved, so a tape of its own is no cost per frame
            if (const auto actionsText = ActionsText(*_controllable)) {
                JsonTape tape;
                if (tape.Parse(StringView(*actionsText))) {
                    const JsonView actions(tape, 0);
                    _actions.reserve(actions.Size());
                    for (const JsonView action : actions.Items()) {
                        const StringView raw = action.Raw();
                        _actions.push_back({FormatGameActionLabel(action), String(raw.data(), raw.size())});
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
        const String action = _actions[index].action;
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
        JsonDeserializer reader;
        reader.Load(StringView(*params));
        String resultText;
        JsonWriter resultWriter(resultText);
        JsonStreamSerializer writer(resultWriter);
        const auto called = CallMethod(TypedRef::Of(*_game, registry), _methods[index].name, reader, writer, registry);
        if (!called) {
            SetResult(Format("{}: {}", EnumUtils<CallStatus>::ToString(called.error().status), called.error().message), true);
            return;
        }
        writer.Finish();

        // The result is the member `result` of the written object, which a method that returns nothing does not write
        JsonTape resultTape;
        if (!resultWriter.IsComplete() || !resultTape.Parse(resultText)) {
            SetResult(String("internal_error: the result of the method cannot be written"), true);
            return;
        }
        const Optional<JsonView> result = JsonView(resultTape, 0).Find(kKeyResult);
        const StringView shown = result ? result->Raw() : kOk;
        SetResult(String(shown.data(), shown.size()), false);
    }

}  // namespace BECore
