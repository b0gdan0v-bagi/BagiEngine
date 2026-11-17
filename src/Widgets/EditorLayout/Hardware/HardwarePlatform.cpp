#include "HardwarePlatform.h"

#include <Widgets/EditorLayout/Hardware/CpuReaders.h>
#include <Widgets/EditorLayout/Hardware/NvmlReader.h>

namespace BECore {

    UniquePtr<IHardwareReader> MakePlatformGpuReader() {
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
        return MakeUnique<NvmlReader>();
#else
        return nullptr;
#endif
    }

    UniquePtr<IHardwareReader> MakePlatformCpuReader() {
#if defined(PLATFORM_WINDOWS)
        return MakeWindowsCpuReader();
#elif defined(PLATFORM_LINUX)
        return MakeLinuxCpuReader();
#else
        return nullptr;
#endif
    }

    UniquePtr<HardwareMonitor> MakePlatformHardwareMonitor() {
        return MakeUnique<HardwareMonitor>(MakePlatformGpuReader(), MakePlatformCpuReader());
    }

}  // namespace BECore
