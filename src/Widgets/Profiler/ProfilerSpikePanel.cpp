#include "ProfilerSpikePanel.h"

#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kListHeight = 110.0f;

    }  // namespace

    Optional<size_t> ProfilerSpikePanel::DrawList(const SpikeCapture& capture, Optional<uint64_t> openedFrame) {
        Optional<size_t> opened;
        const Vector<FrozenFrame>& spikes = capture.GetSpikes();
        if (ImGui::BeginChild("##spikeList", ImVec2(0.0f, kListHeight), ImGuiChildFlags_Borders)) {
            if (spikes.empty()) {
                ImGui::TextDisabled("No spike yet");
            }
            // The newest first
            for (size_t i = spikes.size(); i-- > 0;) {
                const FrozenFrame& spike = spikes[i];
                char label[96];
                if (spike.medianMs > 0.0) {
                    snprintf(label, sizeof(label), "frame %llu, %.2f ms (median %.2f ms)", static_cast<unsigned long long>(spike.frameIndex), spike.frameMs, spike.medianMs);
                } else {
                    snprintf(label, sizeof(label), "frame %llu, %.2f ms", static_cast<unsigned long long>(spike.frameIndex), spike.frameMs);
                }
                if (ImGui::Selectable(label, openedFrame && *openedFrame == spike.frameIndex)) {
                    opened = i;
                }
            }
        }
        ImGui::EndChild();
        return opened;
    }

}  // namespace BECore
