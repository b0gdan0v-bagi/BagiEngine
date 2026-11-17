#include "HardwarePlatform.h"

#include <Widgets/EditorLayout/Hardware/CpuReaders.h>
#include <Widgets/EditorLayout/Hardware/MacReaders.h>
#include <Widgets/EditorLayout/Hardware/NvmlReader.h>

namespace BECore {

    UniquePtr<IHardwareReader> MakePlatformGpuReader() {
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
        return MakeUnique<NvmlReader>();
#elif defined(PLATFORM_MACOS)
        return MakeMacGpuReader(New<MacSensors>());
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
        return MakeMacCpuReader(New<MacSensors>());
#else
        return nullptr;
#endif
    }

    UniquePtr<HardwareMonitor> MakePlatformHardwareMonitor() {
#if defined(PLATFORM_MACOS)
        // The two readers ask the same sensors, which are opened once
        const MacSensorsPtr sensors = New<MacSensors>();
        return MakeUnique<HardwareMonitor>(MakeMacGpuReader(sensors), MakeMacCpuReader(sensors));
#else
        return MakeUnique<HardwareMonitor>(MakePlatformGpuReader(), MakePlatformCpuReader());
#endif
    }

}  // namespace BECore
