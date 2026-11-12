#include "Clipboard.h"

#include <BECore/GameManager/CoreManager.h>
#include <Events/ClipboardEvents.h>

namespace BECore {

    namespace {

        /// Most pastes one Update() takes from the backend: a page that floods them cannot hold the frame
        constexpr int kMaxPastesPerUpdate = 8;

        /// Makes a backend's answer follow the contract: text only with Ok, an empty Ok is Empty, a long text is Refused
        void Normalize(ClipboardText& answer) {
            if (answer.status == ClipboardStatus::Ok) {
                if (answer.text.size() > kMaxClipboardBytes) {
                    answer.status = ClipboardStatus::Refused;
                } else if (answer.text.empty()) {
                    answer.status = ClipboardStatus::Empty;
                }
            }
            if (answer.status != ClipboardStatus::Ok) {
                answer.text.clear();
            }
        }

    }  // namespace

    void Clipboard::SetBackend(IntrusivePtr<IClipboardBackend> backend) {
        if (_readInFlight) {
            // The read belonged to the old backend: its requests are asked again, first in line
            _readInFlight = false;
            _waiting.insert(_waiting.begin(), eastl::make_move_iterator(_reading.begin()), eastl::make_move_iterator(_reading.end()));
            _reading.clear();
        }
        _backend = std::move(backend);
    }

    bool Clipboard::IsAvailable() const {
        return _backend != nullptr && _backend->IsAvailable();
    }

    bool Clipboard::SetText(eastl::string_view text) {
        if (!IsAvailable() || text.size() > kMaxClipboardBytes) {
            return false;
        }
        return _backend->WriteText(text);
    }

    void Clipboard::RequestText(ClipboardCallback callback) {
        if (callback) {
            _waiting.push_back(std::move(callback));
        }
    }

    void Clipboard::RequestPaste() {
        RequestText([](const ClipboardText& answer) {
            if (answer.status == ClipboardStatus::Ok) {
                ClipboardEvents::PasteEvent::Emit(answer.text);
            }
        });
    }

    void Clipboard::Update() {
        if (!_answering.empty()) {
            return;  // called from a callback: the outer Update goes on by itself
        }
        if (!IsAvailable()) {
            if (_readInFlight || !_waiting.empty()) {
                _readInFlight = false;
                _result.status = ClipboardStatus::Unavailable;
                _result.text.clear();
                AnswerAll(_result);
            }
            return;
        }
        EmitPasted();
        StartReadIfNeeded();
        if (_readInFlight) {
            _result.text.clear();
            if (_backend->PollRead(_result)) {
                _readInFlight = false;
                Normalize(_result);
                AnswerAll(_result);
            }
        }
    }

    void Clipboard::EmitPasted() {
        for (int taken = 0; taken < kMaxPastesPerUpdate; ++taken) {
            _pasted.text.clear();
            if (!_backend->TakePasted(_pasted)) {
                return;
            }
            Normalize(_pasted);
            // A paste the game was not running for (a pause) is dropped, not kept for later
            if (_pasted.status == ClipboardStatus::Ok && !CoreManager::GetPause().IsPaused()) {
                ClipboardEvents::PasteEvent::Emit(_pasted.text);
            }
        }
    }

    void Clipboard::StartReadIfNeeded() {
        if (_readInFlight || _waiting.empty()) {
            return;
        }
        for (ClipboardCallback& callback : _waiting) {
            _reading.push_back(std::move(callback));
        }
        _waiting.clear();
        _readInFlight = true;
        _backend->BeginRead();
    }

    void Clipboard::AnswerAll(const ClipboardText& result) {
        // The requests that came while the read ran share its result; a request made by a callback waits for the next Update()
        for (ClipboardCallback& callback : _reading) {
            _answering.push_back(std::move(callback));
        }
        for (ClipboardCallback& callback : _waiting) {
            _answering.push_back(std::move(callback));
        }
        _reading.clear();
        _waiting.clear();
        for (size_t i = 0; i < _answering.size(); ++i) {
            _answering[i](result);
        }
        _answering.clear();
    }

}  // namespace BECore
