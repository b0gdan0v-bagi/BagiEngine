#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>

namespace BECore {

#ifdef PLATFORM_MACOS
    /// The GPU load and memory from the IOAccelerator services of the IORegistry (MacGpuReader.cpp)
    [[nodiscard]] UniquePtr<IHardwareReader> MakeMacGpuReader();

    /// The CPU load from host_statistics (MacCpuReader.cpp)
    [[nodiscard]] UniquePtr<IHardwareReader> MakeMacCpuReader();
#endif

}  // namespace BECore
