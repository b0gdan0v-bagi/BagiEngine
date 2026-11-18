#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <cstdint>

// Compiled only on macOS and never built in the loop that wrote it (Hardware/CLAUDE.md, "macOS")
#ifdef PLATFORM_MACOS

namespace BECore {

    /**
     * @brief The temperatures a normal user can read on a Mac: the IOHID sensors of Apple Silicon, the SMC of an Intel Mac.
     *
     * Nothing is opened until the first Read...C(), which runs on the monitor's worker (opening takes milliseconds). A temperature that
     * cannot be read is empty, never a guess. Used by one thread only; the GPU and the CPU reader share one object (reference counted
     * because both readers own it), and the worker is the only thread that calls it.
     */
    class MacSensors final : public RefCountedAtomic {
    public:
        MacSensors();
        ~MacSensors() override;

        [[nodiscard]] Optional<int32_t> ReadCpuC();
        [[nodiscard]] Optional<int32_t> ReadGpuC();

    private:
        struct State;
        UniquePtr<State> _state;
    };

    using MacSensorsPtr = IntrusivePtrAtomic<MacSensors>;

}  // namespace BECore

#endif  // PLATFORM_MACOS
