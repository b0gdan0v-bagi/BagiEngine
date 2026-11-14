#include "ProfilerSpikePanel.h"

#include <EASTL/algorithm.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kModeComboWidth = 140.0f;
        constexpr float kValueFieldWidth = 90.0f;
        constexpr float kKeepFieldWidth = 70.0f;
        constexpr float kListHeight = 110.0f;
        constexpr float kThresholdSpeedMs = 0.5f;
        constexpr float kFactorSpeed = 0.1f;
        constexpr float kMinThresholdMs = 1.0f;
        constexpr float kMaxThresholdMs = 10000.0f;
        constexpr float kMinFactor = 1.1f;
        constexpr float kMaxFactor = 100.0f;
        constexpr int kMinKeep = 1;
        constexpr int kMaxKeep = 64;

        const char* const kModeLabels = "Over a time (ms)\0"
                                        "Over the median (times)\0";

    }  // namespace

    ProfilerSpikePanel::Result ProfilerSpikePanel::Draw(SpikeCapture& capture, Optional<uint64_t> openedFrame) {
        Result result;
        SpikeSettings settings = capture.GetSettings();
        bool changed = false;

        int mode = static_cast<int>(+settings.mode);
        ImGui::SetNextItemWidth(kModeComboWidth);
        if (ImGui::Combo("##spikeMode", &mode, kModeLabels)) {
            settings.mode = static_cast<SpikeMode>(mode);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(kValueFieldWidth);
        if (settings.mode == SpikeMode::AbsoluteMs) {
            auto thresholdMs = static_cast<float>(settings.thresholdMs);
            if (ImGui::DragFloat("ms##spikeThreshold", &thresholdMs, kThresholdSpeedMs, kMinThresholdMs, kMaxThresholdMs, "%.1f")) {
                settings.thresholdMs = static_cast<double>(thresholdMs);
                changed = true;
            }
        } else {
            auto factor = static_cast<float>(settings.medianFactor);
            if (ImGui::DragFloat("x median##spikeFactor", &factor, kFactorSpeed, kMinFactor, kMaxFactor, "%.1f")) {
                settings.medianFactor = static_cast<double>(factor);
                changed = true;
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(kKeepFieldWidth);
        auto keep = static_cast<int>(settings.keep);
        if (ImGui::InputInt("keep##spikeKeep", &keep, 0)) {
            settings.keep = static_cast<size_t>(eastl::clamp(keep, kMinKeep, kMaxKeep));
            changed = true;
        }
        if (changed) {
            capture.SetSettings(settings);
        }

        ImGui::BeginDisabled(!openedFrame);
        if (ImGui::Button("Live")) {
            result.live = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            capture.Clear();
            result.live = openedFrame.has_value();
        }
        ImGui::SameLine();
        ImGui::Text("%zu frozen", capture.GetSpikes().size());

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
                    result.open = i;
                }
            }
        }
        ImGui::EndChild();
        return result;
    }

}  // namespace BECore
