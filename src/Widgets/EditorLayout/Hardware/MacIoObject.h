#pragma once

#include <BECore/Types/Types.h>

// Compiled only on macOS and never built in the loop that wrote it (Hardware/CLAUDE.md, "macOS")
#ifdef PLATFORM_MACOS

#include <IOKit/IOKitLib.h>

namespace BECore {

    /// Owns one io_object_t (an iterator or a registry entry) and gives it back with IOObjectRelease; an io_connect_t is closed with IOServiceClose instead
    class IoObject {
    public:
        explicit IoObject(io_object_t handle) : _handle(handle) {}
        ~IoObject() {
            if (_handle != IO_OBJECT_NULL) {
                IOObjectRelease(_handle);
            }
        }
        IoObject(const IoObject&) = delete;
        IoObject& operator=(const IoObject&) = delete;

        [[nodiscard]] io_object_t Get() const {
            return _handle;
        }
        explicit operator bool() const {
            return _handle != IO_OBJECT_NULL;
        }

    private:
        io_object_t _handle;
    };

}  // namespace BECore

#endif  // PLATFORM_MACOS
