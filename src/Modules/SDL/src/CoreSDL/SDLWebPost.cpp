#include "SDLWebPost.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#include <emscripten/threading.h>
#endif

namespace BECore {

    namespace {

        constexpr unsigned char kFirstPrintable = 0x21;
        constexpr unsigned char kLastPrintable = 0x7e;

    }  // namespace

    bool IsWebPostPathValid(eastl::string_view path) {
        if (path.empty() || path.size() > kWebPostMaxPathBytes) {
            return false;
        }
        if (path[0] != '/' || (path.size() > 1 && path[1] == '/') || path.find("..") != eastl::string_view::npos) {
            return false;
        }
        for (const char c : path) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < kFirstPrintable || byte > kLastPrintable || c == '\\') {
                return false;
            }
        }
        return true;
    }

#ifdef __EMSCRIPTEN__
    namespace {

        // The page's script and its answers live on the main thread (a Worker has no window)
        void CheckOwner() {
            ASSERT(emscripten_is_main_browser_thread(), "WebPost is used off the main thread");
        }

        // The JavaScript of EM_ASM is stringized as it stands, so clang-format must not touch it (it splits === into == =)
        // clang-format off
        bool PageHasWebPost() {
            return EM_ASM_INT({
                if (typeof window === 'undefined') {
                    return 0;
                }
                var post = window.bagiWebPost;
                return post && typeof post.start === 'function' && typeof post.poll === 'function' ? 1 : 0;
            }) != 0;
        }

        // The texts reach the page as UTF-8 bytes of the wasm memory. The copy by slice() is needed: in a threaded build the memory
        // is shared, and a TextDecoder refuses a view of a shared buffer. Returns the id of the page's script, 0 when it refused.
        int PostFromPage(const char* path, int pathSize, const char* body, int bodySize, const char* contentType, int contentTypeSize) {
            return EM_ASM_INT({
                var decoder = new TextDecoder();
                var read = function (pointer, size) {
                    return decoder.decode(HEAPU8.slice(pointer, pointer + size));
                };
                return window.bagiWebPost.start(read($0, $1), read($2, $3), read($4, $5));
            }, path, pathSize, body, bodySize, contentType, contentTypeSize);
        }

        int PollPage(WebPostId id) {
            return EM_ASM_INT({
                var post = typeof window === 'undefined' ? null : window.bagiWebPost;
                return post ? post.poll($0) : 0;
            }, static_cast<int>(id));
        }
        // clang-format on

        constexpr int kPagePending = -1;

    }  // namespace

    bool WebPostIsAvailable() {
        CheckOwner();
        return PageHasWebPost();
    }
#else
    bool WebPostIsAvailable() {
        return false;
    }
#endif

    std::expected<WebPostId, eastl::string> WebPostStart(eastl::string_view path, eastl::string_view body, eastl::string_view contentType) {
        if (!IsWebPostPathValid(path)) {
            return std::unexpected(eastl::string("bad path"));
        }
        if (body.size() > kWebPostMaxBodyBytes) {
            return std::unexpected(eastl::string("body too large"));
        }
        if (contentType.size() > kWebPostMaxContentTypeBytes) {
            return std::unexpected(eastl::string("bad content type"));
        }
#ifdef __EMSCRIPTEN__
        if (!WebPostIsAvailable()) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG_WARNING("[SDLWebPost] the page has no window.bagiWebPost (a custom shell?): nothing can be posted");
            }
            return std::unexpected(eastl::string("unavailable"));
        }
        // The sizes are bounded above, so they fit an int
        const int id = PostFromPage(path.data(), static_cast<int>(path.size()), body.data(), static_cast<int>(body.size()), contentType.data(), static_cast<int>(contentType.size()));
        if (id <= 0) {
            return std::unexpected(eastl::string("refused"));
        }
        return static_cast<WebPostId>(id);
#else
        return std::unexpected(eastl::string("unavailable"));
#endif
    }

    WebPostResult WebPostPoll([[maybe_unused]] WebPostId id) {
#ifdef __EMSCRIPTEN__
        if (id == 0) {
            return {WebPostStatus::Failed, 0};
        }
        CheckOwner();
        const int answer = PollPage(id);
        if (answer == kPagePending) {
            return {WebPostStatus::Pending, 0};
        }
        if (answer <= 0) {
            return {WebPostStatus::Failed, 0};
        }
        return {WebPostStatus::Done, answer};
#else
        return {WebPostStatus::Failed, 0};
#endif
    }

}  // namespace BECore
