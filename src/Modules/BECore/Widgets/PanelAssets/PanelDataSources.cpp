#include "PanelDataSources.h"

namespace BECore {

    PanelSourceHandle::~PanelSourceHandle() {
        Reset();
    }

    PanelSourceHandle::PanelSourceHandle(PanelSourceHandle&& other) noexcept : _owner(other._owner), _name(std::move(other._name)), _token(other._token) {
        other._owner = nullptr;
    }

    PanelSourceHandle& PanelSourceHandle::operator=(PanelSourceHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _owner = other._owner;
            _name = std::move(other._name);
            _token = other._token;
            other._owner = nullptr;
        }
        return *this;
    }

    void PanelSourceHandle::Reset() {
        if (_owner != nullptr) {
            _owner->Unregister(_name, _token);
            _owner = nullptr;
        }
    }

    PanelSourceHandle PanelDataSources::Register(StringView name, PanelSourceProvider provider) {
        const uint64_t token = _nextToken++;
        const auto found = _sources.find(name);
        if (found != _sources.end()) {
            LOG_WARNING("[PanelDataSources] the source '{}' is registered again, the new provider replaces the old one", name);
            found->second = Entry{std::move(provider), token};
        } else {
            _sources.emplace(String(name), Entry{std::move(provider), token});
        }
        return PanelSourceHandle(*this, String(name), token);
    }

    TypedRef PanelDataSources::Find(StringView name) const {
        const auto found = _sources.find(name);
        if (found == _sources.end()) {
            return _parent != nullptr ? _parent->Find(name) : TypedRef{};
        }
        return found->second.provider ? found->second.provider() : TypedRef{};
    }

    bool PanelDataSources::Contains(StringView name) const {
        return _sources.find(name) != _sources.end() || (_parent != nullptr && _parent->Contains(name));
    }

    void PanelDataSources::SetParent(const PanelDataSources* parent) {
        if (parent == this) {
            LOG_ERROR("[PanelDataSources] a registry cannot be its own parent, the parent is not set");
            return;
        }
        _parent = parent;
    }

    void PanelDataSources::Unregister(StringView name, uint64_t token) {
        const auto found = _sources.find(name);
        if (found != _sources.end() && found->second.token == token) {
            _sources.erase(found);
        }
    }

}  // namespace BECore
