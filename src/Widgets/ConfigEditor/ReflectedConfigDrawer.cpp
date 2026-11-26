#include "ReflectedConfigDrawer.h"

#include <EASTL/algorithm.h>
#include <Widgets/ConfigEditor/ReflectedConfigDocument.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ImGuiPropertyVisitor.h>
#include <imgui.h>

namespace BECore {

    namespace {

        /// The visitor of the root object: the root's factory lists are empty in it and drawn element by element after it
        class RootFieldsVisitor : public ImGuiPropertyVisitor {
        public:
            explicit RootFieldsVisitor(const Vector<ReflectedConfigDocument::List>& lists) : _lists(lists) {}

            void BeginField(const FieldAttributes& attributes) override {
                ++_fieldDepth;
                ImGuiPropertyVisitor::BeginField(attributes);
            }

            void EndField() override {
                --_fieldDepth;
                ImGuiPropertyVisitor::EndField();
            }

            // A list of pointers is visited as a compound named after its field; the depth tells a field of the root from a nested one
            bool BeginCompound(StringView name) override {
                const bool isRootList = _fieldDepth == 1 && eastl::any_of(_lists.begin(), _lists.end(), [name](const ReflectedConfigDocument::List& list) { return list.name == name; });
                return !isRootList && ImGuiPropertyVisitor::BeginCompound(name);
            }

        private:
            const Vector<ReflectedConfigDocument::List>& _lists;
            int _fieldDepth = 0;
        };

        /// What the walk was asked to do to an element
        struct ElementRequest {
            size_t list = 0;
            size_t element = 0;
            const TypeInfo* type = nullptr;  // the type to make the element; null: set `enabled`
            bool enabled = false;
        };

    }  // namespace

    void ReflectedConfigDrawer::Prepare(const ReflectedConfigDocument& document) {
        _refusal.clear();
        _lists.clear();
        for (const ReflectedConfigDocument::List& list : document.GetLists()) {
            ListLabels& labels = _lists.emplace_back();
            labels.title = String(list.name);
            for (const TypeInfo* type : list.types) {
                labels.typeNames.push_back(String(type->Name()));
            }
            for (size_t i = 0; i < list.elements.size(); ++i) {
                // The id leaves the type out, so a header stays open or closed through a change of it
                labels.headers.push_back(Format("{}[{}]: {}###element", list.name, i, list.elements[i].type->Name()));
            }
        }
    }

    void ReflectedConfigDrawer::Draw(ReflectedConfigDocument& document) {
        if (!_refusal.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, GetImGuiPalette().warning);
            ImGui::TextWrapped("%s", _refusal.c_str());
            ImGui::PopStyleColor();
        }

        bool edited = false;
        if (const ReflectedConfigDocument::Object* root = document.GetRoot()) {
            ImGui::PushID("root");
            RootFieldsVisitor visitor(document.GetLists());
            edited = root->type->acceptVisitor(root->object.Get(), visitor);
            ImGui::PopID();
        }

        Optional<ElementRequest> request;
        const Vector<ReflectedConfigDocument::List>& lists = document.GetLists();
        for (size_t list = 0; list < lists.size(); ++list) {
            const ListLabels& labels = _lists[list];
            ImGui::PushID(static_cast<int>(list));
            ImGui::SeparatorText(labels.title.c_str());
            for (size_t i = 0; i < lists[list].elements.size(); ++i) {
                const ReflectedConfigDocument::Element& element = lists[list].elements[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::CollapsingHeader(labels.headers[i].c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Indent();
                    const Vector<const TypeInfo*>& types = lists[list].types;
                    const size_t current = static_cast<size_t>(eastl::find(types.begin(), types.end(), element.type) - types.begin());
                    if (ImGui::BeginCombo("type", labels.typeNames[current].c_str())) {
                        for (size_t type = 0; type < types.size(); ++type) {
                            const bool selected = type == current;
                            if (ImGui::Selectable(labels.typeNames[type].c_str(), selected) && !selected) {
                                request = ElementRequest{list, i, types[type]};
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    bool enabled = element.enabled;
                    if (!element.hasEnabledField && ImGui::Checkbox("enabled", &enabled)) {
                        request = ElementRequest{list, i, nullptr, enabled};
                    }
                    ImGuiPropertyVisitor visitor;
                    edited |= element.type->acceptVisitor(element.object.Get(), visitor);
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }

        if (edited) {
            document.NoteChange();
        }
        if (!request) {
            return;
        }
        if (request->type == nullptr) {
            document.SetEnabled(request->list, request->element, request->enabled);
        } else if (const auto changed = document.ChangeType(request->list, request->element, *request->type); changed) {
            Prepare(document);
        } else {
            _refusal = Format("{}[{}] cannot become {}: {}", lists[request->list].name, request->element, request->type->Name(), changed.error());
        }
    }

}  // namespace BECore
