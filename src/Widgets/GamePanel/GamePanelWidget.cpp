#include "GamePanelWidget.h"

#include <Games/GameManager.h>
#include <Games/IGame.h>
#include <Generated/GamePanelWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ImGuiPropertyVisitor.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kDefaultWidth = 420.0f;
        constexpr float kDefaultHeight = 560.0f;
        constexpr float kFilterWidthInEm = 14.0f;
        /// The state area shows this many lines before it scrolls
        constexpr float kStateVisibleLines = 12.0f;
        /// The action list shows this many rows before it scrolls
        constexpr float kActionsVisibleRows = 10.0f;

        void TextColored(const ImVec4& color, const char* text) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        }

    }  // namespace

    bool GamePanelWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &GamePanelWidget::OnCommand>(this);
        return true;
    }

    void GamePanelWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewGame) {
            _open = !_open;
        }
    }

    void GamePanelWidget::Draw() {}

    void GamePanelWidget::Update() {
        if (!_open) {
            return;
        }
        _model.Sync(GameManager::GetInstance().GetActive());
        if (_argumentsFor != _model.GetGame() || _arguments.size() != _model.GetMethods().size()) {
            _argumentsFor = _model.GetGame();
            _arguments.clear();
            _arguments.resize(_model.GetMethods().size());
        }

        ImGui::SetNextWindowSize(ImVec2(kDefaultWidth, kDefaultHeight), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Game###GamePanel", &_open)) {
            if (_model.GetGame() == nullptr) {
                EmptyState(ICON_LC_GAMEPAD_2, "No game is active");
            } else {
                DrawHeader();
                DrawLastResult();
                DrawActions();
                DrawFields();
                DrawMethods();
                DrawState();
            }
        }
        ImGui::End();
    }

    void GamePanelWidget::DrawHeader() {
        ImGui::TextUnformatted(ICON_LC_GAMEPAD_2);
        ImGui::SameLine();
        ImGui::TextUnformatted(_model.GetGameName().CStr());
        ImGui::SameLine();
        if (_model.IsControllable()) {
            ScopedMonoFont mono;
            ImGui::TextDisabled("v%llu", static_cast<unsigned long long>(_model.GetStateVersion()));
            ItemTooltip("The state version: it grows with every change a controller has to see");
        } else {
            ImGui::TextDisabled("(not controllable)");
            ItemTooltip("The game hands out no IControllableGame: it has no state or actions, only its reflected fields and methods");
        }
        ImGui::SameLine();
        if (IconTextButton(ICON_LC_ROTATE_CCW, "Reset##gameReset", "Start the game over (IControllableGame::Reset with no options, or IGame::Reset)")) {
            _model.Reset();
        }
    }

    void GamePanelWidget::DrawLastResult() {
        const String& result = _model.GetLastResult();
        if (result.empty()) {
            return;
        }
        const ImGuiPalette& palette = GetImGuiPalette();
        ImGui::PushTextWrapPos(0.0f);
        TextColored(_model.IsLastResultError() ? palette.error : palette.success, result.c_str());
        ImGui::PopTextWrapPos();
    }

    void GamePanelWidget::DrawActions() {
        if (!_model.IsControllable() || !ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * kFilterWidthInEm);
        if (ImGui::InputTextWithHint("##gameActionFilter", ICON_LC_FILTER " Filter actions", _filterBuffer, sizeof(_filterBuffer))) {
            _model.SetFilter(StringView(_filterBuffer));
        }
        ItemTooltip("Actions whose label holds this (upper and lower case alike)");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu", _model.GetVisibleActions().size(), _model.GetActions().size());

        const Vector<size_t>& visible = _model.GetVisibleActions();
        if (visible.empty()) {
            ImGui::TextDisabled(_model.GetActions().empty() ? "No action is legal now" : "No action matches the filter");
            return;
        }
        const float rows = static_cast<float>(visible.size()) < kActionsVisibleRows ? static_cast<float>(visible.size()) : kActionsVisibleRows;
        const float height = rows * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
        Optional<size_t> clicked;
        if (ImGui::BeginChild("##gameActions", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()), ImGui::GetFrameHeightWithSpacing());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const size_t index = visible[static_cast<size_t>(row)];
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Button(_model.GetActions()[index].label.c_str())) {
                        clicked = index;
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        // After the list: performing rebuilds the actions on the next Sync, never in the middle of the walk
        if (clicked.has_value()) {
            _model.Perform(*clicked);
        }
    }

    void GamePanelWidget::DrawFields() {
        if (!ImGui::CollapsingHeader("Fields", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        ImGui::PushID("gameFields");
        ImGuiPropertyVisitor visitor;
        _model.GetGame()->AcceptPropertyVisitor(visitor);
        ImGui::PopID();
    }

    void GamePanelWidget::DrawMethods() {
        const Vector<GamePanelMethod>& methods = _model.GetMethods();
        if (methods.empty() || !ImGui::CollapsingHeader("Methods", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        Optional<size_t> clicked;
        for (size_t i = 0; i < methods.size(); ++i) {
            const GamePanelMethod& method = methods[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginDisabled(!method.callable);
            if (IconTextButton(ICON_LC_PLAY, method.signature.c_str(), method.callable ? "Call the method" : "Not callable: a parameter or the result cannot be read or written")) {
                clicked = i;
            }
            ImGui::EndDisabled();
            if (method.hasParams && i < _arguments.size()) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##arguments", "name=value ... or {\"name\": value}", _arguments[i].text, sizeof(_arguments[i].text));
                ItemTooltip("The arguments by their parameter names, as in the Console");
            }
            ImGui::PopID();
        }
        if (clicked.has_value()) {
            const StringView arguments = *clicked < _arguments.size() ? StringView(_arguments[*clicked].text) : StringView();
            _model.Call(*clicked, arguments);
        }
    }

    void GamePanelWidget::DrawState() {
        if (!_model.IsControllable() || !ImGui::CollapsingHeader("State")) {
            return;
        }
        if (IconTextButton(ICON_LC_COPY, "Copy##gameStateCopy", "Copy the state as JSON")) {
            ImGui::SetClipboardText(_model.GetStateText().c_str());
        }
        const float height = kStateVisibleLines * ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("##gameState", ImVec2(0.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
            ScopedMonoFont mono;
            const String& text = _model.GetStateText();
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }
        ImGui::EndChild();
    }

}  // namespace BECore
