#pragma once

namespace BECore {

    /// Where a post stands. Done means the server answered, with whatever HTTP status: the caller judges the status.
    CORE_ENUM(WebPostStatus, uint8_t, Unavailable, Pending, Done, Failed)

    /// A post in flight, numbered by the page; 0 is none
    using WebPostId = uint32_t;

    struct WebPostResult {
        WebPostStatus status = WebPostStatus::Unavailable;
        int httpStatus = 0;  ///< the HTTP status when `status` is Done, else 0
    };

    /// The longest body a post may carry (the limit of the server's endpoint)
    inline constexpr size_t kWebPostMaxBodyBytes = 1024 * 1024;

    /// The longest path a post may name
    inline constexpr size_t kWebPostMaxPathBytes = 2048;

    /// The longest content type a post may name (a real one is a few dozen bytes)
    inline constexpr size_t kWebPostMaxContentTypeBytes = 256;

    /**
     * @brief Whether `path` is a path of the page's own origin that a post may name: one leading '/', not "//", printable ASCII only
     * (no space, control character or non-ASCII), no backslash, no "..", at most kWebPostMaxPathBytes. The same rule as
     * window.bagiWebPost in web/shell.html. Pure, so it runs, and is tested, on every platform.
     */
    [[nodiscard]] bool IsWebPostPathValid(StringView path);

    /// Whether this build can post: a browser build whose page defines window.bagiWebPost. Every other build says false.
    [[nodiscard]] bool WebPostIsAvailable();

    /**
     * @brief Starts a POST of `body` to `path` of the page's own origin (the page's Basic credentials ride along) and returns at once.
     *
     * An empty `contentType` means application/x-ndjson. The error names the refusal: "bad path", "body too large", "bad content type", "unavailable"
     * (a build that is not a browser one, or a page without window.bagiWebPost) or "refused" (the page has too many posts in flight).
     * The answer is read with WebPostPoll. Browser main thread only; asserted in Debug.
     */
    [[nodiscard]] std::expected<WebPostId, eastl::string> WebPostStart(StringView path, StringView body, StringView contentType);

    /**
     * @brief The state of a post: Pending until the answer arrives, then Done with the HTTP status or Failed (network failure) once;
     * after that the id is forgotten. An unknown id, 0 and every id of a build that cannot post are Failed. No allocation.
     */
    [[nodiscard]] WebPostResult WebPostPoll(WebPostId id);

}  // namespace BECore
