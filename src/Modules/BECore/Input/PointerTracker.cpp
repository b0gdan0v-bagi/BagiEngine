#include "PointerTracker.h"

#include <EASTL/algorithm.h>

namespace BECore {

    bool PointerTracker::BeginCapture(uint32_t which, uint8_t button, IUIInputHandler& handler) {
        auto it = eastl::find_if(_captures.begin(), _captures.end(), [&](const Capture& capture) { return capture.which == which && capture.button == button; });
        if (it != _captures.end()) {
            it->handler = &handler;
            return true;
        }
        if (_captures.size() == kMaxEntries) {
            return false;
        }
        _captures.push_back(Capture{which, button, &handler});
        return true;
    }

    IUIInputHandler* PointerTracker::FindCapture(uint32_t which, uint8_t button) const {
        auto it = eastl::find_if(_captures.begin(), _captures.end(), [&](const Capture& capture) { return capture.which == which && capture.button == button; });
        return it != _captures.end() ? it->handler : nullptr;
    }

    IUIInputHandler* PointerTracker::FindCaptureOfPointer(uint32_t which) const {
        auto it = eastl::find_if(_captures.begin(), _captures.end(), [&](const Capture& capture) { return capture.which == which; });
        return it != _captures.end() ? it->handler : nullptr;
    }

    void PointerTracker::EndCapture(uint32_t which, uint8_t button) {
        auto it = eastl::find_if(_captures.begin(), _captures.end(), [&](const Capture& capture) { return capture.which == which && capture.button == button; });
        if (it != _captures.end()) {
            _captures.erase(it);
        }
    }

    IUIInputHandler* PointerTracker::SetHover(uint32_t which, IUIInputHandler* handler) {
        auto it = eastl::find_if(_hovers.begin(), _hovers.end(), [&](const Hover& hover) { return hover.which == which; });
        if (it == _hovers.end()) {
            if (handler != nullptr && _hovers.size() < kMaxEntries) {
                _hovers.push_back(Hover{which, handler});
            }
            return nullptr;
        }

        IUIInputHandler* previous = it->handler;
        if (handler == nullptr) {
            _hovers.erase(it);
        } else {
            it->handler = handler;
        }
        return previous;
    }

    IUIInputHandler* PointerTracker::GetHover(uint32_t which) const {
        auto it = eastl::find_if(_hovers.begin(), _hovers.end(), [&](const Hover& hover) { return hover.which == which; });
        return it != _hovers.end() ? it->handler : nullptr;
    }

    void PointerTracker::Forget(const IUIInputHandler& handler) {
        _captures.erase(eastl::remove_if(_captures.begin(), _captures.end(), [&](const Capture& capture) { return capture.handler == &handler; }), _captures.end());
        _hovers.erase(eastl::remove_if(_hovers.begin(), _hovers.end(), [&](const Hover& hover) { return hover.handler == &handler; }), _hovers.end());
    }

    void PointerTracker::Clear() {
        _captures.clear();
        _hovers.clear();
    }

}  // namespace BECore
