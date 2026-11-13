#include "UITransitionComponent.h"

#include <Generated/UITransitionComponent.gen.hpp>

namespace BECore {

    UITransitionSpec UITransitionComponent::MakeSpec() const {
        UITransitionSpec spec;
        spec.fade = fade;
        spec.scaleFrom = scaleFrom;
        spec.slideX = slideX;
        spec.slideY = slideY;
        spec.duration = duration;
        spec.hideDuration = hideDuration;
        spec.ease = ease;
        spec.inputDuringTransition = inputDuringTransition;
        return spec;
    }

}  // namespace BECore
