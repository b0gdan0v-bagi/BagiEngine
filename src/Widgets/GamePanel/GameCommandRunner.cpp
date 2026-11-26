#include "GameCommandRunner.h"

#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <Games/GameEditorTools.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kFormWidth = 420.0f;
        constexpr StringView kEmptyResult = "null";

        /// One field of the form: the widget of its type, with the parameter's name as its label
        void DrawField(GameCommandFormField& field) {
            if (field.param.type == GameCommandParamType::Boolean) {
                ImGui::Checkbox(field.param.name.c_str(), &field.flag);
            } else {
                ImGui::InputTextWithHint(field.param.name.c_str(), field.param.required ? "required" : "optional", field.text, sizeof(field.text));
            }
            ItemTooltip(field.param.description.empty() ? nullptr : field.param.description.c_str());
        }

    }  // namespace

    void GameCommandRunner::Connect() {
        Subscribe<EditorCommandEvent, &GameCommandRunner::OnCommand>(this);
    }

    void GameCommandRunner::OnCommand(const EditorCommandEvent& event) {
        // An id of the editor's table is never a game's, even when a game's method reads the same
        if (_tools == nullptr || FindEditorCommand(event.commandId) != nullptr) {
            return;
        }
        const StringView id = event.commandId.ToStringView();
        const IntrusivePtr<GameToolCommandEntry> entry = _tools->FindCommand(id);
        if (!entry) {
            return;
        }
        const GameToolCommand& command = entry->command;
        if (command.params.empty()) {
            Run(id, "{}");
            return;
        }
        _form.emplace(PendingForm{.method = entry->method,
                                  .title = command.title,
                                  .summary = command.summary,
                                  .popupLabel = Format("{}###GameCommandForm", command.title),
                                  .error = {},
                                  .version = _tools->GetVersion(),
                                  .form = GameCommandForm(Span<const GameCommandParam>(command.params.data(), command.params.size()))});
    }

    void GameCommandRunner::Run(StringView method, StringView arguments) {
        const IntrusivePtr<GameToolCommandEntry> entry = _tools != nullptr ? _tools->FindCommand(method) : IntrusivePtr<GameToolCommandEntry>();
        if (!entry) {
            return;
        }
        String result;
        JsonWriter writer(result);
        JsonTape tape;
        std::expected<void, ControlError> outcome;
        if (const auto parsed = tape.Parse(arguments); !parsed) {
            outcome = std::unexpected(MakeControlError(ControlErrors::kInvalidParams, String(parsed.error().message)));
        } else {
            outcome = _tools->Invoke(entry->method, JsonView(tape, 0), writer);
        }
        // The control method answers the same failures for the same callback (ControlCall::TryReply)
        if (outcome && writer.GetFailure() != nullptr) {
            outcome = std::unexpected(*writer.GetFailure());
        } else if (outcome && !writer.IsComplete()) {
            outcome = std::unexpected(MakeControlError(ControlErrors::kInternalError, "the command did not write exactly one value"));
        }

        const String& title = entry->command.title;
        if (!outcome) {
            LOG_WARNING("[GameCommand] {} failed: {} ({})", entry->method, outcome.error().message, outcome.error().code);
            const String text = Format("{}: {}", title, outcome.error().message);
            EditorToastEvent::Emit(ToastLevel::Error, StringView(text.data(), text.size()));
            return;
        }
        LOG_INFO("[GameCommand] {}: {}", entry->method, result);
        const String text = Format("{}: {}", title, StringView(result.data(), result.size()) == kEmptyResult ? StringView("done") : StringView(result.data(), result.size()));
        EditorToastEvent::Emit(ToastLevel::Success, StringView(text.data(), text.size()));
    }

    void GameCommandRunner::Draw() {
        if (!_form) {
            return;
        }
        if (_form->openRequested) {
            _form->openRequested = false;
            ImGui::OpenPopup(_form->popupLabel.c_str());
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(kFormWidth, 0.0f));
        if (!ImGui::BeginPopupModal(_form->popupLabel.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            _form.reset();
            return;
        }

        bool close = _tools == nullptr || _tools->GetVersion() != _form->version;
        Optional<String> arguments;
        if (!close) {
            if (!_form->summary.empty()) {
                ImGui::TextWrapped("%s", _form->summary.c_str());
            }
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            Vector<GameCommandFormField>& fields = _form->form.GetFields();
            for (size_t i = 0; i < fields.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                DrawField(fields[i]);
                ImGui::PopID();
            }
            if (!_form->error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, GetImGuiPalette().error);
                ImGui::TextWrapped("%s", _form->error.c_str());
                ImGui::PopStyleColor();
            }
            if (IconTextButton(ICON_LC_PLAY, "Run")) {
                if (auto built = _form->form.BuildArguments(); built) {
                    arguments = std::move(*built);
                    close = true;
                } else {
                    _form->error = std::move(built.error());
                }
            }
            ImGui::SameLine();
            close = IconTextButton(ICON_LC_X, "Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || close;
        }
        if (close) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();

        if (!close) {
            return;
        }
        // The form is gone before the command runs: the command may ask for another one
        const String method = std::move(_form->method);
        _form.reset();
        if (arguments) {
            Run(method, *arguments);
        }
    }

}  // namespace BECore
