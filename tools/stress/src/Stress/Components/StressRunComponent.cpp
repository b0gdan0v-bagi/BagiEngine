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
        const StressGame* game = StressGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const StressState& state = game->GetState();
        FrameStatsCollector& frameStats = CoreManager::GetFrameStats();
        frameStats.SetCounter(_counterName, state.GetCount());

        const StressSettings& settings = game->GetSettings();
        if (_done || !settings.IsMeasuring()) {
            return;
        }

        // This update runs inside the frame after the last finished one: the frame whose cost TakeSample reads
        const uint64_t currentFrame = frameStats.GetFrameCount() + 1;
        if (state.Version() != _seenVersion) {
            // The frame that notices a change builds what it changed; the warm-up runs after it. The samples taken so far were of another
            // scene or count: they go, and the buffer is made once (a Reserve that has the room allocates nothing)
            _seenVersion = state.Version();
            _firstMeasured = currentFrame + settings.warmup + 1;
            _recorder.Reserve(settings.frames);
        }

        TakeSample();
        if (_recorder.Size() >= settings.frames) {
            Finish(*game);
        }
    }

    void StressRunComponent::TakeSample() {
        const FrameStats* last = CoreManager::GetFrameStats().GetLast();
        if (last == nullptr || last->frame == _lastTaken) {
            return;
        }
        _lastTaken = last->frame;
        if (last->paused || last->frame < _firstMeasured) {
            return;
        }
        _recorder.Add(SampleOf(*last));
    }

    void StressRunComponent::Finish(const StressGame& game) {
        _done = true;
        const StressSettings& settings = game.GetSettings();
        StressRunContext context;
        context.scene = game.GetState().GetScene();
        context.count = game.GetState().GetCount();
        context.warmup = settings.warmup;
        context.interleave = settings.interleave;
        context.atlases = settings.atlases;
        context.manualClock = CoreManager::GetControlService().IsManualClock();
        context.allocTracking = AllocTracker::kEnabled;
        Print(FormatJsonLine(_recorder.Summarize(), context));
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
