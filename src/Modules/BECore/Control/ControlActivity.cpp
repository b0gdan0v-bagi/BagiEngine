#include "ControlActivity.h"

namespace BECore {

    void AssignControlRecordText(String& out, StringView text) {
        size_t length = std::min(text.size(), ControlActivity::kMaxTextBytes);
        if (length < text.size()) {
            constexpr uint8_t kMask = 0xC0;
            constexpr uint8_t kContinuation = 0x80;
            while (length > 0 && (static_cast<uint8_t>(text[length]) & kMask) == kContinuation) {
                --length;
            }
        }
        out.assign(text.data(), length);
        std::replace_if(out.begin(), out.end(), [](char symbol) { return static_cast<uint8_t>(symbol) < ' '; }, ' ');
    }

    void ControlActivity::Clear() {
        for (ControlActivityEntry& entry : _slots) {
            entry.client.Reset();
        }
        _first = 0;
        _size = 0;
        ++_version;
    }

    ControlActivityEntry& ControlActivity::Add(ControlActivityKind kind, ControlClient* client) {
        ControlActivityEntry& entry = _slots[(_first + _size) % kCapacity];
        if (_size < kCapacity) {
            ++_size;
        } else {
            _first = (_first + 1) % kCapacity;
        }
        entry.sequence = _nextSequence++;
        entry.kind = kind;
        FormatLogTimeNow(entry.time);
        entry.client = client;
        entry.method.clear();
        entry.text.clear();
        entry.error.clear();
        entry.node = 0;
        ++_version;
        return entry;
    }

    void ControlActivity::RecordConnection(PassKey<ControlService>, ControlClient& client, bool connected) {
        Add(connected ? ControlActivityKind::Connected : ControlActivityKind::Disconnected, &client);
    }

    void ControlActivity::RecordName(PassKey<ControlService>, ControlClient& client) {
        AssignControlRecordText(Add(ControlActivityKind::Named, &client).text, client.GetName());
    }

    void ControlActivity::RecordCall(PassKey<ControlService>, ControlClient* client, StringView method, StringView params, StringView error, uint64_t node) {
        ControlActivityEntry& entry = Add(ControlActivityKind::Call, client);
        entry.method.assign(method.data(), method.size());
        AssignControlRecordText(entry.text, params);
        entry.error.assign(error.data(), error.size());
        entry.node = node;
    }

}  // namespace BECore
