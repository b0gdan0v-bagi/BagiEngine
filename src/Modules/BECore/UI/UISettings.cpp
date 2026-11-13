#include "UISettings.h"

namespace BECore {

    float UISettings::ClampUserScale(float scale) {
        return std::clamp(scale, kMinUserScale, kMaxUserScale);
    }

    float UISettings::ClampStateTransition(float seconds) {
        return seconds >= 0.0f ? std::min(seconds, kMaxStateTransition) : 0.0f;
    }

    UISettings UISettings::FromConfig(const XmlNode& node) {
        UISettings settings;
        if (!node) {
            return settings;
        }
        if (auto userScale = node.ParseAttribute<float>("userScale")) {
            settings.userScale = ClampUserScale(*userScale);
        }
        if (auto minTouchSize = node.ParseAttribute<float>("minTouchSize")) {
            settings.minTouchSize = std::max(*minTouchSize, 0.0f);
        }
        if (auto hotReloadInterval = node.ParseAttribute<float>("hotReloadInterval")) {
            settings.hotReloadInterval = std::max(*hotReloadInterval, 0.0f);
        }
        if (auto stateTransition = node.ParseAttribute<float>("stateTransition")) {
            settings.stateTransition = ClampStateTransition(*stateTransition);
        }
        return settings;
    }

}  // namespace BECore
