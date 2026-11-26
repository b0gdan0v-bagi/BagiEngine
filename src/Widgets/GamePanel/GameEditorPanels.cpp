#include "GameEditorPanels.h"

#include <BECore/Memory/MemTag.h>
#include <Games/GameEditorTools.h>
#include <imgui.h>

namespace BECore {

    namespace {

        /// Of a panel the ImGui settings do not know yet
        constexpr ImVec2 kFirstUseSize{360.0f, 240.0f};

    }  // namespace

    void GameEditorPanels::Sync(const GameEditorTools& tools) {
        if (&tools == _tools && tools.GetVersion() == _version) {
            return;
        }
        _tools = &tools;
        _version = tools.GetVersion();
        _windows.clear();
        _windows.reserve(tools.GetPanels().size());
        for (const IntrusivePtr<GameToolPanelEntry>& entry : tools.GetPanels()) {
            _windows.push_back(Window{Format("{}###GamePanel/{}/{}", entry->panel.title, tools.GetPrefix(), entry->panel.id), entry->open});
        }
    }

    void GameEditorPanels::Draw(GameEditorTools* tools) {
        if (tools == nullptr) {
            return;
        }
        Sync(*tools);
        for (size_t i = 0; i < _windows.size(); ++i) {
            // Held for the call: the callback may take its own entry out of the registry
            const IntrusivePtr<GameToolPanelEntry> entry = tools->GetPanels()[i];
            Window& window = _windows[i];
            if (!entry->open) {
                window.shown = false;
                continue;
            }
            if (!window.shown) {
                window.shown = true;
                ImGui::SetNextWindowFocus();
            }
            ImGui::SetNextWindowSize(kFirstUseSize, ImGuiCond_FirstUseEver);
            if (ImGui::Begin(window.label.c_str(), &entry->open)) {
                BE_MEM_TAG(Game);
                entry->panel.draw();
            }
            ImGui::End();
            // The other windows are of a list that is gone
            if (tools->GetVersion() != _version) {
                break;
            }
        }
    }

}  // namespace BECore
