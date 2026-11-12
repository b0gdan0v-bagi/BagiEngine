#pragma once

#include <RoomServer/RoomServerSettings.h>

namespace BECore {

    /**
     * @brief What a room server executable is started with: a plain struct the caller fills (LoadRoomServerArgs(): the environment of a
     * hosting platform, then `argv` over it), turned into RoomServerSettings by ToSettings().
     */
    struct RoomServerArgs {
        uint16_t port = 8080;

        /// A public server binds every address; `loopback` is for a machine's own tests
        SocketBind bind = SocketBind::AnyAddress;

        /// The web pages that may connect (`scheme://host[:port]`); `*` is every page (for development only). Empty: no browser page, programs only
        eastl::vector<eastl::string> origins;

        /// The connections the hub admits (the next one is closed with `server_full`); the transport allows 64 more for connections still in their handshake
        size_t maxConnections = RoomLimits::kMaxConnections;

        /// Open sockets of one client address; 0 is off (behind a proxy every client has the proxy's address)
        size_t maxConnectionsPerAddress = 32;

        std::chrono::milliseconds computerDelay = RoomLimits::kComputerDelay;
        std::chrono::milliseconds reconnectGrace = RoomLimits::kReconnectGrace;

        /// 0 turns the liveness pings off (RoomServerSettings::pingInterval)
        std::chrono::milliseconds pingInterval{20'000};

        /// Registers the test games (`cards-leaky`, the negative control) next to `cards`
        bool testGames = false;

        /// `--help` was given: print GetRoomServerUsage() and exit
        bool help = false;
    };

    /// Reads `argv[1..argc)` over @p base (the defaults, or what ReadRoomServerEnvironment() gave: a flag wins over it; `--origin` replaces the list of @p base).
    /// The error is a message for the user (an unknown flag, a missing value, a number that is not one or out of range)
    [[nodiscard]] std::expected<RoomServerArgs, eastl::string> ParseRoomServerArgs(int argc, const char* const* argv, RoomServerArgs base = {});

    /// Looks a variable up: its text, or null when it is not set (`std::getenv`, or a table in a test)
    using RoomServerEnvironmentLookup = std::function<const char*(const char*)>;

    /// What the environment of a hosting platform says: `PORT` and `BAGI_ROOMS_*` (the list is in GetRoomServerUsage()), the same ranges as the flags. An unset or empty
    /// variable is unset. The error names the variable
    [[nodiscard]] std::expected<RoomServerArgs, eastl::string> ReadRoomServerEnvironment(const RoomServerEnvironmentLookup& lookup);

    /// What a server main starts from: the defaults, the environment over them (`std::getenv`), the command line over both
    [[nodiscard]] std::expected<RoomServerArgs, eastl::string> LoadRoomServerArgs(int argc, const char* const* argv);

    /// The text `--help` prints: every flag with its default
    [[nodiscard]] eastl::string GetRoomServerUsage();

    /// The settings of a server for @p args. The policy is always the public one (`Host` is not checked, `Origin` is matched against `args.origins`), also on the loopback
    [[nodiscard]] RoomServerSettings ToSettings(const RoomServerArgs& args);

}  // namespace BECore
