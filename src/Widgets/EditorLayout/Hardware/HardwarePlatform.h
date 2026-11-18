#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareMonitor.h>

namespace BECore {

    /// The GPU reader of this platform, nullptr where there is none
    [[nodiscard]] UniquePtr<IHardwareReader> MakePlatformGpuReader();

    /// The CPU reader of this platform, nullptr where there is none
    [[nodiscard]] UniquePtr<IHardwareReader> MakePlatformCpuReader();

    /// A monitor over the readers of this platform, not started
    [[nodiscard]] UniquePtr<HardwareMonitor> MakePlatformHardwareMonitor();

}  // namespace BECore
