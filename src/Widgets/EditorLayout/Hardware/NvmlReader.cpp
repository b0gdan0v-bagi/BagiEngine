#include "NvmlReader.h"

#include <SDL3/SDL_loadso.h>

namespace BECore {

    namespace {

        constexpr unsigned long long kBytesPerMib = 1024ULL * 1024ULL;

#if defined(PLATFORM_WINDOWS)
        // The driver puts nvml.dll in the system folder; the second name is where older drivers left it
        constexpr const char* kDefaultLibraryNames[] = {"nvml.dll", "C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll"};
#elif defined(PLATFORM_LINUX)
        constexpr const char* kDefaultLibraryNames[] = {"libnvidia-ml.so.1"};
#endif

        uint32_t BytesToMib(unsigned long long bytes) {
            const unsigned long long mib = (bytes + kBytesPerMib / 2) / kBytesPerMib;
            return static_cast<uint32_t>(std::min<unsigned long long>(mib, std::numeric_limits<uint32_t>::max()));
        }

        /// The exported function @p name of @p library as the pointer type of its slot; false when the library lacks it
        template <typename TFunction>
        bool Resolve(SDL_SharedObject* library, const char* name, TFunction& slot) {
            slot = reinterpret_cast<TFunction>(SDL_LoadFunction(library, name));
            return slot != nullptr;
        }

        bool ResolveAll(NvmlApi& api) {
            return Resolve(api.library, "nvmlInit_v2", api.nvmlInit_v2) && Resolve(api.library, "nvmlShutdown", api.nvmlShutdown) &&
                   Resolve(api.library, "nvmlDeviceGetCount_v2", api.nvmlDeviceGetCount_v2) && Resolve(api.library, "nvmlDeviceGetHandleByIndex_v2", api.nvmlDeviceGetHandleByIndex_v2) &&
                   Resolve(api.library, "nvmlDeviceGetName", api.nvmlDeviceGetName) && Resolve(api.library, "nvmlDeviceGetTemperature", api.nvmlDeviceGetTemperature) &&
                   Resolve(api.library, "nvmlDeviceGetUtilizationRates", api.nvmlDeviceGetUtilizationRates) && Resolve(api.library, "nvmlDeviceGetMemoryInfo", api.nvmlDeviceGetMemoryInfo);
        }

    }  // namespace

    Span<const char* const> GetDefaultNvmlLibraryNames() {
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
        return Span<const char* const>(kDefaultLibraryNames);
#else
        return {};
#endif
    }

    Optional<NvmlApi> LoadNvmlApi(Span<const char* const> libraryNames) {
        for (const char* const name : libraryNames) {
            SDL_SharedObject* const library = SDL_LoadObject(name);
            if (library == nullptr) {
                continue;
            }
            NvmlApi api;
            api.library = library;
            if (ResolveAll(api)) {
                return api;
            }
            SDL_UnloadObject(library);
        }
        return std::nullopt;
    }

    NvmlReader::NvmlReader(const NvmlApi& api) : _api(api) {}

    NvmlReader::NvmlReader() : _loadOnOpen(true) {}

    NvmlReader::~NvmlReader() {
        if (_initialized) {
            // Nothing to do about a failed shutdown: the table is dropped either way
            (void)_api.nvmlShutdown();
        }
        if (_api.library != nullptr) {
            SDL_UnloadObject(_api.library);
        }
    }

    bool NvmlReader::Open() {
        if (_loadOnOpen) {
            const Optional<NvmlApi> loaded = LoadNvmlApi(GetDefaultNvmlLibraryNames());
            if (!loaded) {
                return false;
            }
            _api = *loaded;
        }
        if (_api.nvmlInit_v2() != kNvmlSuccess) {
            return false;
        }
        _initialized = true;
        unsigned int count = 0;
        if (_api.nvmlDeviceGetCount_v2(&count) != kNvmlSuccess || count == 0) {
            return false;
        }
        if (_api.nvmlDeviceGetHandleByIndex_v2(0, &_device) != kNvmlSuccess || _device == nullptr) {
            return false;
        }
        const bool named = _api.nvmlDeviceGetName(_device, _name, kNvmlNameBufferSize) == kNvmlSuccess;
        _name[kNvmlNameBufferSize - 1] = '\0';
        return named;
    }

    bool NvmlReader::Read(HardwareReadings& out) {
        if (_state == State::NotStarted) {
            _state = Open() ? State::Ready : State::Failed;
        }
        if (_state != State::Ready) {
            return false;
        }

        SetHardwareName(out, StringView(_name));

        unsigned int temperature = 0;
        if (_api.nvmlDeviceGetTemperature(_device, kNvmlTemperatureGpu, &temperature) == kNvmlSuccess && temperature <= static_cast<unsigned int>(kMaxHardwareTemperatureC)) {
            out.temperatureC = static_cast<int32_t>(temperature);
        }
        NvmlUtilization utilization;
        if (_api.nvmlDeviceGetUtilizationRates(_device, &utilization) == kNvmlSuccess) {
            out.loadPercent = utilization.gpu;
        }
        NvmlMemory memory;
        if (_api.nvmlDeviceGetMemoryInfo(_device, &memory) == kNvmlSuccess) {
            out.memUsedMb = BytesToMib(memory.used);
            out.memTotalMb = BytesToMib(memory.total);
        }
        out.unifiedMemory = false;
        return HasHardwareValue(out);
    }

}  // namespace BECore
