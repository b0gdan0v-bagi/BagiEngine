#include "StressRunComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <Events/ApplicationEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/StressRunComponent.gen.hpp>
#include <Stress/StressGame.h>

namespace BECore::Stress {

    void StressRunComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &StressRunComponent::OnUpdate>(this);
    }

    void StressRunComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        StressGame* game = StressGame::GetActive();
        if (game == nullptr) {
            return;
        }
        FrameStatsCollector& frameStats = CoreManager::GetFrameStats();
        frameStats.SetCounter(_counterName, game->GetState().GetCount());

        const StressSettings& settings = game->GetSettings();
        if (_done || !settings.IsMeasuring()) {
            return;
        }
        if (settings.ramp) {
            UpdateRamp(*game);
        } else {
            UpdateFixed(*game);
        }
    }

    const FrameStats* StressRunComponent::TakeNewFrame() {
        const FrameStats* last = CoreManager::GetFrameStats().GetLast();
        if (last == nullptr || last->frame == _lastTaken) {
            return nullptr;
        }
        _lastTaken = last->frame;
        return last->paused ? nullptr : last;
    }

    void StressRunComponent::UpdateFixed(const StressGame& game) {
        const StressSettings& settings = game.GetSettings();
        const StressState& state = game.GetState();
        if (state.Version() != _seenVersion) {
            // The frame that notices a change is the one that builds what it changed (this update runs inside the frame after the last finished
            // one); the warm-up runs after it. The samples taken so far were of another scene or count: they go, and the buffer is made once
            // (a Reserve that has the room allocates nothing)
            _seenVersion = state.Version();
            _firstMeasured = CoreManager::GetFrameStats().GetFrameCount() + 1 + settings.warmup + 1;
            _recorder.Reserve(settings.frames);
        }

        const FrameStats* frame = TakeNewFrame();
        if (frame != nullptr && frame->frame >= _firstMeasured) {
            _recorder.Add(SampleOf(*frame));
        }
        if (_recorder.Size() >= settings.frames) {
            FinishFixed(game);
        }
    }

    void StressRunComponent::UpdateRamp(StressGame& game) {
        const StressSettings& settings = game.GetSettings();
        if (!_ramp) {
            _ramp.emplace(settings.measure);  // the state already shows kRampStart (StressGame)
        }
        const FrameStats* frame = TakeNewFrame();
        if (frame == nullptr) {
            return;
        }
        const StressSample sample = SampleOf(*frame);
        const RampNext next = _ramp->OnFrame(settings.measure == StressMeasure::Work ? sample.workMs : sample.cpuMs);
        if (next.done) {
            FinishRamp(game);
        } else {
            game.GetState().SetCount(next.count);  // the scene builds it in this frame or the next; the settle frames cover that
        }
    }

    bool StressRunComponent::IsManualClock() {
        return CoreManager::GetControlService().IsManualClock();
    }

    void StressRunComponent::FinishFixed(const StressGame& game) {
        _done = true;
        const StressSettings& settings = game.GetSettings();
        StressRunContext context;
        context.scene = game.GetState().GetScene();
        context.count = game.GetState().GetCount();
        context.warmup = settings.warmup;
        context.interleave = settings.interleave;
        context.atlases = settings.atlases;
        context.manualClock = IsManualClock();
        context.allocTracking = AllocTracker::kEnabled;
        Print(FormatJsonLine(_recorder.Summarize(), context));
        ApplicationEvents::QuitEvent::Emit();
    }

    void StressRunComponent::FinishRamp(const StressGame& game) {
        _done = true;
        const StressSettings& settings = game.GetSettings();
        RampRunContext context;
        context.scene = game.GetState().GetScene();
        context.interleave = settings.interleave;
        context.atlases = settings.atlases;
        context.manualClock = IsManualClock();
        context.allocTracking = AllocTracker::kEnabled;
        Print(FormatRampLine(_ramp->GetResult(), settings.measure, context));
        ApplicationEvents::QuitEvent::Emit();
    }

    void StressRunComponent::Print(const eastl::string& line) const {
        if (_sink) {
            _sink(line);
            return;
        }
        // Not a log line: the benchmark reads the last stdout line that starts with `{`
        std::fputs(line.c_str(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

}  // namespace BECore::Stress
