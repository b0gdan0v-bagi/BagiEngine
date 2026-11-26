#pragma once

#include <BECore/Control/ControlClient.h>
#include <BECore/Logger/LogHistory.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/PassKey.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class ControlService;

    CORE_ENUM(ControlActivityKind, uint8_t, Connected, Disconnected, Named, Call)

    /// One entry of the journal. Every text of it is client data or may hold it: never interned, never a format string
    struct ControlActivityEntry {
        uint64_t sequence = 0;  ///< from 1, never reused
        ControlActivityKind kind = ControlActivityKind::Call;
        char time[kLogTimeLength] = {};      ///< local time of day when it was recorded, "HH:MM:SS.mmm"
        IntrusivePtr<ControlClient> client;  ///< who, null for the engine's own call; its name is the one it has now
        String method;                       ///< Call: the method
        String text;                         ///< Call: the params as the client wrote them, on one line and shortened; Named: the name given then
        String error;                        ///< Call: the code of a failed one, empty for a success
        uint64_t node = 0;                   ///< Call: the runtime id of the node it touched first (ControlTouches.h), 0 for none
    };

    /// Makes @p out the start of @p text as the journal and the touch record keep client text: on one line (control characters become spaces),
    /// ControlActivity::kMaxTextBytes at most and no half of a UTF-8 character
    void AssignControlRecordText(String& out, StringView text);

    /**
     * @brief The journal of the control API: who connected, who named itself and who did what, the last kCapacity entries.
     *
     * ControlService records into it (ControlService::Activity()); which calls are listed: `.claude/rules/control-activity.md`. A ring of slots made
     * once: an entry goes into the oldest slot, whose strings keep their capacity. Main thread only.
     */
    class ControlActivity {
    public:
        static constexpr size_t kCapacity = 256;

        /// Longest `text` of an entry in bytes; a longer one is cut between two UTF-8 characters
        static constexpr size_t kMaxTextBytes = 200;

        ControlActivity() : _slots(kCapacity) {}

        [[nodiscard]] size_t Size() const {
            return _size;
        }

        /// @p index 0 is the oldest entry kept; below Size(). The reference is good until the next entry is recorded
        [[nodiscard]] const ControlActivityEntry& At(size_t index) const {
            return _slots[(_first + index) % kCapacity];
        }

        /// The sequence the next entry gets: a reader that kept it knows later which entries are new
        [[nodiscard]] uint64_t GetNextSequence() const {
            return _nextSequence;
        }

        /// Moves with every entry and every Clear(), and with nothing else
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

        /// Empties the journal and gives the clients back; the sequence goes on
        void Clear();

        void RecordConnection(PassKey<ControlService>, ControlClient& client, bool connected);
        void RecordName(PassKey<ControlService>, ControlClient& client);

        /// An answered call; @p error is empty for a success, @p node is the node it touched or 0
        void RecordCall(PassKey<ControlService>, ControlClient* client, StringView method, StringView params, StringView error, uint64_t node);

    private:
        /// The slot of a new entry, its texts empty
        ControlActivityEntry& Add(ControlActivityKind kind, ControlClient* client);

        Vector<ControlActivityEntry> _slots;
        size_t _first = 0;
        size_t _size = 0;
        uint64_t _nextSequence = 1;
        uint64_t _version = 0;
    };

}  // namespace BECore
