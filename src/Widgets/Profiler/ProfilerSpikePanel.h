#pragma once

#include <BECore/Profiler/Ring/SpikeCapture.h>

namespace BECore {

    /**
     * @brief The list of frozen frames of the Profiler panel, newest first, one row per spike. The settings of `SpikeCapture` and the buttons
     * that go back to the live ring and clear the list are the panel asset's. Editor only.
     */
    class ProfilerSpikePanel {
    public:
        /// @p openedFrame is the RingFrame::index of the spike the timeline shows now, none when it shows the live ring
        /// @return The position in SpikeCapture::GetSpikes() of the row clicked this frame
        [[nodiscard]] Optional<size_t> DrawList(const SpikeCapture& capture, Optional<uint64_t> openedFrame);
    };

}  // namespace BECore
