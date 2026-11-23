#include "SDLWebDownload.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#include <emscripten/threading.h>
#endif

namespace BECore {

    namespace {

        constexpr unsigned char kFirstPrintable = 0x21;
        constexpr unsigned char kLastPrintable = 0x7e;

        // A space is allowed in a mime type parameter ("text/plain; charset=utf-8")
        bool IsMimeTypeValid(StringView mimeType) {
            for (const char c : mimeType) {
                const auto byte = static_cast<unsigned char>(c);
                if (byte < ' ' || byte > kLastPrintable) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    bool IsWebDownloadNameValid(StringView name) {
        if (name.empty() || name.size() > kWebDownloadMaxNameBytes || name.find("..") != StringView::npos) {
            return false;
        }
        for (const char c : name) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < kFirstPrintable || byte > kLastPrintable || c == '/' || c == '\\') {
                return false;
            }
        }
        return true;
    }

#ifdef __EMSCRIPTEN__
    namespace {

        // The page's document lives on the main thread (a Worker has no window)
        void CheckOwner() {
            ASSERT(emscripten_is_main_browser_thread(), "WebDownload is used off the main thread");
        }

        // The JavaScript of EM_ASM is stringized as it stands, so clang-format must not touch it
        // clang-format off
        // The bytes are copied by slice() first: in a threaded build the memory is shared and a Blob refuses a view of it.
        // The link is clicked and dropped at once; the object URL is released later, after the browser has taken the Blob.
        void DownloadFromPage(const char* name, int nameSize, const char* content, int contentSize, const char* mimeType, int mimeTypeSize) {
            EM_ASM({
                var decoder = new TextDecoder();
                var fileName = decoder.decode(HEAPU8.slice($0, $0 + $1));
                var type = decoder.decode(HEAPU8.slice($4, $4 + $5));
                var blob = new Blob([HEAPU8.slice($2, $2 + $3)], { type: type });
                var url = URL.createObjectURL(blob);
                var link = document.createElement('a');
                link.href = url;
                link.download = fileName;
                link.style.display = 'none';
                document.body.appendChild(link);
                link.click();
                document.body.removeChild(link);
                setTimeout(function () { URL.revokeObjectURL(url); }, 10000);
            }, name, nameSize, content, contentSize, mimeType, mimeTypeSize);
        }
        // clang-format on

    }  // namespace
#endif

    std::expected<void, String> WebDownloadStart(StringView name, StringView content, StringView mimeType) {
        if (!IsWebDownloadNameValid(name)) {
            return std::unexpected(String("bad name"));
        }
        if (!IsMimeTypeValid(mimeType)) {
            return std::unexpected(String("bad mime type"));
        }
#ifdef __EMSCRIPTEN__
        CheckOwner();
        constexpr StringView kDefaultMimeType = "application/octet-stream";
        const StringView type = mimeType.empty() ? kDefaultMimeType : mimeType;
        // The sizes reach the page as int
        constexpr size_t kMaxContentBytes = 1024 * 1024 * 1024;
        if (content.size() > kMaxContentBytes) {
            return std::unexpected(String("content too large"));
        }
        DownloadFromPage(name.data(), static_cast<int>(name.size()), content.data(), static_cast<int>(content.size()), type.data(), static_cast<int>(type.size()));
        return {};
#else
        static_cast<void>(content);
        return std::unexpected(String("not in the browser"));
#endif
    }

}  // namespace BECore
