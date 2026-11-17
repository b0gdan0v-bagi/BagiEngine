#include <Widgets/EditorLayout/Hardware/MacCoreFoundation.h>
#include <Widgets/EditorLayout/Hardware/MacIoObject.h>
#include <Widgets/EditorLayout/Hardware/MacMapping.h>
#include <Widgets/EditorLayout/Hardware/MacSensors.h>

// Not compiled in the loop that wrote it (no Mac), and what it relies on (private IOHID symbols, the layout of the SMC's struct) is
// from memory of open-source readers: every call is checked and a failure is a missing temperature (Hardware/CLAUDE.md, "macOS").
#ifdef PLATFORM_MACOS

#include <SDL3/SDL_loadso.h>
#include <mach/mach.h>

namespace BECore {

    namespace {

        // ---- IOHID (Apple Silicon): private symbols of IOKit, resolved at run time so a wrong guess is a missing temperature, never a link error

        struct HidClientTag;
        struct HidServiceTag;
        struct HidEventTag;
        using HidClient = const HidClientTag*;
        using HidService = const HidServiceTag*;
        using HidEvent = const HidEventTag*;

        using HidClientCreateFn = HidClient (*)(CFAllocatorRef allocator);
        using HidClientSetMatchingFn = int (*)(HidClient client, CFDictionaryRef matching);
        using HidClientCopyServicesFn = CFArrayRef (*)(HidClient client);
        using HidServiceCopyPropertyFn = CFTypeRef (*)(HidService service, CFStringRef key);
        using HidServiceCopyEventFn = HidEvent (*)(HidService service, int64_t eventType, int32_t options, int64_t timestamp);
        using HidEventGetFloatValueFn = double (*)(HidEvent event, int32_t field);

        constexpr const char* kIoKitLibraryPath = "/System/Library/Frameworks/IOKit.framework/IOKit";
        constexpr const char* kUsagePageKey = "PrimaryUsagePage";
        constexpr const char* kUsageKey = "PrimaryUsage";
        /// The vendor usage page and usage of the Apple temperature sensors
        constexpr int32_t kSensorUsagePage = 0xff00;
        constexpr int32_t kSensorUsage = 5;
        constexpr int64_t kHidEventTypeTemperature = 15;
        /// The field of an IOHID event is its type in the upper 16 bits (IOHIDEventFieldBase)
        constexpr int32_t kHidTemperatureField = 15 << 16;
        /// A Mac has a few dozen temperature services; the rest would not fit the fixed array and is left out
        constexpr size_t kMaxSensorSamples = 64;

        template <typename TFunction>
        bool Resolve(SDL_SharedObject* library, const char* name, TFunction& slot) {
            slot = reinterpret_cast<TFunction>(SDL_LoadFunction(library, name));
            return slot != nullptr;
        }

        /// The IOHID filter {PrimaryUsagePage: 0xff00, PrimaryUsage: 5}; every Create goes into an owner, and the dictionary retains what it holds
        CfOwned<CFDictionaryRef> MakeSensorMatching() {
            const int32_t page = kSensorUsagePage;
            const int32_t usage = kSensorUsage;
            const CfOwned<CFNumberRef> pageNumber(CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &page));
            const CfOwned<CFNumberRef> usageNumber(CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usage));
            const CfOwned<CFStringRef> pageKey(CFStringCreateWithCString(kCFAllocatorDefault, kUsagePageKey, kCFStringEncodingUTF8));
            const CfOwned<CFStringRef> usageKey(CFStringCreateWithCString(kCFAllocatorDefault, kUsageKey, kCFStringEncodingUTF8));
            if (!pageNumber || !usageNumber || !pageKey || !usageKey) {
                return {};
            }
            const void* keys[] = {pageKey.Get(), usageKey.Get()};
            const void* values[] = {pageNumber.Get(), usageNumber.Get()};
            return CfOwned<CFDictionaryRef>(
                CFDictionaryCreate(kCFAllocatorDefault, keys, values, static_cast<CFIndex>(std::size(keys)), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        }

        // ---- SMC (Intel): the struct the AppleSMC user client exchanges, as the open-source readers declare it

        constexpr uint32_t kSmcHandleYpcEvent = 2;
        constexpr uint8_t kSmcReadKey = 5;
        constexpr uint8_t kSmcGetKeyInfo = 9;
        constexpr uint8_t kSmcSuccess = 0;
        constexpr size_t kSmcBytesSize = 32;
        constexpr size_t kSmcKeyDataSize = 80;

        struct SmcVersion {
            char major = 0;
            char minor = 0;
            char build = 0;
            char reserved = 0;
            uint16_t release = 0;
        };

        struct SmcPLimit {
            uint16_t version = 0;
            uint16_t length = 0;
            uint32_t cpuPLimit = 0;
            uint32_t gpuPLimit = 0;
            uint32_t memPLimit = 0;
        };

        struct SmcKeyInfo {
            uint32_t dataSize = 0;
            /// Four characters, big endian
            uint32_t dataType = 0;
            uint8_t dataAttributes = 0;
        };

        struct SmcKeyData {
            uint32_t key = 0;
            SmcVersion version;
            SmcPLimit pLimit;
            SmcKeyInfo keyInfo;
            uint8_t result = 0;
            uint8_t status = 0;
            uint8_t data8 = 0;
            uint32_t data32 = 0;
            uint8_t bytes[kSmcBytesSize] = {};
        };

        static_assert(sizeof(SmcKeyData) == kSmcKeyDataSize, "the AppleSMC user client reads and writes exactly this layout");

        uint32_t PackFourCharacters(StringView text) {
            return (static_cast<uint32_t>(static_cast<uint8_t>(text[0])) << 24) | (static_cast<uint32_t>(static_cast<uint8_t>(text[1])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(text[2])) << 8) | static_cast<uint32_t>(static_cast<uint8_t>(text[3]));
        }

        void UnpackFourCharacters(uint32_t packed, char (&text)[kSmcTypeSize]) {
            text[0] = static_cast<char>(packed >> 24);
            text[1] = static_cast<char>(packed >> 16);
            text[2] = static_cast<char>(packed >> 8);
            text[3] = static_cast<char>(packed);
        }

    }  // namespace

    struct MacSensors::State {
        ~State() {
            // The client first: it may talk to IOKit while it dies, and the library stays loaded until after it
            hidClient.Reset();
            sensorMatching.Reset();
            if (iokit != nullptr) {
                SDL_UnloadObject(iokit);
            }
            if (smcConnection != IO_OBJECT_NULL) {
                // Closes the connection and destroys the handle
                IOServiceClose(smcConnection);
            }
        }

        using SampleArray = Array<MacSensorSample, kMaxSensorSamples>;

        bool opened = false;

        SDL_SharedObject* iokit = nullptr;
        HidClientCreateFn hidClientCreate = nullptr;
        HidClientSetMatchingFn hidClientSetMatching = nullptr;
        HidClientCopyServicesFn hidClientCopyServices = nullptr;
        HidServiceCopyPropertyFn hidServiceCopyProperty = nullptr;
        HidServiceCopyEventFn hidServiceCopyEvent = nullptr;
        HidEventGetFloatValueFn hidEventGetFloatValue = nullptr;
        CfOwned<CFDictionaryRef> sensorMatching;
        CfOwned<HidClient> hidClient;
        /// True when the IOHID path found sensors: the SMC is then never tried
        bool hidActive = false;

        io_connect_t smcConnection = IO_OBJECT_NULL;

        /// Chooses the path on the first call: IOHID when it finds a sensor (Apple Silicon), else the SMC (Intel)
        void Open() {
            opened = true;
            if (OpenHid()) {
                SampleArray samples;
                hidActive = CollectHidSamples(samples) > 0;
            }
            if (!hidActive) {
                OpenSmc();
            }
        }

        Optional<int32_t> Read(HardwareKind kind) {
            if (!opened) {
                Open();
            }
            if (hidActive) {
                SampleArray samples;
                const size_t count = CollectHidSamples(samples);
                return PickAppleSiliconTemperature(Span<const MacSensorSample>(samples.data(), count), kind);
            }
            if (smcConnection != IO_OBJECT_NULL) {
                return ReadSmc(kind == HardwareKind::Gpu ? Span<const StringView>(kSmcGpuTemperatureKeys) : Span<const StringView>(kSmcCpuTemperatureKeys));
            }
            return std::nullopt;
        }

    private:
        bool OpenHid() {
            iokit = SDL_LoadObject(kIoKitLibraryPath);
            if (iokit == nullptr) {
                return false;
            }
            const bool resolved = Resolve(iokit, "IOHIDEventSystemClientCreate", hidClientCreate) && Resolve(iokit, "IOHIDEventSystemClientSetMatching", hidClientSetMatching) &&
                                  Resolve(iokit, "IOHIDEventSystemClientCopyServices", hidClientCopyServices) && Resolve(iokit, "IOHIDServiceClientCopyProperty", hidServiceCopyProperty) &&
                                  Resolve(iokit, "IOHIDServiceClientCopyEvent", hidServiceCopyEvent) && Resolve(iokit, "IOHIDEventGetFloatValue", hidEventGetFloatValue);
            if (!resolved) {
                return false;
            }
            sensorMatching = MakeSensorMatching();
            if (!sensorMatching) {
                return false;
            }
            // Create: +1, owned from here
            hidClient.Reset(hidClientCreate(kCFAllocatorDefault));
            if (!hidClient) {
                return false;
            }
            // The result is a status nobody can act on: a client with no filter just finds more services, and the names pick the right ones
            (void)hidClientSetMatching(hidClient.Get(), sensorMatching.Get());
            return true;
        }

        /// Fills @p samples with the name and the temperature of each service that has both; every +1 object is owned and released on every path
        size_t CollectHidSamples(SampleArray& samples) {
            // Copy: +1
            const CfOwned<CFArrayRef> services(hidClientCopyServices(hidClient.Get()));
            if (!services || CFGetTypeID(services.Get()) != CFArrayGetTypeID()) {
                return 0;
            }
            const CFIndex total = CFArrayGetCount(services.Get());
            size_t count = 0;
            for (CFIndex index = 0; index < total && count < samples.size(); ++index) {
                // Borrowed from the array, which outlives this iteration
                const void* const borrowed = CFArrayGetValueAtIndex(services.Get(), index);
                if (borrowed == nullptr) {
                    continue;
                }
                const HidService service = static_cast<HidService>(borrowed);
                // Copy: +1
                const CfOwned<CFTypeRef> product(hidServiceCopyProperty(service, CFSTR("Product")));
                MacSensorSample& sample = samples[count];
                sample = MacSensorSample{};
                if (!CfStringToBuffer(product.Get(), Span<char>(sample.name))) {
                    continue;
                }
                // Copy: +1, null when this service has no temperature event
                const CfOwned<HidEvent> event(hidServiceCopyEvent(service, kHidEventTypeTemperature, 0, 0));
                if (!event) {
                    continue;
                }
                sample.celsius = hidEventGetFloatValue(event.Get(), kHidTemperatureField);
                ++count;
            }
            return count;
        }

        bool OpenSmc() {
            // Create: +1, consumed by the next call whether it succeeds or not
            CFMutableDictionaryRef matching = IOServiceMatching("AppleSMC");
            if (matching == nullptr) {
                return false;
            }
            io_iterator_t rawIterator = IO_OBJECT_NULL;
            if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &rawIterator) != KERN_SUCCESS) {
                return false;
            }
            const IoObject iterator(rawIterator);
            const IoObject service(IOIteratorNext(iterator.Get()));
            if (!service) {
                return false;
            }
            io_connect_t connection = IO_OBJECT_NULL;
            if (IOServiceOpen(service.Get(), mach_task_self(), 0, &connection) != KERN_SUCCESS || connection == IO_OBJECT_NULL) {
                return false;
            }
            smcConnection = connection;
            return true;
        }

        /// One call of the user client; false for a failing return, a short answer or a non-zero result byte
        bool CallSmc(const SmcKeyData& input, SmcKeyData& output) const {
            size_t outputSize = sizeof(output);
            const kern_return_t status = IOConnectCallStructMethod(smcConnection, kSmcHandleYpcEvent, &input, sizeof(input), &output, &outputSize);
            return status == KERN_SUCCESS && outputSize == sizeof(output) && output.result == kSmcSuccess;
        }

        /// The first of @p keys that reads and decodes
        Optional<int32_t> ReadSmc(Span<const StringView> keys) const {
            for (const StringView key : keys) {
                if (const Optional<int32_t> celsius = ReadSmcKey(key)) {
                    return celsius;
                }
            }
            return std::nullopt;
        }

        /// Two calls: the key's size and type, then its bytes; a partial answer is never decoded
        Optional<int32_t> ReadSmcKey(StringView key) const {
            if (key.size() != kSmcTypeSize) {
                return std::nullopt;
            }
            SmcKeyData infoInput;
            infoInput.key = PackFourCharacters(key);
            infoInput.data8 = kSmcGetKeyInfo;
            SmcKeyData infoOutput;
            if (!CallSmc(infoInput, infoOutput)) {
                return std::nullopt;
            }
            const uint32_t size = infoOutput.keyInfo.dataSize;
            if (size == 0 || size > kSmcBytesSize) {
                return std::nullopt;
            }
            char type[kSmcTypeSize];
            UnpackFourCharacters(infoOutput.keyInfo.dataType, type);

            SmcKeyData readInput;
            readInput.key = infoInput.key;
            readInput.keyInfo.dataSize = size;
            readInput.data8 = kSmcReadKey;
            SmcKeyData readOutput;
            if (!CallSmc(readInput, readOutput)) {
                return std::nullopt;
            }
            return DecodeSmcTemperature(type, Span<const uint8_t>(readOutput.bytes, size));
        }
    };

    MacSensors::MacSensors() : _state(MakeUnique<State>()) {}

    MacSensors::~MacSensors() = default;

    Optional<int32_t> MacSensors::ReadCpuC() {
        return _state->Read(HardwareKind::Cpu);
    }

    Optional<int32_t> MacSensors::ReadGpuC() {
        return _state->Read(HardwareKind::Gpu);
    }

}  // namespace BECore

#endif  // PLATFORM_MACOS
