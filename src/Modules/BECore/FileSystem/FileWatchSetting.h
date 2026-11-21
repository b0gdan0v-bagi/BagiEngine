#pragma once

#include <BECore/Application/RunMode.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The `fileWatch` attribute of config/ApplicationConfig.xml: whether the FileWatcher runs.
     *
     * Auto watches in the Editor run mode and in a build with asserts on (the Debug builds), where a person edits files next to the running program;
     * a Release game has nobody to tell. On and Off are as written.
     */
    CORE_ENUM(FileWatchSetting, uint8_t, Auto, On, Off)

    /// Whether the file watcher starts. @p runMode is the resolved mode (it runs in this build), @p assertsOn is the condition of the ASSERT macros, @p isWeb
    /// is a build without a file system to watch: always false there, whatever the setting says.
    [[nodiscard]] bool ShouldWatchFiles(FileWatchSetting setting, RunMode runMode, bool assertsOn, bool isWeb);

}  // namespace BECore
