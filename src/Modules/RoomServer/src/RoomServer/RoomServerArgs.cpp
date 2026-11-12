#include "RoomServerArgs.h"

namespace BECore {

    namespace {

        constexpr size_t kMaxConnectionsLimit = 4096;
        constexpr int64_t kMaxMillisecondsLimit = 24 * 60 * 60 * 1000;
        constexpr size_t kMaxOriginBytes = 200;

        std::expected<int64_t, eastl::string> ParseNumber(eastl::string_view flag, eastl::string_view text, int64_t min, int64_t max) {
            int64_t value = 0;
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            const auto [stop, error] = std::from_chars(begin, end, value);
            if (text.empty() || error != std::errc() || stop != end) {
                return std::unexpected(Format("{} needs a number, got '{}'", flag, text));
            }
            if (value < min || value > max) {
                return std::unexpected(Format("{} has to be from {} to {}, got {}", flag, min, max, value));
            }
            return value;
        }

        std::expected<std::chrono::milliseconds, eastl::string> ParseMilliseconds(eastl::string_view flag, eastl::string_view text) {
            const auto value = ParseNumber(flag, text, 0, kMaxMillisecondsLimit);
            if (!value) {
                return std::unexpected(value.error());
            }
            return std::chrono::milliseconds(*value);
        }

        /// An entry the allow-list can match: `*`, `null` or `scheme://host[:port]`. One that matches nothing (a path, a bare host) would
        /// only lock every page out, so it is told at start. The check is the one the handshake makes: the entry has to admit itself
        bool IsUsableOrigin(eastl::string_view origin) {
            if (origin.size() > kMaxOriginBytes) {
                return false;
            }
            HandshakePolicy policy;
            policy.loopbackOnly = false;
            policy.allowedOrigins.emplace_back(origin);
            return IsOriginAllowed(origin, policy);
        }

    }  // namespace

    std::expected<RoomServerArgs, eastl::string> ParseRoomServerArgs(int argc, const char* const* argv) {
        RoomServerArgs args;
        for (int i = 1; i < argc; ++i) {
            const eastl::string_view flag = argv[i];
            if (flag == "--help" || flag == "-h") {
                args.help = true;
                continue;
            }
            if (flag == "--test-games") {
                args.testGames = true;
                continue;
            }

            const bool isKnown = flag == "--port" || flag == "--bind" || flag == "--origin" || flag == "--max-connections" || flag == "--max-per-address" || flag == "--computer-delay-ms" ||
                                 flag == "--reconnect-grace-ms" || flag == "--ping-interval-ms";
            if (!isKnown) {
                return std::unexpected(Format("unknown argument '{}' (see --help)", flag));
            }
            if (i + 1 >= argc) {
                return std::unexpected(Format("{} needs a value", flag));
            }
            const eastl::string_view value = argv[++i];

            if (flag == "--bind") {
                if (value == "loopback") {
                    args.bind = SocketBind::Loopback;
                } else if (value == "any") {
                    args.bind = SocketBind::AnyAddress;
                } else {
                    return std::unexpected(Format("--bind is 'loopback' or 'any', got '{}'", value));
                }
            } else if (flag == "--origin") {
                if (!IsUsableOrigin(value)) {
                    return std::unexpected(Format("--origin needs an origin such as https://example.com:8443 (scheme, host and optional port, no path; or *), got '{}'", value));
                }
                args.origins.emplace_back(value);
            } else if (flag == "--computer-delay-ms" || flag == "--reconnect-grace-ms" || flag == "--ping-interval-ms") {
                const auto milliseconds = ParseMilliseconds(flag, value);
                if (!milliseconds) {
                    return std::unexpected(milliseconds.error());
                }
                if (flag == "--computer-delay-ms") {
                    args.computerDelay = *milliseconds;
                } else if (flag == "--reconnect-grace-ms") {
                    args.reconnectGrace = *milliseconds;
                } else {
                    args.pingInterval = *milliseconds;
                }
            } else {
                const bool isPort = flag == "--port";
                const bool hasZero = isPort || flag == "--max-per-address";
                const int64_t max = isPort ? std::numeric_limits<uint16_t>::max() : static_cast<int64_t>(kMaxConnectionsLimit);
                const auto number = ParseNumber(flag, value, hasZero ? 0 : 1, max);
                if (!number) {
                    return std::unexpected(number.error());
                }
                if (isPort) {
                    args.port = static_cast<uint16_t>(*number);
                } else if (flag == "--max-connections") {
                    args.maxConnections = static_cast<size_t>(*number);
                } else {
                    args.maxConnectionsPerAddress = static_cast<size_t>(*number);
                }
            }
        }
        return args;
    }

    eastl::string GetRoomServerUsage() {
        const RoomServerArgs defaults;
        return Format("Usage: BagiRoomServer [options]\n"
                      "  --port N                 TCP port, 0 picks a free one (default {})\n"
                      "  --bind loopback|any      the machine's own address only, or every address (default any)\n"
                      "  --origin ORIGIN          a web page that may connect, e.g. https://example.com; repeat for more; '*' lets every page in\n"
                      "                           (development only). Without it browsers are refused and programs are let in\n"
                      "  --max-connections N      connections the hub admits (default {})\n"
                      "  --max-per-address N      open sockets of one client address, 0 is off (default {})\n"
                      "  --computer-delay-ms N    pause between two moves of computers (default {})\n"
                      "  --reconnect-grace-ms N   how long a dropped player keeps the seat (default {})\n"
                      "  --ping-interval-ms N     a ping after this much silence, a drop after twice that, 0 is off (default {})\n"
                      "  --test-games             register the test games (cards-leaky) next to cards\n"
                      "  --help                   this text\n",
                      defaults.port, defaults.maxConnections, defaults.maxConnectionsPerAddress, defaults.computerDelay.count(), defaults.reconnectGrace.count(), defaults.pingInterval.count());
    }

    RoomServerSettings ToSettings(const RoomServerArgs& args) {
        RoomServerSettings settings;
        settings.port = args.port;
        settings.bind = args.bind;
        settings.policy.loopbackOnly = false;
        settings.policy.allowedOrigins = args.origins;
        settings.hub.maxConnections = args.maxConnections;
        settings.maxConnections = args.maxConnections + kHandshakeConnectionReserve;
        settings.maxConnectionsPerAddress = args.maxConnectionsPerAddress;
        settings.hub.timings.computerDelay = args.computerDelay;
        settings.hub.timings.reconnectGrace = args.reconnectGrace;
        settings.pingInterval = args.pingInterval;
        return settings;
    }

}  // namespace BECore
