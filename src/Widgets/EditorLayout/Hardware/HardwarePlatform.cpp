#include "HardwarePlatform.h"

namespace BECore {

    UniquePtr<IHardwareReader> MakePlatformGpuReader() {
        return nullptr;
    }

    UniquePtr<IHardwareReader> MakePlatformCpuReader() {
        return nullptr;
    }

    UniquePtr<HardwareMonitor> MakePlatformHardwareMonitor() {
        return MakeUnique<HardwareMonitor>(MakePlatformGpuReader(), MakePlatformCpuReader());
    }

}  // namespace BECore
