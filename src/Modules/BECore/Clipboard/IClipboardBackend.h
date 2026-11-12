#pragma once

#include <BECore/Clipboard/ClipboardTypes.h>

namespace BECore {

    /**
     * @brief The platform half of the clipboard: SDL on the desktop, the page's navigator.clipboard in the browser, FakeClipboard in tests.
     *
     * A backend is dumb: it holds no queue and applies no limit; Clipboard (the facade) orders the requests, shares one read,
     * checks the size and the status. Main thread only.
     */
    class IClipboardBackend : public RefCounted {
    public:
        ~IClipboardBackend() override = default;

        /// Whether the platform has a clipboard API at all. False answers every read and write with Unavailable
        [[nodiscard]] virtual bool IsAvailable() const = 0;

        /// @return true when the platform took the call; a web write can still fail later
        virtual bool WriteText(eastl::string_view text) = 0;

        /// Starts a read; the result comes from PollRead(). Called only when no read is in flight
        virtual void BeginRead() = 0;

        /// @return true once, when the read started by BeginRead() has finished and @p out holds its result (it may be true at once)
        virtual bool PollRead(ClipboardText& out) = 0;

        /// @return true when the platform announced a paste by itself (a menu command of a browser) and @p out holds it
        virtual bool TakePasted(ClipboardText& /*out*/) {
            return false;
        }
    };

}  // namespace BECore
