#include "RunMode.h"

namespace BECore {

    RunMode ReadRunMode(const XmlNode& applicationConfig) {
        const auto modeName = applicationConfig.ParseAttribute<eastl::string_view>("mode");
        if (!modeName) {
            return RunMode::Editor;
        }

        const auto mode = EnumUtils<RunMode>::Cast(*modeName);
        if (!mode) {
            LOG_WARNING("ApplicationConfig: unknown mode '{}', starting in Editor mode"_format(*modeName));
            return RunMode::Editor;
        }
        return *mode;
    }

    PoolString GetWidgetsConfigName(RunMode mode) {
        if (mode == RunMode::Game) {
            return "GameWidgetsConfig"_intern;
        }
        return "WidgetsConfig"_intern;
    }

}  // namespace BECore
