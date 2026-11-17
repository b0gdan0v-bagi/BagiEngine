#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>
#include <cstdint>

struct SDL_SharedObject;

namespace BECore {

    /// nvmlReturn_t of a successful call
    inline constexpr int kNvmlSuccess = 0;
    /// NVML_TEMPERATURE_GPU, the die sensor
    inline constexpr int kNvmlTemperatureGpu = 0;
    /// Bytes of a device name NVML never exceeds (NVML_DEVICE_NAME_V2_BUFFER_SIZE), the NUL included
    inline constexpr unsigned int kNvmlNameBufferSize = 96;

    /// nvmlDevice_t, opaque
    using NvmlDeviceHandle = void*;

    /// nvmlUtilization_t
    struct NvmlUtilization {
        unsigned int gpu = 0;
        unsigned int memory = 0;
    };

    /// nvmlMemory_t, bytes
    struct NvmlMemory {
        unsigned long long total = 0;
        unsigned long long free = 0;
        unsigned long long used = 0;
    };

    /**
     * @brief The NVML functions the reader calls, with the signatures of nvml.h declared here: the SDK is not needed to build.
     *
     * A fake table is how a test drives NvmlReader without a driver. `library` is the handle LoadNvmlApi keeps (null in a fake); the
     * table does not own it, NvmlReader releases it.
     */
    struct NvmlApi {
        int (*nvmlInit_v2)() = nullptr;
        int (*nvmlShutdown)() = nullptr;
        int (*nvmlDeviceGetCount_v2)(unsigned int* count) = nullptr;
        int (*nvmlDeviceGetHandleByIndex_v2)(unsigned int index, NvmlDeviceHandle* device) = nullptr;
        int (*nvmlDeviceGetName)(NvmlDeviceHandle device, char* name, unsigned int length) = nullptr;
        int (*nvmlDeviceGetTemperature)(NvmlDeviceHandle device, int sensor, unsigned int* temperature) = nullptr;
        int (*nvmlDeviceGetUtilizationRates)(NvmlDeviceHandle device, NvmlUtilization* utilization) = nullptr;
        int (*nvmlDeviceGetMemoryInfo)(NvmlDeviceHandle device, NvmlMemory* memory) = nullptr;
        SDL_SharedObject* library = nullptr;
    };

    /// The names of the NVML library this platform tries, in order; empty where NVML is not used
    [[nodiscard]] Span<const char* const> GetDefaultNvmlLibraryNames();

    /// Loads the first of @p libraryNames that opens and exports every function of NvmlApi; empty when none does
    [[nodiscard]] Optional<NvmlApi> LoadNvmlApi(Span<const char* const> libraryNames);

    /**
     * @brief Device 0 of the NVIDIA driver: temperature, load and memory, each queried on its own so one failing query leaves only its field missing.
     *
     * Built from a ready table or from the default library names; in that case the library is loaded inside the first Read (on the
     * worker), never in the constructor, because loading and nvmlInit take hundreds of milliseconds. A failed load, init or lookup of
     * the device is final: every later Read returns false at once and nothing is loaded again.
     */
    class NvmlReader final : public IHardwareReader {
    public:
        explicit NvmlReader(const NvmlApi& api);
        NvmlReader();
        ~NvmlReader() override;

        NvmlReader(const NvmlReader&) = delete;
        NvmlReader& operator=(const NvmlReader&) = delete;

        bool Read(HardwareReadings& out) override;

    private:
        enum class State : uint8_t { NotStarted, Ready, Failed };

        bool Open();

        NvmlApi _api;
        State _state = State::NotStarted;
        /// Set by the constructor without a table: Open() loads the default library first
        bool _loadOnOpen = false;
        bool _initialized = false;
        NvmlDeviceHandle _device = nullptr;
        char _name[kNvmlNameBufferSize] = {};
    };

}  // namespace BECore
