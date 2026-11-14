#pragma once

#include <BECore/Profiler/Ring/SpikeCapture.h>

namespace BECore {

    /**
     * @brief The spike section of the Profiler panel: the settings of SpikeCapture, the list of frozen frames and the buttons to open
     * one in the timeline, go back to the live ring and clear the list. Editor only.
     */
    class ProfilerSpikePanel {
    public:
        /// What the user asked for this frame; at most one of the fields is set
        struct Result {
            Optional<size_t> open;  // the position in SpikeCapture::GetSpikes() of the spike to show in the timeline
            bool live = false;      // back to the live ring
        };

        /// @p openedFrame is the RingFrame::index of the spike the timeline shows now, none when it shows the live ring
        [[nodiscard]] Result Draw(SpikeCapture& capture, Optional<uint64_t> openedFrame);
    };

}  // namespace BECore
