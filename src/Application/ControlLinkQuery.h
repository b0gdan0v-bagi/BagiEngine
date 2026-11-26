#pragma once

#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// What a page needs to dial the bridge of the control link (.claude/rules/control-web-link.md)
    struct ControlLinkSettings {
        /// A `ws://` address of the loopback
        String address;
        /// The secret of the bridge: never logged
        String token;
        bool manualClock = false;
    };

    /**
     * @brief Reads the link's parameters from the query of a page's address (`location.search`, with or without its `?`).
     *
     * `controlToken` is the switch: without it there is nothing to dial. The values are taken as written (no percent-decoding), the first of a repeated
     * parameter counts, and other parameters are the page's own. The query is untrusted (anyone can craft a link to a page), so only an address of the
     * loopback is accepted: whoever holds the other end drives the game. The error is a message for the log and holds no value of the query.
     */
    [[nodiscard]] std::expected<Optional<ControlLinkSettings>, String> ParseControlLinkQuery(StringView query);

}  // namespace BECore
