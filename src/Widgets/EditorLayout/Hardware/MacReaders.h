#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>
#include <Widgets/EditorLayout/Hardware/MacSensors.h>

namespace BECore {

#ifdef PLATFORM_MACOS
    /// The GPU load and memory from the IOAccelerator services of the IORegistry, and its temperature from @p sensors (MacGpuReader.cpp)
    [[nodiscard]] UniquePtr<IHardwareReader> MakeMacGpuReader(MacSensorsPtr sensors);

    /// The CPU load from host_statistics and its temperature from @p sensors, which the GPU reader may share (MacCpuReader.cpp)
    [[nodiscard]] UniquePtr<IHardwareReader> MakeMacCpuReader(MacSensorsPtr sensors);
#endif

}  // namespace BECore
