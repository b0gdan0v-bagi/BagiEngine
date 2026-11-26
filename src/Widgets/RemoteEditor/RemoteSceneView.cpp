#include "RemoteSceneView.h"

#include <EASTL/algorithm.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <Widgets/RemoteEditor/RemoteHierarchySource.h>
#include <Widgets/RemoteEditor/RemoteSession.h>
#include <Widgets/SceneEditor/ComponentListView.h>
#include <Widgets/SceneEditor/ImGuiPropertyVisitor.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kHierarchyWidth = 300.0f;

        const RemoteTreeNode* SelectedNode(const RemoteSession& session) {
            const Optional<String>& selection = session.GetSelection();
            return selection ? session.GetTree().Find(*selection) : nullptr;
        }

        bool HasUnsavedEdits(RemoteSession& session) {
            return session.GetState() == RemoteSessionState::Attached && (session.HasUnsavedWrites() || session.GetWrites().HasUnanswered());
        }

        /// The components of a node of the tree: the headers from the tree, the fields from what was last read
        class RemoteComponentList final : public IComponentListSource {
        public:
            RemoteComponentList(RemoteSession& session, const RemoteTreeNode& node) : _session(session), _node(node) {}

            [[nodiscard]] size_t GetCount() const override {
                return _node.components.size();
            }

            [[nodiscard]] const char* GetTypeName(size_t index) const override {
                return _node.components[index].c_str();
            }

            [[nodiscard]] bool IsReadOnly() const override {
                return _session.GetState() != RemoteSessionState::Attached;
            }

            [[nodiscard]] bool IsRemovable() const override {
                return false;
            }

            void DrawFields(size_t index) override {
                RemoteComponent* const component = FindRead(index);
                if (component == nullptr) {
                    ImGui::TextDisabled("Not read");
                    return;
                }
                ImGuiPropertyVisitor visitor;
                component->Walk(_session.GetSchema(), _session.GetWrites(), visitor);
            }

        private:
            /// The read component of the header @p index. The tree may be newer than the components, so one of another type or another namesake is not it
            [[nodiscard]] RemoteComponent* FindRead(size_t index) const {
                const Span<RemoteComponent> read = _session.GetComponents();
                if (index >= read.size()) {
                    return nullptr;
                }
                RemoteComponent& component = read[index];
                const auto type = _node.components.begin() + index;
                const size_t namesake = static_cast<size_t>(eastl::count(_node.components.begin(), type, *type));
                const bool same = component.GetType() == StringView(*type) && component.GetIndex() == namesake && component.GetNode() == StringView(_node.path);
                return same ? &component : nullptr;
            }

            RemoteSession& _session;
            const RemoteTreeNode& _node;
        };

    }  // namespace

    void RemoteSceneView::Reset() {
        _hierarchy = HierarchyState{};
        _link.Reset();
        _save.Reset();
        _announced.reset();
        _reveal.reset();
    }

    bool RemoteSceneView::TakeReveal(RemoteSession& session) {
        Optional<String> reveal = session.TakeReveal();
        if (!reveal) {
            return false;
        }
        _reveal = std::move(reveal);
        return true;
    }

    void RemoteSceneView::ReleaseTypedText(RemoteSession& session) {
        if (!ImGui::IsAnyItemActive()) {
            session.GetWrites().ReleaseText();
        }
    }

    void RemoteSceneView::Draw(RemoteSession& session, StringView toolbarAsset, StringView inspectorAsset) {
        PanelBarsRegistry::Draw(toolbarAsset);
        DrawLink(session);
        DrawSave(session);

        // A link that is not up takes no click, a selection included: the components of another node could not be read
        ImGui::BeginDisabled(session.GetState() != RemoteSessionState::Attached);

        ImGui::BeginChild("##hierarchy", ImVec2(kHierarchyWidth, 0.0f), true);
        RemoteHierarchySource source(session);
        if (_reveal) {
            if (const Optional<HierarchyRow> row = source.Reveal(*_reveal)) {
                HierarchyPanel::ClearHidingFilter(source, _hierarchy, *row);
            }
            _reveal.reset();
        }
        HierarchyPanel::Draw(source, _hierarchy);
        ImGui::EndChild();
        AnnounceSelection(session);

        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0.0f, 0.0f), true);
        if (!PanelBarsRegistry::Draw(inspectorAsset)) {
            DrawComponents(session);
        }
        ImGui::EndChild();

        ImGui::EndDisabled();
    }

    void RemoteSceneView::DrawComponents(RemoteSession& session) {
        if (const RemoteTreeNode* const node = SelectedNode(session)) {
            RemoteComponentList list(session, *node);
            // Nothing is removable, so no removal is asked for
            static_cast<void>(DrawComponentList(list));
        }
    }

    SceneInspectedNode RemoteSceneView::Inspect(RemoteSession& session) {
        SceneInspectedNode inspected;
        inspected.hasScene = true;
        inspected.nameReadOnly = true;
        inspected.layerReadOnly = session.GetState() != RemoteSessionState::Attached;
        inspected.structureReadOnly = true;
        inspected.dirty = HasUnsavedEdits(session);
        if (const Optional<RemoteRefusal>& refusal = session.GetWrites().GetRefusal()) {
            inspected.refusalCode = refusal->code;
            inspected.refusalMessage = refusal->message;
        }
        if (const RemoteTreeNode* const node = SelectedNode(session)) {
            inspected.hasSelection = true;
            inspected.selectionCount = 1;
            inspected.name = node->name;
            inspected.layer = node->layer;
            if (const PropertyValue* const edited = session.GetWrites().Find({node->path})) {
                if (const int64_t* const layer = std::get_if<int64_t>(edited)) {
                    inspected.layer = static_cast<int32_t>(*layer);
                }
            }
        }
        return inspected;
    }

    void RemoteSceneView::Save(RemoteSession& session) {
        if (HasUnsavedEdits(session)) {
            session.RequestSave();
        }
    }

    void RemoteSceneView::SetLayer(RemoteSession& session, int32_t layer) {
        const RemoteTreeNode* const node = SelectedNode(session);
        if (node != nullptr && session.GetState() == RemoteSessionState::Attached) {
            session.GetWrites().Edit({node->path}, PropertyValue{int64_t{layer}}, RemoteEditKind::Value);
        }
    }

    void RemoteSceneView::DrawLink(const RemoteSession& session) {
        _link.Update(session);
        const bool troubled = _link.IsTroubled();
        ImGui::PushStyleColor(ImGuiCol_Text, troubled ? GetImGuiPalette().warning : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s %s", troubled ? ICON_LC_TRIANGLE_ALERT : ICON_LC_PLUG, _link.GetText().c_str());
        ImGui::PopStyleColor();
    }

    void RemoteSceneView::DrawSave(const RemoteSession& session) {
        _save.Update(session.GetSaveStatus());
        if (_save.GetText().empty()) {
            return;
        }
        const bool troubled = _save.IsTroubled();
        ImGui::PushStyleColor(ImGuiCol_Text, troubled ? GetImGuiPalette().warning : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s %s", troubled ? ICON_LC_TRIANGLE_ALERT : ICON_LC_SAVE, _save.GetText().c_str());
        ImGui::PopStyleColor();
    }

    void RemoteSceneView::AnnounceSelection(const RemoteSession& session) {
        if (_announced == session.GetSelection()) {
            return;
        }
        _announced = session.GetSelection();
        SceneNodeSelectedEvent::Emit(nullptr, nullptr);
    }

}  // namespace BECore
