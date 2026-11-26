#include "WorkspaceSwitcher.h"

#include <Widgets/EditorLayout/EditorPanelRegistry.h>
#include <Widgets/EditorLayout/WorkspaceLayouts.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        String CaptureLayoutText() {
            size_t size = 0;
            const char* text = ImGui::SaveIniSettingsToMemory(&size);
            // SaveIniSettingsToMemory cancels the pending write of imgui.ini
            ImGui::MarkIniSettingsDirty();
            return String(text, size);
        }

        WorkspaceState CaptureScreen() {
            const EditorPanelRegistry& registry = EditorPanelRegistry::GetInstance();
            WorkspaceState shown{CaptureLayoutText(), {}};
            for (const EditorPanelRegistry::Entry& entry : registry.GetEntries()) {
                shown.open.insert(MakePair(entry.id, entry.panel->IsOpen()));
            }
            return shown;
        }

        void ApplyOpenFlags(const WorkspacePanelStates& states) {
            for (const EditorPanelRegistry::Entry& entry : EditorPanelRegistry::GetInstance().GetEntries()) {
                if (const auto found = states.find(entry.id); found != states.end()) {
                    entry.panel->SetOpen(found->second);
                }
            }
        }

    }  // namespace

    WorkspaceSwitcher::WorkspaceSwitcher(Path userDirectory) : _model(std::move(userDirectory)) {
        _model.Load();
    }

    void WorkspaceSwitcher::SwitchTo(EditorWorkspace target) {
        if (target == _model.GetActive()) {
            return;
        }
        // Only what is really on screen: not before the first layout, nor while an earlier change waits
        Optional<WorkspaceState> shown;
        if (_started && !_model.IsPending()) {
            shown = CaptureScreen();
        }
        if (Optional<WorkspaceState> answer = _model.SwitchTo(target, shown ? &*shown : nullptr)) {
            SetPending(std::move(*answer));
            _model.Save();
        }
    }

    void WorkspaceSwitcher::Reset() {
        SetPending(_model.Reset());
        _model.Save();
    }

    void WorkspaceSwitcher::LoadLayout(String text) {
        _model.LoadLayout(StringView(text));
        // A switch that still waits keeps its panel flags and gets this layout
        WorkspaceState state = _pending ? std::move(*_pending) : WorkspaceState{};
        state.layout = std::move(text);
        SetPending(std::move(state));
        _model.Save();
    }

    void WorkspaceSwitcher::SetPending(WorkspaceState state) {
        _pending = std::move(state);
        _buildReady = false;
        _checkTree = false;
    }

    void WorkspaceSwitcher::ApplyPending() {
        // ImGui reads imgui.ini in its first NewFrame, which would replace a layout loaded before it
        if (!_started || !_pending) {
            return;
        }
        ApplyOpenFlags(_pending->open);
        if (_pending->layout.empty()) {
            _buildReady = true;
        } else {
            ImGui::LoadIniSettingsFromMemory(_pending->layout.c_str(), _pending->layout.size());
            // imgui.ini follows the loaded layout
            ImGui::MarkIniSettingsDirty();
            _checkTree = true;
            _model.MarkApplied();
        }
        _pending.reset();
    }

    void WorkspaceSwitcher::Update(ImGuiID dockspaceId, ImVec2 size) {
        if (!_started) {
            Start(dockspaceId, size);
            return;
        }
        if (_buildReady) {
            _buildReady = false;
            BuildWorkspaceLayout(dockspaceId, size, _model.GetActive());
            ImGui::MarkIniSettingsDirty();
            _model.MarkApplied();
        } else if (_checkTree) {
            _checkTree = false;
            if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
                BuildWorkspaceLayout(dockspaceId, size, _model.GetActive());
            }
        }
        NoteOpenFlags();
    }

    void WorkspaceSwitcher::Start(ImGuiID dockspaceId, ImVec2 size) {
        _started = true;
        const WorkspaceState* const stored = _model.FindStored(_model.GetActive());
        if (stored != nullptr) {
            ApplyOpenFlags(stored->open);
        }
        // A change asked before the first frame comes at the next; else the tree of imgui.ini is what the last session showed
        if (_pending || ImGui::DockBuilderGetNode(dockspaceId) != nullptr) {
            return;
        }
        if (stored != nullptr) {
            SetPending(_model.Restore());
        } else {
            BuildWorkspaceLayout(dockspaceId, size, _model.GetActive());
        }
    }

    void WorkspaceSwitcher::NoteOpenFlags() {
        const EditorPanelRegistry& registry = EditorPanelRegistry::GetInstance();
        bool changed = false;
        for (const EditorPanelRegistry::Entry& entry : registry.GetEntries()) {
            // Of two panels of one id only the first counts, or the flag would change in every frame
            if (registry.Find(entry.id) == entry.panel) {
                changed = _model.NoteOpen(entry.id, entry.panel->IsOpen()) || changed;
            }
        }
        if (changed) {
            _model.Save();
        }
    }

    void WorkspaceSwitcher::Shutdown() {
        if (!_started || _model.IsPending()) {
            return;
        }
        _model.StoreLayout(StringView(CaptureLayoutText()));
        _model.Save();
    }

}  // namespace BECore
