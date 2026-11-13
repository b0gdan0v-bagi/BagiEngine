#include "UITransition.h"

#include <BECore/Animation/Lerp.h>
#include <BECore/UI/UIGeometryBounds.h>

namespace BECore {

    namespace {

        constexpr float kHiddenProgress = 0.0f;
        constexpr float kShownProgress = 1.0f;

        float BoundSeconds(float seconds) {
            if (!(seconds > 0.0f)) {
                return 0.0f;  // negative and NaN
            }
            return std::min(seconds, kMaxTransitionSeconds);
        }

        float BoundScale(float scale) {
            if (!(scale == scale)) {
                return 1.0f;
            }
            return std::clamp(scale, 0.0f, UIBounds::kMaxScale);
        }

    }  // namespace

    UITransitionSpec UITransitionSpec::Bounded() const {
        UITransitionSpec result = *this;
        result.scaleFrom = BoundScale(scaleFrom);
        result.slideX = UIBounds::Bound(slideX);
        result.slideY = UIBounds::Bound(slideY);
        result.duration = ShowSeconds();
        result.hideDuration = hideDuration >= 0.0f ? BoundSeconds(hideDuration) : -1.0f;
        return result;
    }

    float UITransitionSpec::ShowSeconds() const {
        return BoundSeconds(duration);
    }

    float UITransitionSpec::HideSeconds() const {
        return hideDuration >= 0.0f ? BoundSeconds(hideDuration) : ShowSeconds();
    }

    UITransition::Evaluated UITransition::Evaluate(const UITransitionSpec& spec, float progress) {
        if (!(progress == progress)) {
            return {};
        }
        const UITransitionSpec bounded = spec.Bounded();
        Evaluated result;
        if (bounded.fade) {
            result.opacity = std::clamp(progress, 0.0f, 1.0f);
        }
        // An overshooting ease may bulge the scale past 1, never below 0 (a mirrored element)
        result.scale = std::max(Lerp(bounded.scaleFrom, kShownProgress, progress), 0.0f);
        const float remaining = kShownProgress - progress;
        result.offsetX = remaining * bounded.slideX;
        result.offsetY = remaining * bounded.slideY;
        return result;
    }

    ViewTransform UITransition::Compose(const ViewTransform& view, Vec2 pivot, const Evaluated& evaluated) {
        const float s = evaluated.scale;
        ViewTransform result;
        result.scale = view.scale * s;
        result.offsetX = view.offsetX + (pivot.x * (1.0f - s) + evaluated.offsetX) * view.scale;
        result.offsetY = view.offsetY + (pivot.y * (1.0f - s) + evaluated.offsetY) * view.scale;
        return result;
    }

    void UITransitionRun::Sync(bool visible, double now, const UITransitionSpec& spec) {
        if (!_synced) {
            _synced = true;
            _targetVisible = visible;
            _running = false;
            _from = visible ? kShownProgress : kHiddenProgress;
            return;
        }
        if (visible == _targetVisible) {
            return;
        }
        const float shown = Progress(now);
        _targetVisible = visible;
        const float goal = visible ? kShownProgress : kHiddenProgress;
        const float seconds = visible ? spec.ShowSeconds() : spec.HideSeconds();
        // The time of the run is the time of a full trip in proportion to the distance left, so a reversal near the end is quick
        const float length = seconds * std::abs(goal - shown);
        if (!(length > 0.0f)) {
            _running = false;
            _from = goal;
            return;
        }
        _running = true;
        _from = shown;
        _length = length;
        _start = now;
        _ease = spec.ease;
    }

    float UITransitionRun::Progress(double now) const {
        if (!_running) {
            return _from;  // a run that is not on has settled: `_from` is where it stands
        }
        const float goal = _targetVisible ? kShownProgress : kHiddenProgress;
        const float t = static_cast<float>(std::clamp((now - _start) / static_cast<double>(_length), 0.0, 1.0));
        return Lerp(_from, goal, Ease(_ease, t));
    }

    bool UITransitionRun::IsTransitioning(double now) const {
        return _running && now < _start + static_cast<double>(_length);
    }

    bool UITransitionRun::IsLeaving(double now) const {
        // The run decides, not the progress: an overshooting ease (BackOut, ElasticOut) takes the progress below 0 before it settles
        return _synced && !_targetVisible && IsTransitioning(now);
    }

    bool UITransitionRun::IsShowing(double now) const {
        return _targetVisible && IsTransitioning(now);
    }

    bool UITransitionRun::TakeFinished(double now) {
        if (!_running || now < _start + static_cast<double>(_length)) {
            return false;
        }
        _running = false;
        _from = _targetVisible ? kShownProgress : kHiddenProgress;
        return true;
    }

}  // namespace BECore
