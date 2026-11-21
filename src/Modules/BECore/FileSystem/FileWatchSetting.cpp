#include "FileWatchSetting.h"

namespace BECore {

    bool ShouldWatchFiles(FileWatchSetting setting, RunMode runMode, bool assertsOn, bool isWeb) {
        if (isWeb) {
            return false;
        }
        switch (setting) {
            case FileWatchSetting::On:
                return true;
            case FileWatchSetting::Off:
                return false;
            case FileWatchSetting::Auto:
                break;
        }
        return runMode == RunMode::Editor || assertsOn;
    }

}  // namespace BECore
