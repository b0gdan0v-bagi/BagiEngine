#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>

namespace BECore {

#ifdef PLATFORM_WINDOWS
    /// The CPU load from GetSystemTimes (CpuReaderWindows.cpp); there is no CPU temperature a normal user can read on Windows
    [[nodiscard]] UniquePtr<IHardwareReader> MakeWindowsCpuReader();
#endif

#ifdef PLATFORM_LINUX
    /// The CPU load from /proc/stat and the temperature of the CPU's hwmon device (CpuReaderLinux.cpp)
    [[nodiscard]] UniquePtr<IHardwareReader> MakeLinuxCpuReader();
#endif

}  // namespace BECore
