#include "PanelSlots.h"

namespace BECore {

    PanelSlotHandle::~PanelSlotHandle() {
        Reset();
    }

    PanelSlotHandle::PanelSlotHandle(PanelSlotHandle&& other) noexcept : _owner(other._owner), _name(std::move(other._name)), _token(other._token) {
        other._owner = nullptr;
    }

    PanelSlotHandle& PanelSlotHandle::operator=(PanelSlotHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _owner = other._owner;
            _name = std::move(other._name);
            _token = other._token;
            other._owner = nullptr;
        }
        return *this;
    }

    void PanelSlotHandle::Reset() {
        if (_owner != nullptr) {
            _owner->Unregister(_name, _token);
            _owner = nullptr;
        }
    }

    PanelSlotHandle PanelSlots::Register(StringView name, PanelSlotDraw draw) {
        const uint64_t token = _nextToken++;
        const auto found = _slots.find(name);
        if (found != _slots.end()) {
            LOG_WARNING("[PanelSlots] the slot '{}' is registered again, the new drawer replaces the old one", name);
            found->second = Entry{std::move(draw), token};
        } else {
            _slots.emplace(String(name), Entry{std::move(draw), token});
        }
        return PanelSlotHandle(*this, String(name), token);
    }

    PanelSlotDraw* PanelSlots::Find(StringView name) {
        const auto found = _slots.find(name);
        return found != _slots.end() && found->second.draw ? &found->second.draw : nullptr;
    }

    void PanelSlots::Unregister(StringView name, uint64_t token) {
        const auto found = _slots.find(name);
        if (found != _slots.end() && found->second.token == token) {
            _slots.erase(found);
        }
    }

}  // namespace BECore
