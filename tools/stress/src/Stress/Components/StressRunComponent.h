#pragma once

#include <BECore/Scene/IComponent.h>
#include <Stress/RampController.h>
#include <Stress/StressRecorder.h>
#include <functional>
#include <optional>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Stress {

    class StressGame;

    /**
     * @brief The measurement of a run: sits on the node `Run` of every scene, counts frames and, when the command line asks for a measuring run,
     *        records them, prints one JSON line and quits.
     *
     * Every frame it sets the named counter `Stress objects` to the count of the state (the control smoke and the overlay read it). It measures
     * only with `--stress-frames` (the fixed mode, below) or `--stress-ramp`; a plain run just shows the scene.
     *
     * **Fixed mode.** A sample is the `FrameStats` of the last finished frame, which this update (inside the next frame) reads once per new frame
     * id: a frame it already took, a held frame (the game stood still) and every frame before `firstMeasured` are skipped. When
     * `StressState::Version()` moves, the frame that notices it is the frame that builds the new nodes, textures and glyphs, so it and the next
     * `warmup` frames are skipped: the warm-up restarts with every change of the scene, the count or the language. After `frames` samples the line
     * (`FormatJsonLine`) goes to the sink and `ApplicationEvents::QuitEvent` is emitted, once. The recorder's samples are reserved when the run starts.
     *
     * **Ramp mode** (`--stress-ramp`, which wins over `--stress-frames`). Each new frame's measure (`workMs` or `cpuMs` by `--stress-measure`) goes
     * to the `RampController`, and the count it answers is set into the state; its settle frames cover the frame that builds the new count, so no
     * warm-up logic is needed here. When it is done (or aborted) the line (`FormatRampLine`) goes out and the application is asked to quit, once.
     */
    class StressRunComponent : public IComponent {
        BE_CLASS(StressRunComponent)
    public:
        /// Where the JSON line goes: a line without its newline
        using LineSink = std::function<void(const eastl::string&)>;

        StressRunComponent() = default;
        ~StressRunComponent() override = default;

        void OnAttached() override;

        /// Sends the line to @p sink instead of stdout (tests); an empty sink puts it back
        void SetLineSink(LineSink sink) {
            _sink = std::move(sink);
        }

        /// The line has been printed and the run asked the application to quit
        bool IsDone() const {
            return _done;
        }

        /// The measured frames recorded so far
        size_t GetRecordedCount() const {
            return _recorder.Size();
        }

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        /// The last finished frame when it is one this component has not looked at yet and the game did advance in it, else null
        const FrameStats* TakeNewFrame();
        void UpdateFixed(const StressGame& game);
        void UpdateRamp(StressGame& game);
        /// Prints the line of the run and asks the application to quit
        void FinishFixed(const StressGame& game);
        void FinishRamp(const StressGame& game);
        void Print(const eastl::string& line) const;
        static bool IsManualClock();

        PoolString _counterName = "Stress objects"_intern;
        StressRecorder _recorder;
        std::optional<RampController> _ramp;  // made at the first update of a ramp run
        LineSink _sink;
        uint64_t _seenVersion = 0;    // the StressState version of the last update; 0 is never a version
        uint64_t _firstMeasured = 0;  // the first frame id a sample is taken of
        uint64_t _lastTaken = 0;      // the frame id of the last sample seen (taken or skipped), so a frame is judged once
        bool _done = false;
    };

}  // namespace BECore::Stress
