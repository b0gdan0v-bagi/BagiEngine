#include "FileChangesWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <Generated/FileChangesWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr int kPendingColumns = 4;
        constexpr size_t kLabelSize = 32;

        void TextOf(StringView text) {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }

        void DrawConsumers(const Vector<PoolString>& consumers) {
            for (size_t i = 0; i < consumers.size(); ++i) {
                if (i > 0) {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextUnformatted(", ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                TextOf(consumers[i].ToStringView());
            }
        }

    }  // namespace

    bool FileChangesWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &FileChangesWidget::OnCommand>(this);
        return true;
    }

    void FileChangesWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewFileChanges) {
            _open = !_open;
        }
    }

    void FileChangesWidget::Draw() {}

    void FileChangesWidget::Update() {
        if (!_open) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(640.0f, 320.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("File changes###FileChanges", &_open)) {
            FileWatcher& watcher = CoreManager::GetFileWatcher();
            if (!watcher.IsRunning()) {
                EmptyState(ICON_LC_FILE_DIFF, "The file watcher is not running: set fileWatch to On or Auto in ApplicationConfig.xml");
            } else {
                if (IconTextButton(ICON_LC_REFRESH_CW, "Rescan##fileChanges", "Look at the disk now and list the files that changed since the engine last knew them")) {
                    _model.Rescan(watcher);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(_model.GetPending().empty());
                if (IconTextButton(ICON_LC_CHECK_CHECK, "Apply all##fileChanges", "Deliver every listed change to the files' consumers (a config is read again)")) {
                    _model.ApplyAll(watcher);
                }
                ImGui::EndDisabled();
                DrawPending();
                DrawApplied();
            }
        }
        ImGui::End();
    }

    void FileChangesWidget::DrawPending() {
        if (!_model.HasScanned()) {
            ImGui::TextDisabled("Press Rescan to look for files changed on disk");
            return;
        }
        if (_model.GetPending().empty()) {
            ImGui::TextDisabled("Nothing changed");
            return;
        }
        if (!ImGui::BeginTable("##fileChangesPending", kPendingColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            return;
        }
        PoolString toApply;
        char label[kLabelSize];
        for (size_t i = 0; i < _model.GetPending().size(); ++i) {
            const PendingFileChange& change = _model.GetPending()[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            TextOf(change.virtualPath.ToStringView());
            ImGui::TableNextColumn();
            TextOf(EnumUtils<FileChangeKind>::ToString(change.kind));
            ImGui::TableNextColumn();
            DrawConsumers(change.consumers);
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "##applyRow%zu", i);
            if (IconButton(ICON_LC_CHECK, "Apply this change", label)) {
                toApply = change.virtualPath;
            }
        }
        ImGui::EndTable();
        // After the table: an apply changes the list the loop above walks
        if (!toApply.Empty()) {
            _model.ApplyOne(CoreManager::GetFileWatcher(), toApply);
        }
    }

    void FileChangesWidget::DrawApplied() const {
        if (_model.GetApplied().empty()) {
            return;
        }
        ImGui::SeparatorText("Last apply");
        const ImGuiPalette& palette = GetImGuiPalette();
        for (const AppliedFileChange& change : _model.GetApplied()) {
            ImGui::PushStyleColor(ImGuiCol_Text, change.ok ? palette.success : palette.error);
            TextOf(change.virtualPath.ToStringView());
            ImGui::PopStyleColor();
            if (!change.ok) {
                ImGui::SameLine();
                TextOf(change.error);
            }
        }
    }

}  // namespace BECore
