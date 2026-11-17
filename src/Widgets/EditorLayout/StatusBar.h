#pragma once

#include <BECore/Control/ControlTypes.h>
#include <Widgets/EditorLayout/Hardware/HardwareMonitor.h>
#include <Widgets/EditorLayout/Hardware/HardwareText.h>
#include <Widgets/EditorLayout/StatusBarModel.h>

namespace BECore {

    /**
     * @brief The bar at the bottom of the editor: frame time and FPS, allocations, the GPU or CPU readout, the control API, the game, the
     * scene and the branch.
     *
     * Every number is text in a fixed buffer made again only when its value changed (the FrameMeter rounds and averages, so that is a
     * few times a second); a name is copied into a member only when it differs from the last one seen. A steady frame allocates
     * nothing. ImGui calls: a frame in progress is required. Like the toolbar it is a child of the current window, not part of the
     * saved layout.
     */
    class StatusBar {
    public:
        /// The height of the bar in ImGui units at the current font and theme
        [[nodiscard]] static float GetHeight();

        /// The branch of the engine's repository, read once by the owner; empty leaves it out
        void SetBranch(StringView branch);

        /// Builds the monitor of this platform and starts its worker; nothing is started (and no readout shown) where the platform has no reader
        void StartHardwareMonitor();

        /// Takes a monitor that is not started: the test's way to drive it with PollOnce on the calling thread
        void SetHardwareMonitor(UniquePtr<HardwareMonitor> monitor);

        /// What the hardware group shows now
        [[nodiscard]] const HardwareText& GetHardwareText() const {
            return _hardwareText;
        }

        void Draw();

    private:
        static constexpr size_t kTextSize = 48;
        static constexpr uint32_t kNoVersion = std::numeric_limits<uint32_t>::max();

        /// Reads the sources of the bar and makes the text of what changed
        void Refresh();
        void DrawContent();

        /// Its destructor joins the worker, so the monitor dies with the bar
        UniquePtr<HardwareMonitor> _hardware;
        HardwareReadings _hardwareReadings;
        HardwareText _hardwareText;

        FrameMeter _meter;
        uint32_t _shownMeterVersion = kNoVersion;
        char _fpsText[kTextSize] = {};
        char _frameMsText[kTextSize] = {};

        uint64_t _allocCount = 0;
        Optional<uint64_t> _shownAllocCount;
        char _allocText[kTextSize] = {};

        ControlServerStatus _controlStatus;
        Optional<ControlServerStatus> _shownControlStatus;
        char _controlText[kTextSize] = {};

        PoolString _seenGame;
        PoolString _seenScene;
        String _game;
        String _scene;
        String _branch;
    };

}  // namespace BECore
