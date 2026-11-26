#pragma once

#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/FunctionRef.h>
#include <RoomClient/RoomTransport.h>
#include <Widgets/RemoteEditor/RemoteCallError.h>
#include <Widgets/RemoteEditor/RemoteComponent.h>
#include <Widgets/RemoteEditor/RemotePendingWrites.h>
#include <Widgets/RemoteEditor/RemotePicture.h>
#include <Widgets/RemoteEditor/RemoteSchema.h>
#include <Widgets/RemoteEditor/RemoteTree.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace BECore {

    class JsonWriter;

    /// Everything but `Detached` is remote mode; every state but `Attached` is read-only
    CORE_ENUM(RemoteSessionState, uint8_t, Detached, Connecting, Attached, Lost)

    /// `Unconfirmed`: the link ended before the program answered, so the scene may or may not have been written
    CORE_ENUM(RemoteSaveState, uint8_t, None, Pending, Saved, Refused, Unconfirmed)

    /// What became of the last save a RemoteSession was asked for
    struct RemoteSaveStatus {
        RemoteSaveState state = RemoteSaveState::None;
        /// Of `Saved`: the file the program wrote (its text, untrusted), whether that file outlives the program, and how many things the writer could not keep as they were
        String file;
        bool persistent = true;
        size_t notes = 0;
        /// Of `Refused`
        RemoteCallError error;
    };

    /// The time RemoteSession::Poll() runs its polling and its retries against
    using RemoteSessionTime = std::chrono::steady_clock::time_point;

    namespace RemoteSessionTiming {
        /// Between two reads of the tree
        inline constexpr std::chrono::milliseconds kTreeInterval{1000};
        /// Between two reads of the selected node's components
        inline constexpr std::chrono::milliseconds kComponentInterval{250};
        /// The first retry after a loss; each failed one doubles the wait up to kMaxRetry
        inline constexpr std::chrono::milliseconds kFirstRetry{1000};
        inline constexpr std::chrono::milliseconds kMaxRetry{10000};
        /// How long requests may wait with nothing answered before the session is stalled
        inline constexpr std::chrono::milliseconds kStallAfter{3000};
        /// From an answered `screenshot` to the next one
        inline constexpr std::chrono::milliseconds kPictureInterval{500};
        /// From a refused `screenshot` to the next try
        inline constexpr std::chrono::milliseconds kPictureRetry{5000};
    }  // namespace RemoteSessionTiming

    /// The longest side of the picture a RemoteSession asks for, however large the place that shows it is
    inline constexpr uint32_t kMaxRemotePictureSide = 1600;

    /// The transport of a link of the editor, which takes an answer as long as a control server may send; null where the build has no sockets
    [[nodiscard]] UniquePtr<IRoomTransport> MakeRemoteTransport();

    /**
     * @brief The editor's link to a running control endpoint: keeps the schema, the tree and the selected node's components current and sends the edits
     *
     * Polled on the main thread, which is the only one that may call it. At most one request of each kind is unanswered, so a slow peer is not
     * flooded. A lost link keeps the data that was read and is retried until Detach(); a peer without the methods the session reads with is not
     * retried. The peer's messages are untrusted: one that cannot be read is dropped. A poll with nothing due, or with answers that say what the
     * last ones said, allocates nothing.
     */
    class RemoteSession {
    public:
        /// @p transportFactory makes the transport of each connection
        explicit RemoteSession(RoomTransportFactory transportFactory = &MakeRemoteTransport);

        RemoteSession(const RemoteSession&) = delete;
        RemoteSession& operator=(const RemoteSession&) = delete;

        /// Starts to connect to @p address, leaving what the session was attached to. An error says why there is no connection to wait for (no
        /// sockets in this build, an address the transport refuses) and changes nothing
        [[nodiscard]] std::expected<void, String> Attach(StringView address);

        /// Ends the link and forgets everything read from it and every edit
        void Detach();

        void Poll(RemoteSessionTime now);

        /// Asks for the program's picture with a longer side of @p longerSide pixels, within what `screenshot` takes and kMaxRemotePictureSide.
        /// Called in each frame that shows the picture: a poll reads it only when it was asked for since the poll before
        void RequestPicture(uint32_t longerSide);

        /// The last frame read, kept while the link is `Lost` and reconnects
        [[nodiscard]] const RemotePicture& GetPicture() const {
            return _picture;
        }

        [[nodiscard]] RemoteSessionState GetState() const {
            return _state;
        }

        /// Requests wait and nothing is answered (a browser tab that is hidden, for one): the link is not lost
        [[nodiscard]] bool IsStalled() const {
            return _stalled;
        }

        [[nodiscard]] StringView GetAddress() const {
            return _address;
        }

        /// Why the link is `Lost`: what ended it first, or what ended the retries; empty in the other states
        [[nodiscard]] StringView GetLossReason() const {
            return _lossReason;
        }

        [[nodiscard]] const RemoteSchema& GetSchema() const {
            return _schema;
        }

        [[nodiscard]] const RemoteTree& GetTree() const {
            return _tree;
        }

        /// Selects the node at @p path; a path the tree does not list selects nothing. The selection lasts while the tree lists its path
        void Select(StringView path);

        void ClearSelection();

        [[nodiscard]] const Optional<String>& GetSelection() const {
            return _selection;
        }

        /**
         * @brief Selects what the program shows at @p x, @p y, fractions 0..1 of its picture: a hit becomes the selection, a miss clears it
         *
         * Only while `Attached`. One pick is unanswered at a time and a newer request replaces the one that waits. A pick is dropped when
         * Select() or ClearSelection() is called after it, or its connection ends. A node the tree does not list yet is selected once a new read does.
         */
        void RequestPick(double x, double y);

        /// Why the last answered pick selected nothing; not set after one that worked
        [[nodiscard]] const RemoteCallError& GetPickError() const {
            return _pickError;
        }

        /// The path a pick has selected since the last call, for the view to bring it into sight
        [[nodiscard]] Optional<String> TakeReveal();

        /// Saves the program's scene by its own Save, after the edits made so far. Only while `Attached`; ignored while a save is pending
        void RequestSave();

        [[nodiscard]] const RemoteSaveStatus& GetSaveStatus() const {
            return _saveStatus;
        }

        /// The program accepted a write of this session that no save is known to hold. Kept over a reconnect and forgotten with the link
        [[nodiscard]] bool HasUnsavedWrites() const {
            return _acceptedWrites > _savedWrites;
        }

        /// The components of the selected node in its order, as last read: empty until the first read; one the program did not give is not loaded
        [[nodiscard]] Span<RemoteComponent> GetComponents() {
            return {_components.data(), _components.size()};
        }

        /// Where the edits go (`RemoteComponent::Walk`; a layer is a write of the node alone). They are sent while `Attached`, and a new
        /// connection drops them
        [[nodiscard]] RemotePendingWrites& GetWrites() {
            return _writes;
        }

    private:
        /// The `ok`, `result` and `error` of a response, or of one call of a `batch`; the views die with the next message
        struct Answer {
            bool ok = false;
            JsonView result;
            JsonView error;

            /// Refused with this code
            [[nodiscard]] bool Failed(StringView code) const;
        };

        struct PickPoint {
            double x = 0.0;
            double y = 0.0;
        };

        struct SentWrite {
            uint64_t requestId = 0;
            uint64_t writeId = 0;
        };

        [[nodiscard]] std::expected<UniquePtr<IRoomTransport>, String> Dial(StringView address);
        void DropConnection();
        /// The connection ended or could not be made: `Lost`, and retried
        void Lose(StringView reason, RemoteSessionTime now);
        /// The peer cannot serve the editor: `Lost` for good
        void GiveUpWithout(StringView method);

        void HandleOpened();
        void HandleText(StringView text, RemoteSessionTime now);
        [[nodiscard]] static Answer ReadAnswer(JsonView response);
        void HandleSchema(const Answer& answer);
        void HandleTree(uint64_t requestId, const Answer& answer);
        void HandleComponents(uint64_t requestId, const Answer& answer);
        void HandlePicture(const Answer& answer, RemoteSessionTime now);
        void HandlePick(const Answer& answer);
        void HandleSave(const Answer& answer);
        /// False when no write waits for @p requestId
        bool HandleWrite(uint64_t requestId, const Answer& answer);
        /// `Attached` once this connection has read the schema and the tree
        void AttachWhenRead();

        void SendDue(RemoteSessionTime now);
        void SendWrites();
        void SendComponents();
        void SendPicture();
        void SendPick();
        void ForgetPicks();
        void ApplySelection(StringView path);
        void DropSelection();
        /// Selects the picked path when the tree lists it
        bool SelectPicked();
        /// Sends a request whose params @p writeParams writes the members of; its id, or 0 when the transport refused it, which ends the connection
        uint64_t SendRequest(StringView method, FunctionRef<void(JsonWriter&)> writeParams);
        [[nodiscard]] bool IsWaiting() const;

        RoomTransportFactory _transportFactory;
        UniquePtr<IRoomTransport> _transport;
        RemoteSessionState _state = RemoteSessionState::Detached;
        String _address;
        String _lossReason;

        // Of the connection
        bool _open = false;
        bool _hasSchema = false;
        bool _hasTree = false;
        uint64_t _statusRequest = 0;  // the id of the unanswered request of each kind, 0 when none is
        uint64_t _schemaRequest = 0;
        uint64_t _treeRequest = 0;
        uint64_t _componentRequest = 0;
        uint64_t _pictureRequest = 0;
        uint64_t _pickRequest = 0;
        uint64_t _saveRequest = 0;
        Vector<SentWrite> _sentWrites;
        Optional<RemoteSessionTime> _schemaDueAt;  // nullopt: due at once
        Optional<RemoteSessionTime> _treeDueAt;
        Optional<RemoteSessionTime> _componentsDueAt;
        Optional<RemoteSessionTime> _pictureDueAt;
        /// The program has no `screenshot`
        bool _noPictureMethod = false;
        Optional<PickPoint> _waitingPick;
        /// The selection was chosen after the unanswered pick was sent
        bool _pickSuperseded = false;
        /// A picked node the tree did not list; a read sent after the request `_pickedAfterRequest` decides about it
        Optional<String> _pickedPath;
        uint64_t _pickedAfterRequest = 0;
        /// Since when requests have waited with nothing answered
        Optional<RemoteSessionTime> _waitingSince;
        bool _stalled = false;
        String _runMode;

        // Of the outage
        Optional<RemoteSessionTime> _retryAt;
        std::chrono::milliseconds _retryDelay = RemoteSessionTiming::kFirstRetry;
        bool _outageReported = false;

        /// Never reset, so an id names one request of the session
        uint64_t _lastRequestId = 0;

        RemoteSchema _schema;
        RemoteTree _tree;
        Optional<String> _selection;
        Vector<RemoteComponent> _components;
        RemotePendingWrites _writes;
        Optional<String> _reveal;
        RemoteCallError _pickError;
        RemoteSaveStatus _saveStatus;
        /// The writes the program has accepted, how many of them a save that worked holds, and how many the unanswered one holds
        uint64_t _acceptedWrites = 0;
        uint64_t _savedWrites = 0;
        uint64_t _savingWrites = 0;
        RemotePicture _picture;
        /// The longer side asked for since the last poll
        Optional<uint32_t> _askedPictureSide;

        /// Grows when the selection changes: an answer about an earlier one is dropped
        uint64_t _selectionVersion = 0;
        uint64_t _askedSelectionVersion = 0;
        /// For each component of the unanswered read, which namesake of its node it is
        Vector<size_t> _askedIndices;

        // Members for their capacity
        RoomTransportEvent _event;
        JsonTape _envelope;
        String _request;
    };

}  // namespace BECore
