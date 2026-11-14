#include "RoomServerArgs.h"

namespace BECore {

    namespace {

        constexpr size_t kMaxConnectionsLimit = 4096;
        constexpr int64_t kMaxMillisecondsLimit = 24 * 60 * 60 * 1000;
        constexpr int64_t kMaxShutdownGraceMs = 60'000;  // a platform kills a process that takes longer to stop
        constexpr int64_t kMaxRoomsPerAddressLimit = 64;
        constexpr int64_t kMaxTrustedProxyHopsLimit = 4;
        constexpr size_t kMaxOriginBytes = 200;
        constexpr const char* kTestGamesVariable = "BAGI_ROOMS_TEST_GAMES";

        /// The settings that take a value, by the flag and the environment variable that set them
        enum class Option : uint8_t {
            Port,
            Bind,
            Origin,
            MaxConnections,
            MaxPerAddress,
            MaxRoomsPerAddress,
            LobbyTimeout,
            TrustedProxyHops,
            ComputerDelay,
            ReconnectGrace,
            PingInterval,
            StatsInterval,
            ShutdownGrace
        };

        struct OptionInfo {
            Option option;
            const char* flag;
            const char* variable;
        };

        constexpr Array<OptionInfo, 13> kOptions{
            OptionInfo{Option::Port, "--port", "PORT"},
            OptionInfo{Option::Bind, "--bind", "BAGI_ROOMS_BIND"},
            OptionInfo{Option::Origin, "--origin", "BAGI_ROOMS_ORIGINS"},
            OptionInfo{Option::MaxConnections, "--max-connections", "BAGI_ROOMS_MAX_CONNECTIONS"},
            OptionInfo{Option::MaxPerAddress, "--max-per-address", "BAGI_ROOMS_MAX_PER_ADDRESS"},
            OptionInfo{Option::MaxRoomsPerAddress, "--max-rooms-per-address", "BAGI_ROOMS_MAX_ROOMS_PER_ADDRESS"},
            OptionInfo{Option::LobbyTimeout, "--lobby-timeout-ms", "BAGI_ROOMS_LOBBY_TIMEOUT_MS"},
            OptionInfo{Option::TrustedProxyHops, "--trusted-proxy-hops", "BAGI_ROOMS_TRUSTED_PROXY_HOPS"},
            OptionInfo{Option::ComputerDelay, "--computer-delay-ms", "BAGI_ROOMS_COMPUTER_DELAY_MS"},
            OptionInfo{Option::ReconnectGrace, "--reconnect-grace-ms", "BAGI_ROOMS_RECONNECT_GRACE_MS"},
            OptionInfo{Option::PingInterval, "--ping-interval-ms", "BAGI_ROOMS_PING_INTERVAL_MS"},
            OptionInfo{Option::StatsInterval, "--stats-interval-ms", "BAGI_ROOMS_STATS_INTERVAL_MS"},
            OptionInfo{Option::ShutdownGrace, "--shutdown-grace-ms", "BAGI_ROOMS_SHUTDOWN_GRACE_MS"},
        };

        const OptionInfo* FindOptionByFlag(StringView flag) {
            for (const OptionInfo& info : kOptions) {
                if (flag == info.flag) {
                    return &info;
                }
            }
            return nullptr;
        }

        StringView Trim(StringView text) {
            constexpr StringView kBlanks = " \t\r\n";
            const size_t first = text.find_first_not_of(kBlanks);
            if (first == StringView::npos) {
                return {};
            }
            return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
        }

        std::expected<int64_t, String> ParseNumber(StringView name, StringView text, int64_t min, int64_t max) {
            int64_t value = 0;
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            const auto [stop, error] = std::from_chars(begin, end, value);
            if (text.empty() || error != std::errc() || stop != end) {
                return std::unexpected(Format("{} needs a number, got '{}'", name, text));
            }
            if (value < min || value > max) {
                return std::unexpected(Format("{} has to be from {} to {}, got {}", name, min, max, value));
            }
            return value;
        }

        std::expected<std::chrono::milliseconds, String> ParseMilliseconds(StringView name, StringView text, int64_t max = kMaxMillisecondsLimit) {
            const auto value = ParseNumber(name, text, 0, max);
            if (!value) {
                return std::unexpected(value.error());
            }
            return std::chrono::milliseconds(*value);
        }

        /// An entry the allow-list can match: `*`, `null` or `scheme://host[:port]`. One that matches nothing (a path, a bare host) would
        /// only lock every page out, so it is told at start. The check is the one the handshake makes: the entry has to admit itself
        bool IsUsableOrigin(StringView origin) {
            if (origin.size() > kMaxOriginBytes) {
                return false;
            }
            HandshakePolicy policy;
            policy.loopbackOnly = false;
            policy.allowedOrigins.emplace_back(origin);
            return IsOriginAllowed(origin, policy);
        }

        /// Sets one value of @p args; @p name (a flag or a variable) is what the error message names, so both ways in share the ranges
        std::expected<void, String> ApplyOption(RoomServerArgs& args, Option option, StringView name, StringView value) {
            switch (option) {
                case Option::Bind:
                    if (value == "loopback") {
                        args.bind = SocketBind::Loopback;
                    } else if (value == "any") {
                        args.bind = SocketBind::AnyAddress;
                    } else {
                        return std::unexpected(Format("{} is 'loopback' or 'any', got '{}'", name, value));
                    }
                    return {};
                case Option::Origin:
                    if (!IsUsableOrigin(value)) {
                        return std::unexpected(Format("{} needs an origin such as https://example.com:8443 (scheme, host and optional port, no path; or *), got '{}'", name, value));
                    }
                    args.origins.emplace_back(value);
                    return {};
                case Option::ComputerDelay:
                case Option::LobbyTimeout:
                case Option::ReconnectGrace:
                case Option::PingInterval:
                case Option::StatsInterval:
                case Option::ShutdownGrace: {
                    const auto milliseconds = ParseMilliseconds(name, value, option == Option::ShutdownGrace ? kMaxShutdownGraceMs : kMaxMillisecondsLimit);
                    if (!milliseconds) {
                        return std::unexpected(milliseconds.error());
                    }
                    if (option == Option::ComputerDelay) {
                        args.computerDelay = *milliseconds;
                    } else if (option == Option::LobbyTimeout) {
                        args.lobbyTimeout = *milliseconds;
                    } else if (option == Option::ReconnectGrace) {
                        args.reconnectGrace = *milliseconds;
                    } else if (option == Option::PingInterval) {
                        args.pingInterval = *milliseconds;
                    } else if (option == Option::StatsInterval) {
                        args.statsInterval = *milliseconds;
                    } else {
                        args.shutdownGrace = *milliseconds;
                    }
                    return {};
                }
                case Option::MaxRoomsPerAddress:
                case Option::TrustedProxyHops: {
                    const bool isRooms = option == Option::MaxRoomsPerAddress;
                    const auto number = ParseNumber(name, value, 0, isRooms ? kMaxRoomsPerAddressLimit : kMaxTrustedProxyHopsLimit);
                    if (!number) {
                        return std::unexpected(number.error());
                    }
                    (isRooms ? args.maxRoomsPerAddress : args.trustedProxyHops) = static_cast<size_t>(*number);
                    return {};
                }
                case Option::Port:
                case Option::MaxConnections:
                case Option::MaxPerAddress: {
                    const bool isPort = option == Option::Port;
                    const bool hasZero = isPort || option == Option::MaxPerAddress;
                    const int64_t max = isPort ? std::numeric_limits<uint16_t>::max() : static_cast<int64_t>(kMaxConnectionsLimit);
                    const auto number = ParseNumber(name, value, hasZero ? 0 : 1, max);
                    if (!number) {
                        return std::unexpected(number.error());
                    }
                    if (isPort) {
                        args.port = static_cast<uint16_t>(*number);
                    } else if (option == Option::MaxConnections) {
                        args.maxConnections = static_cast<size_t>(*number);
                    } else {
                        args.maxConnectionsPerAddress = static_cast<size_t>(*number);
                    }
                    return {};
                }
            }
            return {};
        }

    }  // namespace

    std::expected<RoomServerArgs, String> ParseRoomServerArgs(int argc, const char* const* argv, RoomServerArgs base) {
        RoomServerArgs args = std::move(base);
        bool hasFlagOrigins = false;
        for (int i = 1; i < argc; ++i) {
            const StringView flag = argv[i];
            if (flag == "--help" || flag == "-h") {
                args.help = true;
                continue;
            }
            if (flag == "--test-games") {
                args.testGames = true;
                continue;
            }

            const OptionInfo* option = FindOptionByFlag(flag);
            if (option == nullptr) {
                return std::unexpected(Format("unknown argument '{}' (see --help)", flag));
            }
            if (i + 1 >= argc) {
                return std::unexpected(Format("{} needs a value", flag));
            }
            const StringView value = argv[++i];

            if (option->option == Option::Origin && !hasFlagOrigins) {
                // The origins of the command line replace those of the environment
                hasFlagOrigins = true;
                args.origins.clear();
            }
            if (auto applied = ApplyOption(args, option->option, flag, value); !applied) {
                return std::unexpected(applied.error());
            }
        }
        return args;
    }

    std::expected<RoomServerArgs, String> ReadRoomServerEnvironment(const RoomServerEnvironmentLookup& lookup) {
        RoomServerArgs args;
        const auto read = [&lookup](const char* name) -> StringView {
            const char* text = lookup(name);
            return text != nullptr ? StringView(text) : StringView();
        };

        for (const OptionInfo& info : kOptions) {
            const StringView value = read(info.variable);
            if (value.empty()) {
                continue;
            }
            if (info.option == Option::Origin) {
                // A comma-separated list: items trimmed, empty items skipped
                StringView rest = value;
                while (!rest.empty()) {
                    const size_t comma = rest.find(',');
                    const StringView item = Trim(rest.substr(0, comma));
                    rest = comma == StringView::npos ? StringView() : rest.substr(comma + 1);
                    if (item.empty()) {
                        continue;
                    }
                    if (auto applied = ApplyOption(args, info.option, info.variable, item); !applied) {
                        return std::unexpected(applied.error());
                    }
                }
            } else if (auto applied = ApplyOption(args, info.option, info.variable, value); !applied) {
                return std::unexpected(applied.error());
            }
        }

        const StringView testGames = read(kTestGamesVariable);
        if (!testGames.empty()) {
            if (testGames == "1" || testGames == "true") {
                args.testGames = true;
            } else if (testGames != "0" && testGames != "false") {
                return std::unexpected(Format("{} is 1, 0, true or false, got '{}'", kTestGamesVariable, testGames));
            }
        }
        return args;
    }

    std::expected<RoomServerArgs, String> LoadRoomServerArgs(int argc, const char* const* argv) {
        auto environment = ReadRoomServerEnvironment([](const char* name) { return std::getenv(name); });
        if (!environment) {
            return std::unexpected(environment.error());
        }
        return ParseRoomServerArgs(argc, argv, std::move(*environment));
    }

    String GetRoomServerUsage() {
        const RoomServerArgs defaults;
        return Format("Usage: BagiRoomServer [options]\n"
                      "  --port N                 TCP port, 0 picks a free one (default {})\n"
                      "  --bind loopback|any      the machine's own address only, or every address (default any)\n"
                      "  --origin ORIGIN          a web page that may connect, e.g. https://example.com; repeat for more; '*' lets every page in\n"
                      "                           (development only). Without it browsers are refused and programs are let in\n"
                      "  --max-connections N      connections the hub admits (default {})\n"
                      "  --max-per-address N      open sockets of one client address, 0 is off (default {})\n"
                      "  --max-rooms-per-address N rooms one client address may have at once, 0 is off (default {})\n"
                      "  --lobby-timeout-ms N     close a lobby with no message but ping for this long, 0 is off (default {})\n"
                      "  --trusted-proxy-hops N   proxies in front that append to X-Forwarded-For: the client is the entry N places from the right,\n"
                      "                           0 reads the socket's address (default {}). Only behind a proxy that appends the header\n"
                      "  --computer-delay-ms N    pause between two moves of computers (default {})\n"
                      "  --reconnect-grace-ms N   how long a dropped player keeps the seat (default {})\n"
                      "  --ping-interval-ms N     a ping after this much silence, a drop after twice that, 0 is off (default {})\n"
                      "  --stats-interval-ms N    log the connections and rooms at most this often, when they changed, 0 is off (default {})\n"
                      "  --shutdown-grace-ms N    on SIGTERM, how long to wait for the clients to answer the close 1001 before stopping, 0 is off (default {})\n"
                      "  --test-games             register the test games (cards-leaky) next to cards\n"
                      "  --help                   this text\n"
                      "\n"
                      "Environment (a flag wins over its variable, a variable over the default; an empty variable is unset):\n"
                      "  PORT=N  BAGI_ROOMS_BIND  BAGI_ROOMS_ORIGINS (comma-separated)  BAGI_ROOMS_MAX_CONNECTIONS  BAGI_ROOMS_MAX_PER_ADDRESS\n"
                      "  BAGI_ROOMS_MAX_ROOMS_PER_ADDRESS  BAGI_ROOMS_LOBBY_TIMEOUT_MS  BAGI_ROOMS_TRUSTED_PROXY_HOPS\n"
                      "  BAGI_ROOMS_COMPUTER_DELAY_MS  BAGI_ROOMS_RECONNECT_GRACE_MS  BAGI_ROOMS_PING_INTERVAL_MS  BAGI_ROOMS_STATS_INTERVAL_MS\n"
                      "  BAGI_ROOMS_SHUTDOWN_GRACE_MS\n"
                      "  {}=1  the --test-games flag\n"
                      "GET {} answers 200 ok (a hosting platform's health check).\n",
                      defaults.port, defaults.maxConnections, defaults.maxConnectionsPerAddress, defaults.maxRoomsPerAddress, defaults.lobbyTimeout.count(), defaults.trustedProxyHops,
                      defaults.computerDelay.count(), defaults.reconnectGrace.count(), defaults.pingInterval.count(), defaults.statsInterval.count(), defaults.shutdownGrace.count(),
                      kTestGamesVariable, kRoomServerHealthPath);
    }

    RoomServerSettings ToSettings(const RoomServerArgs& args) {
        RoomServerSettings settings;
        settings.policy.healthPath = kRoomServerHealthPath;
        settings.port = args.port;
        settings.bind = args.bind;
        settings.policy.loopbackOnly = false;
        settings.policy.allowedOrigins = args.origins;
        settings.hub.maxConnections = args.maxConnections;
        settings.maxConnections = args.maxConnections + kHandshakeConnectionReserve;
        settings.maxConnectionsPerAddress = args.maxConnectionsPerAddress;
        settings.policy.trustedProxyHops = args.trustedProxyHops;
        settings.hub.maxRoomsPerAddress = args.maxRoomsPerAddress;
        settings.hub.lobbyTimeout = args.lobbyTimeout;
        settings.hub.timings.computerDelay = args.computerDelay;
        settings.hub.timings.reconnectGrace = args.reconnectGrace;
        settings.pingInterval = args.pingInterval;
        settings.statsLogInterval = args.statsInterval;
        settings.shutdownGrace = args.shutdownGrace;
        return settings;
    }

}  // namespace BECore
