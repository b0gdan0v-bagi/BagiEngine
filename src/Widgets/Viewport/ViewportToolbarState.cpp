#include "ViewportToolbarState.h"

namespace BECore {

    ViewportToolbarHandle::~ViewportToolbarHandle() {
        Reset();
    }

    ViewportToolbarHandle::ViewportToolbarHandle(ViewportToolbarHandle&& other) noexcept : _token(other._token) {
        other._token = 0;
    }

    ViewportToolbarHandle& ViewportToolbarHandle::operator=(ViewportToolbarHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _token = other._token;
            other._token = 0;
        }
        return *this;
    }

    void ViewportToolbarHandle::Reset() {
        if (_token != 0) {
            // The registry may be gone already (a test destroys it); there is then nothing to unregister
            if (ViewportToolbarRegistry::IsInstanceCreated()) {
                ViewportToolbarRegistry::GetInstance().Unregister(_token);
            }
            _token = 0;
        }
    }

    ViewportToolbarHandle ViewportToolbarRegistry::Register(IViewportToolbar& toolbar) {
        if (_toolbar != nullptr && _toolbar != &toolbar) {
            LOG_WARNING("[ViewportToolbar] a toolbar is registered again, the new one replaces the old one");
        }
        _toolbar = &toolbar;
        _token = _nextToken++;
        return ViewportToolbarHandle(_token);
    }

    void ViewportToolbarRegistry::Unregister(uint64_t token) {
        if (token == _token) {
            _toolbar = nullptr;
            _token = 0;
        }
    }

}  // namespace BECore
