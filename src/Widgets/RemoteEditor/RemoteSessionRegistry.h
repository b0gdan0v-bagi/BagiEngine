#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <cstdint>

namespace BECore {

    class RemoteSession;
    class RemoteSessionRegistry;

    /**
     * @brief The registration of a session; the session is unregistered when the handle ends
     *
     * Move-only. A session registered again replaces the first, whose handle then unregisters nothing.
     */
    class RemoteSessionHandle {
    public:
        RemoteSessionHandle() = default;
        ~RemoteSessionHandle();

        RemoteSessionHandle(const RemoteSessionHandle&) = delete;
        RemoteSessionHandle& operator=(const RemoteSessionHandle&) = delete;
        RemoteSessionHandle(RemoteSessionHandle&& other) noexcept;
        RemoteSessionHandle& operator=(RemoteSessionHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle or one that a later registration replaced)
        void Reset();

    private:
        friend class RemoteSessionRegistry;
        explicit RemoteSessionHandle(uint64_t token) : _token(token) {}

        uint64_t _token = 0;
    };

    /**
     * @brief The one link of the editor to another program, found by the panels that show it
     *
     * `RemoteSessionWidget` registers its session for its lifetime. A panel asks in each call and keeps no pointer. Main thread only, so it takes no lock
     */
    class RemoteSessionRegistry : public Singleton<RemoteSessionRegistry> {
    public:
        RemoteSessionRegistry() = default;

        /// Makes @p session the one panels find; it must outlive the handle. A session registered before is replaced (logged once)
        [[nodiscard]] RemoteSessionHandle Register(RemoteSession& session);

        /// The registered session, or null
        [[nodiscard]] RemoteSession* Get() const {
            return _session;
        }

        /// True for the first caller only: the program's `--attach` is for its start, not for the widgets a later mode switch makes
        [[nodiscard]] bool TakeStartAttach();

    private:
        friend class RemoteSessionHandle;

        void Unregister(uint64_t token);

        RemoteSession* _session = nullptr;
        uint64_t _token = 0;
        uint64_t _nextToken = 1;
        bool _startAttachTaken = false;
    };

}  // namespace BECore
