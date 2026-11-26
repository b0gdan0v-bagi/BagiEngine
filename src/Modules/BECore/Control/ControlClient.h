#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/PassKey.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class ControlService;

    /// Longest name a client may give itself (`client_hello`), in bytes
    inline constexpr size_t kMaxControlClientNameBytes = 64;

    /**
     * @brief Who posts control requests: a connection of a transport, or a part of the program that names itself.
     *
     * ControlService makes it (OpenConnectionClient, OpenLocalClient). A request, its call and whatever records one hold it, so it outlives its
     * connection and the last holder may be any thread. The id and the kind never change; the name and IsConnected() belong to the main thread.
     */
    class ControlClient : public RefCountedAtomic {
    public:
        ControlClient(PassKey<ControlService>, uint64_t id, bool local, StringView name) : _name(name), _id(id), _local(local) {}

        /// From 1, never reused by the service that gave it
        [[nodiscard]] uint64_t GetId() const {
            return _id;
        }

        /// True for a part of the program itself, which has no connection
        [[nodiscard]] bool IsLocal() const {
            return _local;
        }

        /// Empty until a connection names itself. Client text: never interned, never a format string
        [[nodiscard]] const String& GetName() const {
            return _name;
        }

        /// True for a connection from the Drain() that delivered its connect to the one that delivered its disconnect; never for a local client
        [[nodiscard]] bool IsConnected() const {
            return _connected;
        }

    private:
        friend class ControlService;

        String _name;
        const uint64_t _id;
        const bool _local;
        bool _connected = false;
    };

}  // namespace BECore
