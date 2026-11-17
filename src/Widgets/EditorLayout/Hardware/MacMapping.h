#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Bytes of MacGpuStats::model, the terminating NUL included
    inline constexpr size_t kMacGpuModelSize = 64;

    /**
     * @brief What the IORegistry said about one accelerator, copied as it is: an absent key is an empty value.
     *
     * MacGpuReader.cpp fills it from the C APIs and MapMacGpu decides what it means, so every decision of the macOS GPU readout is
     * a pure function that is tested on every platform.
     */
    struct MacGpuStats {
        /// `Device Utilization %` of the PerformanceStatistics dictionary
        Optional<double> utilizationPercent;
        /// `In use system memory`: the system memory the accelerator uses (the only memory of an integrated GPU)
        Optional<uint64_t> inUseSystemBytes;
        /// `vramUsedBytes` and `vramFreeBytes`: the own memory of a discrete GPU
        Optional<uint64_t> vramUsedBytes;
        Optional<uint64_t> vramFreeBytes;
        /// `hw.memsize`: the total of a unified memory
        Optional<uint64_t> physicalMemoryBytes;
        /// The accelerator's `model`, NUL-terminated, empty when unknown
        char model[kMacGpuModelSize] = {};
    };

    /**
     * @brief Turns the statistics into load, memory and name of @p out (the temperature and `kind` are not touched).
     *
     * The load is rounded and clamped to 0..100. Memory: a discrete GPU (VRAM used and free both present) shows its own memory;
     * otherwise `In use system memory` is the used part of the unified memory, whose total is the machine's physical memory (without
     * it only the used part is shown) and `unifiedMemory` is set. A missing piece leaves its field empty. False when no load and no
     * memory could be made.
     */
    [[nodiscard]] bool MapMacGpu(const MacGpuStats& stats, HardwareReadings& out);

}  // namespace BECore
