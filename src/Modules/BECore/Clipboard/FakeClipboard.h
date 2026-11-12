#pragma once

#include <BECore/Clipboard/Clipboard.h>
#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    /// A clipboard in memory for tests, with switches for every outcome: the real system clipboard is never touched by a test
    class FakeClipboard : public IClipboardBackend {
    public:
        FakeClipboard() = default;

        explicit FakeClipboard(eastl::string_view content) : _content(content) {}

        [[nodiscard]] bool IsAvailable() const override {
            return _available;
        }

        bool WriteText(eastl::string_view text) override {
            ++_writeCount;
            if (_refuse) {
                return false;
            }
            _content.assign(text.data(), text.size());
            return true;
        }

        void BeginRead() override {
            ++_readCount;
            _pollsLeft = _deferredPolls;
        }

        bool PollRead(ClipboardText& out) override {
            if (_pollsLeft > 0) {
                --_pollsLeft;
                return false;
            }
            if (_refuse) {
                out.status = ClipboardStatus::Refused;
            } else if (_content.empty()) {
                out.status = ClipboardStatus::Empty;
            } else {
                out.status = ClipboardStatus::Ok;
                out.text = _content;
            }
            return true;
        }

        bool TakePasted(ClipboardText& out) override {
            if (_pasted.empty()) {
                return false;
            }
            out.status = ClipboardStatus::Ok;
            out.text = std::move(_pasted.front());
            _pasted.erase(_pasted.begin());
            return true;
        }

        void SetContent(eastl::string_view content) {
            _content.assign(content.data(), content.size());
        }

        [[nodiscard]] const eastl::string& Content() const {
            return _content;
        }

        void SetAvailable(bool available) {
            _available = available;
        }

        /// Reads answer Refused and writes return false
        void SetRefuse(bool refuse) {
            _refuse = refuse;
        }

        /// A read finishes after @p polls polls that say "not yet" (0: at once)
        void SetDeferred(int polls) {
            _deferredPolls = polls;
        }

        /// A paste the platform announces by itself, handed out by TakePasted
        void QueuePasted(eastl::string_view text) {
            _pasted.emplace_back(text.data(), text.size());
        }

        [[nodiscard]] int WriteCount() const {
            return _writeCount;
        }

        [[nodiscard]] int ReadCount() const {
            return _readCount;
        }

    private:
        eastl::string _content;
        eastl::vector<eastl::string> _pasted;
        bool _available = true;
        bool _refuse = false;
        int _deferredPolls = 0;
        int _pollsLeft = 0;
        int _writeCount = 0;
        int _readCount = 0;
    };

    /// Installs a backend on the clipboard of CoreManager and puts the old one back, so a test never leaks a fake into the next
    class ScopedClipboardBackend {
    public:
        explicit ScopedClipboardBackend(IntrusivePtr<IClipboardBackend> backend) : _previous(CoreManager::GetClipboard().GetBackend()) {
            CoreManager::GetClipboard().SetBackend(std::move(backend));
        }

        ~ScopedClipboardBackend() {
            CoreManager::GetClipboard().SetBackend(std::move(_previous));
        }

        ScopedClipboardBackend(const ScopedClipboardBackend&) = delete;
        ScopedClipboardBackend& operator=(const ScopedClipboardBackend&) = delete;
        ScopedClipboardBackend(ScopedClipboardBackend&&) = delete;
        ScopedClipboardBackend& operator=(ScopedClipboardBackend&&) = delete;

    private:
        IntrusivePtr<IClipboardBackend> _previous;
    };

}  // namespace BECore
