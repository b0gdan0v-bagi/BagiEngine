#pragma once

#include <BECore/Control/ControlClient.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/PassKey.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class ControlService;

    /// One report of a client that touched a node of a scene. Both texts may hold client data: never interned, never a format string
    struct ControlTouch {
        uint64_t sequence = 0;  ///< from 1, never reused
        uint64_t nodeId = 0;    ///< SceneNode::GetRuntimeId(); the node may be gone since
        String component;       ///< the component's type name; empty for the node as a whole
        String field;           ///< the field of the component; empty for the component as a whole
        IntrusivePtr<ControlClient> client;
    };

    /**
     * @brief Which nodes, components and fields the clients of the control API touched, the last kCapacity reports.
     *
     * ControlService records into it (ControlService::ReportTouch). It holds ids and never a node, so it keeps nothing of a scene alive and a reader
     * looks the node up (FindNodeByRuntimeId). A ring of slots made once: a report goes into the oldest slot, whose strings keep their capacity.
     * Main thread only.
     */
    class ControlTouches {
    public:
        static constexpr size_t kCapacity = 256;

        ControlTouches() : _slots(kCapacity) {}

        [[nodiscard]] size_t Size() const {
            return _size;
        }

        /// @p index 0 is the oldest report kept; below Size(). The reference is good until the next report is recorded
        [[nodiscard]] const ControlTouch& At(size_t index) const {
            return _slots[(_first + index) % kCapacity];
        }

        /// The sequence the next report gets: a reader that kept it knows later which reports are new
        [[nodiscard]] uint64_t GetNextSequence() const {
            return _nextSequence;
        }

        /// Moves with every report and with nothing else
        [[nodiscard]] uint64_t GetVersion() const {
            return _nextSequence;
        }

        void Record(PassKey<ControlService>, ControlClient& client, uint64_t nodeId, StringView component, StringView field);

    private:
        Vector<ControlTouch> _slots;
        size_t _first = 0;
        size_t _size = 0;
        uint64_t _nextSequence = 1;
    };

}  // namespace BECore
