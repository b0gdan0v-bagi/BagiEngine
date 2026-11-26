#pragma once

#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// What a write changes: a field of a component of a remote node, or the node's layer (no component and no path)
    struct RemoteWriteTarget {
        StringView node;
        /// The type name; of the node's components of that type, the one at `index`
        StringView component{};
        size_t index = 0;
        /// The property path of the field in the component
        StringView path{};
    };

    /// `Held`: a text the person may still be typing. `Ready` may be sent, `Sent` has no answer yet, `Answered` was accepted by the program
    CORE_ENUM(RemoteWriteState, uint8_t, Held, Ready, Sent, Answered)

    CORE_ENUM(RemoteEditKind, uint8_t, Value, Text)

    struct RemoteWrite {
        /// Unique among the writes of one list
        uint64_t id = 0;
        RemoteWriteState state = RemoteWriteState::Ready;
        String node;
        String component;
        size_t index = 0;
        String path;
        /// A layer is an `int64_t`
        PropertyValue value;
        /// The id of the last request sent when the answer came: only a later read is known to show the write
        uint64_t answeredAt = 0;

        [[nodiscard]] bool IsLayer() const {
            return component.empty();
        }

        [[nodiscard]] RemoteWriteTarget Target() const {
            return {node, component, index, path};
        }
    };

    /// The last write the program refused, for the panel to show
    struct RemoteRefusal {
        String node;
        String component;
        String path;
        String code;
        String message;
    };

    /**
     * @brief The edits of remote values that the data read from the program does not show yet
     *
     * An edited value overrides the received one from the edit until a read that was sent after the write was answered has arrived: an earlier
     * read may still carry the old value. Request ids are the caller's and grow with every request it sends.
     */
    class RemotePendingWrites {
    public:
        /// Takes an edit of @p target: it replaces an unsent write of the same target. A `Text` edit waits for ReleaseText(), a `Value` may be sent at once
        void Edit(const RemoteWriteTarget& target, PropertyValue value, RemoteEditKind kind);

        /// The texts typed so far may be sent: no field is being edited
        void ReleaseText();

        /// What @p target shows instead of the received value: the newest write of it; null when there is none
        [[nodiscard]] const PropertyValue* Find(const RemoteWriteTarget& target) const;

        /// The oldest write that may be sent now: `Ready`, with no unanswered write of its target. Null when there is none
        [[nodiscard]] const RemoteWrite* NextToSend() const;

        void MarkSent(uint64_t writeId);

        /// A write is still unsent, a held text included, or has no answer yet
        [[nodiscard]] bool HasUnanswered() const;

        /// The program accepted the write; @p lastRequestId is the id of the last request sent so far
        void Accept(uint64_t writeId, uint64_t lastRequestId);

        /// The program refused the write: its override goes and the reason is kept
        void Refuse(uint64_t writeId, StringView code, StringView message);

        /// A `scene_get` of this component, sent as @p requestId, has arrived
        void ReleaseComponent(StringView node, StringView component, size_t index, uint64_t requestId);

        /// A `scene_tree` sent as @p requestId has arrived: it carries the layers
        void ReleaseLayers(uint64_t requestId);

        /// Forgets every write and the refusal
        void Clear();

        [[nodiscard]] const Vector<RemoteWrite>& GetWrites() const {
            return _writes;
        }

        [[nodiscard]] const Optional<RemoteRefusal>& GetRefusal() const {
            return _refusal;
        }

    private:
        RemoteWrite* FindById(uint64_t writeId);

        /// In the order of the edits
        Vector<RemoteWrite> _writes;
        Optional<RemoteRefusal> _refusal;
        uint64_t _lastId = 0;
    };

}  // namespace BECore
