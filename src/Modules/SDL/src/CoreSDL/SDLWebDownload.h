#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <expected>

namespace BECore {

    /// The longest file name a download may carry
    inline constexpr size_t kWebDownloadMaxNameBytes = 128;

    /**
     * @brief Whether `name` is a plain file name a download may carry: printable ASCII without '/', '\\' and "..", not empty,
     * at most kWebDownloadMaxNameBytes. The browser takes the name for the saved file, so a path must not get through.
     * Pure, so it runs, and is tested, on every platform.
     */
    [[nodiscard]] bool IsWebDownloadNameValid(StringView name);

    /**
     * @brief Hands `content` to the page as a file `name` of type `mimeType` (the browser saves it) and returns at once.
     *
     * An empty `mimeType` means application/octet-stream. The error is "bad name", "bad mime type" (a control character or a
     * non-ASCII byte in it) or, outside the browser, "not in the browser". Browser main thread only; asserted in Debug.
     */
    [[nodiscard]] std::expected<void, String> WebDownloadStart(StringView name, StringView content, StringView mimeType);

}  // namespace BECore
