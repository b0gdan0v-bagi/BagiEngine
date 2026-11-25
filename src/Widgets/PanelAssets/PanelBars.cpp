#include "PanelBars.h"

namespace BECore {

    PanelBarHandle::~PanelBarHandle() {
        Reset();
    }

    PanelBarHandle::PanelBarHandle(PanelBarHandle&& other) noexcept : _name(std::move(other._name)), _token(other._token) {
        other._token = 0;
    }

    PanelBarHandle& PanelBarHandle::operator=(PanelBarHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _name = std::move(other._name);
            _token = other._token;
            other._token = 0;
        }
        return *this;
    }

    void PanelBarHandle::Reset() {
        if (_token != 0) {
            // The registry may be gone already (a test destroys it); there is then nothing to unregister
            if (PanelBarsRegistry::IsInstanceCreated()) {
                PanelBarsRegistry::GetInstance().Unregister(_name, _token);
            }
            _token = 0;
        }
    }

    PanelBarHandle PanelBarsRegistry::Register(StringView name, IPanelBar& bar) {
        const uint64_t token = _nextToken++;
        const auto found = _bars.find(name);
        if (found != _bars.end()) {
            LOG_WARNING("[PanelBars] the bar '{}' is registered again, the new one replaces the old one", name);
            found->second = Entry{&bar, token};
        } else {
            _bars.emplace(String(name), Entry{&bar, token});
        }
        return PanelBarHandle(String(name), token);
    }

    bool PanelBarsRegistry::Draw(StringView name) {
        if (!IsInstanceCreated()) {
            return false;
        }
        const PanelBarsRegistry& registry = GetInstance();
        const auto found = registry._bars.find(name);
        if (found == registry._bars.end()) {
            return false;
        }
        found->second.bar->DrawBar();
        return true;
    }

    void PanelBarsRegistry::Unregister(StringView name, uint64_t token) {
        const auto found = _bars.find(name);
        if (found != _bars.end() && found->second.token == token) {
            _bars.erase(found);
        }
    }

}  // namespace BECore
