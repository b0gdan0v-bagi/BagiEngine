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
        if (found == _sources.end() || !found->second.provider) {
            return {};
        }
        return found->second.provider();
    }

    bool PanelDataSources::Contains(StringView name) const {
        return _sources.find(name) != _sources.end();
    }

    void PanelDataSources::Unregister(StringView name, uint64_t token) {
        const auto found = _sources.find(name);
        if (found != _sources.end() && found->second.token == token) {
            _sources.erase(found);
        }
    }

}  // namespace BECore
