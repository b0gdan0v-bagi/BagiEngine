#include "ControlLinkQuery.h"

#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        constexpr StringView kTokenParameter = "controlToken";
        constexpr StringView kAddressParameter = "control";
        constexpr StringView kClockParameter = "controlClock";
        constexpr StringView kManualClockValue = "manual";
        constexpr StringView kScheme = "ws://";
        constexpr Array<StringView, 3> kLoopbackHosts = {"localhost", "127.0.0.1", "[::1]"};
        constexpr uint16_t kDefaultLinkPort = 8766;  // the page port of bagi_bridge.py
        constexpr size_t kMaxTokenBytes = 128;

        [[nodiscard]] bool IsToken(StringView text) {
            return !text.empty() && text.size() <= kMaxTokenBytes &&
                   eastl::all_of(text.begin(), text.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
        }

        [[nodiscard]] bool IsPort(StringView text) {
            const Optional<uint16_t> port = StringUtils::ParseNumber<uint16_t>(text);
            return port && *port != 0;
        }

        /// A browser drops tabs and newlines before it parses an address and refuses a fragment, so only plain printable text is judged. The authority has to
        /// be exactly a loopback host and at most a port: a user name, a longer host name and a backslash all fail that
        [[nodiscard]] bool IsLoopbackAddress(StringView address) {
            if (!address.starts_with(kScheme) || !eastl::all_of(address.begin(), address.end(), [](char c) { return c > ' ' && c <= '~' && c != '#'; })) {
                return false;
            }
            StringView authority = address.substr(kScheme.size());
            authority = authority.substr(0, authority.find('/'));
            for (const StringView host : kLoopbackHosts) {
                if (authority.starts_with(host)) {
                    const StringView rest = authority.substr(host.size());
                    return rest.empty() || (rest.front() == ':' && IsPort(rest.substr(1)));
                }
            }
            return false;
        }

    }  // namespace

    std::expected<Optional<ControlLinkSettings>, String> ParseControlLinkQuery(StringView query) {
        if (query.starts_with("?")) {
            query.remove_prefix(1);
        }
        Optional<StringView> token;
        Optional<StringView> address;
        Optional<StringView> clock;
        for (const StringView part : StringUtils::Split(query, '&')) {
            const size_t equals = part.find('=');
            const StringView name = part.substr(0, equals);
            Optional<StringView>* slot = name == kTokenParameter ? &token : name == kAddressParameter ? &address : name == kClockParameter ? &clock : nullptr;
            if (slot != nullptr && !*slot) {
                *slot = equals == StringView::npos ? StringView() : part.substr(equals + 1);
            }
        }

        if (!token) {
            if (address || clock) {
                return std::unexpected(String("control and controlClock need controlToken"));
            }
            return Optional<ControlLinkSettings>();
        }
        if (!IsToken(*token)) {
            return std::unexpected(Format("controlToken has to be 1 to {} of A-Z a-z 0-9 _ -", kMaxTokenBytes));
        }

        ControlLinkSettings settings;
        settings.token = String(*token);
        if (!address) {
            settings.address = Format("{}localhost:{}", kScheme, kDefaultLinkPort);
        } else if (IsPort(*address)) {
            settings.address = Format("{}localhost:{}", kScheme, *address);
        } else if (IsLoopbackAddress(*address)) {
            settings.address = String(*address);
        } else {
            return std::unexpected(String("control has to be a port or a ws:// address of localhost, 127.0.0.1 or [::1]"));
        }
        if (clock) {
            if (*clock != kManualClockValue) {
                return std::unexpected(Format("controlClock can only be {}", kManualClockValue));
            }
            settings.manualClock = true;
        }
        return Optional<ControlLinkSettings>(std::move(settings));
    }

}  // namespace BECore
