#include "UISettings.h"

namespace BECore {

    float UISettings::ClampUserScale(float scale) {
        return std::clamp(scale, kMinUserScale, kMaxUserScale);
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
        return settings;
    }

}  // namespace BECore
