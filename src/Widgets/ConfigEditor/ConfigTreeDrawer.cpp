#include "ConfigTreeDrawer.h"

#include <BECore/Memory/ArenaAllocator.h>
#include <EASTL/algorithm.h>
#include <EASTL/iterator.h>
#include <Widgets/ConfigEditor/StagedChanges.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {
        // Room for a path of a few levels, so a key is built in one block of the scratch arena
        constexpr size_t kPathReserve = 128;

        /// A comment before the first element must not turn a branch into a leaf
        bool HasElementChild(pugi::xml_node node) {
            for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element) {
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    const char* ConfigTreeDrawer::InputValue(StringView key, StringView value) {
        auto it = _textBuffers.find(key);
        if (it == _textBuffers.end()) {
            it = _textBuffers.emplace(String(key), TextBuffer{}).first;
            eastl::copy_n(value.data(), eastl::min(value.size(), kTextBufferSize - 1), it->second.data());
        }
        // The key scopes the fixed label, so no label is formatted per frame
        ImGui::PushID(key.data(), key.data() + key.size());
        ImGui::SetNextItemWidth(-1.0f);
        const bool entered = ImGui::InputText("##value", it->second.data(), it->second.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopID();
        return entered ? it->second.data() : nullptr;
    }

    void ConfigTreeDrawer::Draw(const XmlNode& root) {
        const ScratchScope scratch;
        ArenaString rootPath;
        rootPath.reserve(kPathReserve);
        rootPath.append(root.Name().data(), root.Name().size());
        RenderXmlNode(root.GetPugiNode(), rootPath);
    }

    void ConfigTreeDrawer::RenderXmlNode(pugi::xml_node node, const ArenaString& nodePath) {
        if (node.type() != pugi::node_element) {
            return;
        }

        const StringView path(nodePath.data(), nodePath.size());
        const bool hasChildren = HasElementChild(node);
        const bool hasAttributes = static_cast<bool>(node.first_attribute());

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        const bool nodeOpen = ImGui::TreeNodeEx(nodePath.c_str(), flags, "%s", node.name());

        if (nodeOpen) {
            if (hasAttributes) {
                ImGui::Indent(8.0f);
                if (ImGui::BeginTable("##attrs", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    for (auto attr = node.first_attribute(); attr; attr = attr.next_attribute()) {
                        ImGui::TableNextRow();

                        const ScratchScope attrScope;
                        ArenaString attrKey;
                        attrKey.reserve(kPathReserve);
                        FormatTo(eastl::back_inserter(attrKey), "{}@{}", path, attr.name());
                        const StringView attrKeyView(attrKey.data(), attrKey.size());
                        const bool isStaged = _staged.Has(attrKeyView);

                        ImGui::TableSetColumnIndex(0);
                        if (isStaged) {
                            ImGui::TextColored(GetImGuiPalette().warning, "%s*", attr.name());
                        } else {
                            ImGui::TextUnformatted(attr.name());
                        }

                        ImGui::TableSetColumnIndex(1);

                        const String* stagedPtr = _staged.Get(attrKeyView);
                        const StringView currentValue = stagedPtr ? StringView{*stagedPtr} : StringView{attr.value()};
                        if (const char* entered = InputValue(attrKeyView, currentValue)) {
                            _staged.Set(attrKeyView, attr, entered);
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::Unindent(8.0f);
            }

            size_t childIndex = 0;
            for (auto child = node.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element) {
                    const ScratchScope childScope;
                    ArenaString childPath;
                    childPath.reserve(kPathReserve);
                    FormatTo(eastl::back_inserter(childPath), "{}/{}[{}]", path, child.name(), childIndex);
                    RenderXmlNode(child, childPath);
                    ++childIndex;
                } else if (child.type() == pugi::node_pcdata) {
                    const ScratchScope textScope;
                    ArenaString textKeyString;
                    textKeyString.reserve(kPathReserve);
                    FormatTo(eastl::back_inserter(textKeyString), "{}#text", path);
                    const StringView textKey(textKeyString.data(), textKeyString.size());
                    const String* stagedPtr = _staged.Get(textKey);
                    const StringView currentValue = stagedPtr ? StringView{*stagedPtr} : StringView{child.value()};
                    if (const char* entered = InputValue(textKey, currentValue)) {
                        _staged.SetText(textKey, child, entered);
                    }
                }
            }

            ImGui::TreePop();
        }
    }

}  // namespace BECore
