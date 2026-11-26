#include "RemotePendingWrites.h"

#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        bool IsOf(const RemoteWrite& write, const RemoteWriteTarget& target) {
            return write.index == target.index && StringView(write.path) == target.path && StringView(write.component) == target.component && StringView(write.node) == target.node;
        }

        bool IsUnsent(const RemoteWrite& write) {
            return write.state == RemoteWriteState::Held || write.state == RemoteWriteState::Ready;
        }

        bool IsShownBy(const RemoteWrite& write, uint64_t requestId) {
            return write.state == RemoteWriteState::Answered && write.answeredAt < requestId;
        }

    }  // namespace

    void RemotePendingWrites::Edit(const RemoteWriteTarget& target, PropertyValue value, RemoteEditKind kind) {
        _refusal.reset();
        auto write = eastl::find_if(_writes.begin(), _writes.end(), [&target](const RemoteWrite& other) { return IsUnsent(other) && IsOf(other, target); });
        if (write == _writes.end()) {
            RemoteWrite& added = _writes.emplace_back();
            added.id = ++_lastId;
            added.node = String(target.node);
            added.component = String(target.component);
            added.index = target.index;
            added.path = String(target.path);
            write = _writes.end() - 1;
        }
        write->state = kind == RemoteEditKind::Text ? RemoteWriteState::Held : RemoteWriteState::Ready;
        write->value = std::move(value);
    }

    void RemotePendingWrites::ReleaseText() {
        for (RemoteWrite& write : _writes) {
            if (write.state == RemoteWriteState::Held) {
                write.state = RemoteWriteState::Ready;
            }
        }
    }

    const PropertyValue* RemotePendingWrites::Find(const RemoteWriteTarget& target) const {
        for (auto write = _writes.rbegin(); write != _writes.rend(); ++write) {
            if (IsOf(*write, target)) {
                return &write->value;
            }
        }
        return nullptr;
    }

    const RemoteWrite* RemotePendingWrites::NextToSend() const {
        for (const RemoteWrite& write : _writes) {
            if (write.state != RemoteWriteState::Ready) {
                continue;
            }
            const bool waits = eastl::any_of(_writes.begin(), _writes.end(), [&write](const RemoteWrite& other) { return other.state == RemoteWriteState::Sent && IsOf(other, write.Target()); });
            if (!waits) {
                return &write;
            }
        }
        return nullptr;
    }

    bool RemotePendingWrites::HasUnanswered() const {
        return eastl::any_of(_writes.begin(), _writes.end(), [](const RemoteWrite& write) { return write.state != RemoteWriteState::Answered; });
    }

    void RemotePendingWrites::MarkSent(uint64_t writeId) {
        if (RemoteWrite* write = FindById(writeId)) {
            write->state = RemoteWriteState::Sent;
        }
    }

    void RemotePendingWrites::Accept(uint64_t writeId, uint64_t lastRequestId) {
        if (RemoteWrite* write = FindById(writeId)) {
            write->state = RemoteWriteState::Answered;
            write->answeredAt = lastRequestId;
        }
    }

    void RemotePendingWrites::Refuse(uint64_t writeId, StringView code, StringView message) {
        if (RemoteWrite* write = FindById(writeId)) {
            _refusal = RemoteRefusal{std::move(write->node), std::move(write->component), std::move(write->path), String(code), String(message)};
            _writes.erase(write);
        }
    }

    void RemotePendingWrites::ReleaseComponent(StringView node, StringView component, size_t index, uint64_t requestId) {
        eastl::erase_if(_writes,
                        [&](const RemoteWrite& write) { return IsShownBy(write, requestId) && write.index == index && StringView(write.component) == component && StringView(write.node) == node; });
    }

    void RemotePendingWrites::ReleaseLayers(uint64_t requestId) {
        eastl::erase_if(_writes, [requestId](const RemoteWrite& write) { return IsShownBy(write, requestId) && write.IsLayer(); });
    }

    void RemotePendingWrites::Clear() {
        _writes.clear();
        _refusal.reset();
    }

    RemoteWrite* RemotePendingWrites::FindById(uint64_t writeId) {
        const auto found = eastl::find_if(_writes.begin(), _writes.end(), [writeId](const RemoteWrite& write) { return write.id == writeId; });
        return found != _writes.end() ? &*found : nullptr;
    }

}  // namespace BECore
