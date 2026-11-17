#include "HardwarePlatform.h"

#include <Widgets/EditorLayout/Hardware/CpuReaders.h>
#include <Widgets/EditorLayout/Hardware/MacReaders.h>
#include <Widgets/EditorLayout/Hardware/NvmlReader.h>

namespace BECore {

    UniquePtr<IHardwareReader> MakePlatformGpuReader() {
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
        return MakeUnique<NvmlReader>();
#elif defined(PLATFORM_MACOS)
        return MakeMacGpuReader();
#else
        return nullptr;
#endif
    }

    UniquePtr<IHardwareReader> MakePlatformCpuReader() {
#if defined(PLATFORM_WINDOWS)
        return MakeWindowsCpuReader();
#elif defined(PLATFORM_LINUX)
        return MakeLinuxCpuReader();
#elif defined(PLATFORM_MACOS)
        return MakeMacCpuReader();
#else
        return nullptr;
#endif
    }

    UniquePtr<HardwareMonitor> MakePlatformHardwareMonitor() {
        return MakeUnique<HardwareMonitor>(MakePlatformGpuReader(), MakePlatformCpuReader());
    }

}  // namespace BECore
