#include <RoomServer/RoomServerArgs.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        std::expected<RoomServerArgs, eastl::string> Parse(std::initializer_list<const char*> arguments) {
            std::vector<const char*> argv = {"BagiRoomServer"};
            argv.insert(argv.end(), arguments.begin(), arguments.end());
            return ParseRoomServerArgs(static_cast<int>(argv.size()), argv.data());
        }

        /// The message of a failed parse (empty when it succeeded)
        std::string ErrorOf(std::initializer_list<const char*> arguments) {
            const auto parsed = Parse(arguments);
            return parsed ? std::string() : std::string(parsed.error().c_str());
        }

        using Environment = std::map<std::string, std::string>;

        std::expected<RoomServerArgs, eastl::string> ReadEnvironment(const Environment& environment) {
            return ReadRoomServerEnvironment([&environment](const char* name) -> const char* {
                const auto found = environment.find(name);
                return found != environment.end() ? found->second.c_str() : nullptr;
            });
        }

        /// The message of an environment that was refused (empty when it was taken)
        std::string EnvironmentErrorOf(const Environment& environment) {
            const auto read = ReadEnvironment(environment);
            return read ? std::string() : std::string(read.error().c_str());
        }

        /// The environment first, the command line over it (what LoadRoomServerArgs() does with the real one)
        std::expected<RoomServerArgs, eastl::string> ParseOver(const Environment& environment, std::initializer_list<const char*> arguments) {
            auto base = ReadEnvironment(environment);
            if (!base) {
                return base;
            }
            std::vector<const char*> argv = {"BagiRoomServer"};
            argv.insert(argv.end(), arguments.begin(), arguments.end());
            return ParseRoomServerArgs(static_cast<int>(argv.size()), argv.data(), std::move(*base));
        }

    }  // namespace

    TEST(RoomServerArgsTest, NoArgumentsGiveTheDefaults) {
        const auto args = Parse({});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->port, 8080);
        EXPECT_EQ(args->bind, SocketBind::AnyAddress);
        EXPECT_TRUE(args->origins.empty());
        EXPECT_EQ(args->maxConnections, RoomLimits::kMaxConnections);
        EXPECT_EQ(args->maxConnectionsPerAddress, 32u);
        EXPECT_EQ(args->computerDelay, RoomLimits::kComputerDelay);
        EXPECT_EQ(args->reconnectGrace, RoomLimits::kReconnectGrace);
        EXPECT_EQ(args->pingInterval, 20s);
        EXPECT_EQ(args->statsInterval, 30s);
        EXPECT_EQ(args->shutdownGrace, 1s);
        EXPECT_FALSE(args->testGames);
        EXPECT_FALSE(args->help);
    }

    TEST(RoomServerArgsTest, EveryFlagIsRead) {
        const auto args = Parse({"--port",
                                 "9001",
                                 "--bind",
                                 "loopback",
                                 "--origin",
                                 "https://a.example",
                                 "--max-connections",
                                 "100",
                                 "--max-per-address",
                                 "5",
                                 "--computer-delay-ms",
                                 "0",
                                 "--reconnect-grace-ms",
                                 "300",
                                 "--ping-interval-ms",
                                 "1500",
                                 "--stats-interval-ms",
                                 "2000",
                                 "--shutdown-grace-ms",
                                 "2500",
                                 "--test-games"});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->port, 9001);
        EXPECT_EQ(args->bind, SocketBind::Loopback);
        ASSERT_EQ(args->origins.size(), 1u);
        EXPECT_EQ(args->origins[0], "https://a.example");
        EXPECT_EQ(args->maxConnections, 100u);
        EXPECT_EQ(args->maxConnectionsPerAddress, 5u);
        EXPECT_EQ(args->computerDelay, 0ms);
        EXPECT_EQ(args->reconnectGrace, 300ms);
        EXPECT_EQ(args->pingInterval, 1500ms);
        EXPECT_EQ(args->statsInterval, 2000ms);
        EXPECT_EQ(args->shutdownGrace, 2500ms);
        EXPECT_TRUE(args->testGames);
    }

    TEST(RoomServerArgsTest, StatsIntervalZeroTurnsTheLineOffAndNegativeIsAnError) {
        const auto args = Parse({"--stats-interval-ms", "0"});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->statsInterval, 0ms);
        EXPECT_EQ(ToSettings(*args).statsLogInterval, 0ms);
        EXPECT_NE(ErrorOf({"--stats-interval-ms", "-5"}).find("--stats-interval-ms"), std::string::npos);
        EXPECT_NE(ErrorOf({"--stats-interval-ms", "often"}).find("often"), std::string::npos);
    }

    TEST(RoomServerArgsTest, ShutdownGraceIsZeroToAMinuteAndGoesToTheSettings) {
        EXPECT_EQ(ToSettings(RoomServerArgs{}).shutdownGrace, 1s);
        EXPECT_EQ(RoomServerSettings{}.shutdownGrace, 0ms) << "a test server stops at once unless it asks";

        const auto off = Parse({"--shutdown-grace-ms", "0"});
        ASSERT_TRUE(off.has_value());
        EXPECT_EQ(ToSettings(*off).shutdownGrace, 0ms);
        const auto longest = Parse({"--shutdown-grace-ms", "60000"});
        ASSERT_TRUE(longest.has_value());
        EXPECT_EQ(ToSettings(*longest).shutdownGrace, 60s);

        for (const char* bad : {"60001", "-1", "soon"}) {
            EXPECT_NE(ErrorOf({"--shutdown-grace-ms", bad}).find("--shutdown-grace-ms"), std::string::npos) << bad;
        }
    }

    TEST(RoomServerArgsTest, ToSettingsCarriesTheStatsInterval) {
        EXPECT_EQ(ToSettings(RoomServerArgs{}).statsLogInterval, 30s);
        const auto args = Parse({"--stats-interval-ms", "200"});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(ToSettings(*args).statsLogInterval, 200ms);
        EXPECT_EQ(RoomServerSettings{}.statsLogInterval, 0ms) << "a test server logs no stats unless it asks";
    }

    TEST(RoomServerArgsTest, BindAnyAndPortZeroAndPerAddressZeroAreValid) {
        const auto args = Parse({"--bind", "any", "--port", "0", "--max-per-address", "0", "--ping-interval-ms", "0"});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->bind, SocketBind::AnyAddress);
        EXPECT_EQ(args->port, 0);
        EXPECT_EQ(args->maxConnectionsPerAddress, 0u);
        EXPECT_EQ(args->pingInterval, 0ms);
    }

    TEST(RoomServerArgsTest, OriginIsRepeatableAndTakesTheWildcard) {
        const auto args = Parse({"--origin", "https://a.example", "--origin", "*", "--origin", "http://localhost:5173"});
        ASSERT_TRUE(args.has_value());
        ASSERT_EQ(args->origins.size(), 3u);
        EXPECT_EQ(args->origins[0], "https://a.example");
        EXPECT_EQ(args->origins[1], "*");
        EXPECT_EQ(args->origins[2], "http://localhost:5173");
    }

    TEST(RoomServerArgsTest, OriginThatCouldMatchNothingIsAnError) {
        for (const char* origin : {"", "example.com", "https://a.example/path", "https://", "https://a.example:0", "https://a.example:99999", "https://user@a.example", "https://a b.example"}) {
            const std::string error = ErrorOf({"--origin", origin});
            EXPECT_NE(error.find("--origin"), std::string::npos) << origin;
            EXPECT_NE(error.find(origin), std::string::npos) << origin;
        }
        EXPECT_TRUE(ErrorOf({"--origin", "https://a.example:8443"}).empty());
        EXPECT_TRUE(ErrorOf({"--origin", "http://localhost:5173"}).empty());
        EXPECT_TRUE(ErrorOf({"--origin", "null"}).empty());
        EXPECT_TRUE(ErrorOf({"--origin", "*"}).empty());
    }

    TEST(RoomServerArgsTest, HelpIsAFlagAndTheUsageNamesEveryFlag) {
        const auto args = Parse({"--help"});
        ASSERT_TRUE(args.has_value());
        EXPECT_TRUE(args->help);

        const std::string usage = GetRoomServerUsage().c_str();
        for (const char* flag : {"--port", "--bind", "--origin", "--max-connections", "--max-per-address", "--computer-delay-ms", "--reconnect-grace-ms", "--ping-interval-ms", "--stats-interval-ms",
                                 "--shutdown-grace-ms", "--test-games", "--help"}) {
            EXPECT_NE(usage.find(flag), std::string::npos) << flag;
        }
    }

    TEST(RoomServerArgsTest, UnknownFlagAndStrayWordAreErrors) {
        EXPECT_NE(ErrorOf({"--nope"}).find("--nope"), std::string::npos);
        EXPECT_NE(ErrorOf({"extra"}).find("extra"), std::string::npos);
        EXPECT_NE(ErrorOf({"--port=80"}).find("--port=80"), std::string::npos);
    }

    TEST(RoomServerArgsTest, MissingValueIsAnError) {
        for (const char* flag : {"--port", "--bind", "--origin", "--max-connections", "--max-per-address", "--computer-delay-ms", "--reconnect-grace-ms", "--ping-interval-ms", "--stats-interval-ms",
                                 "--shutdown-grace-ms"}) {
            const std::string error = ErrorOf({flag});
            EXPECT_NE(error.find("needs a value"), std::string::npos) << flag << ": " << error;
        }
    }

    TEST(RoomServerArgsTest, NumbersAreChecked) {
        EXPECT_FALSE(ErrorOf({"--port", "abc"}).empty());
        EXPECT_FALSE(ErrorOf({"--port", ""}).empty());
        EXPECT_FALSE(ErrorOf({"--port", "12x"}).empty());
        EXPECT_FALSE(ErrorOf({"--port", "1.5"}).empty());
        EXPECT_FALSE(ErrorOf({"--port", "-1"}).empty());
        EXPECT_FALSE(ErrorOf({"--port", "65536"}).empty());
        EXPECT_FALSE(ErrorOf({"--port", "99999999999999999999"}).empty());
        EXPECT_TRUE(ErrorOf({"--port", "65535"}).empty());
        EXPECT_FALSE(ErrorOf({"--max-connections", "0"}).empty());
        EXPECT_FALSE(ErrorOf({"--max-connections", "100000"}).empty());
        EXPECT_FALSE(ErrorOf({"--max-per-address", "-3"}).empty());
        EXPECT_FALSE(ErrorOf({"--computer-delay-ms", "-1"}).empty());
        EXPECT_FALSE(ErrorOf({"--reconnect-grace-ms", "999999999999"}).empty());
        EXPECT_FALSE(ErrorOf({"--ping-interval-ms", "soon"}).empty());
        EXPECT_NE(ErrorOf({"--port", "70000"}).find("70000"), std::string::npos) << "the message names the value";
    }

    TEST(RoomServerArgsTest, BadBindAndBadOriginAreErrors) {
        EXPECT_NE(ErrorOf({"--bind", "everywhere"}).find("everywhere"), std::string::npos);
        EXPECT_FALSE(ErrorOf({"--origin", ""}).empty());
        EXPECT_FALSE(ErrorOf({"--origin", "https://a b"}).empty());
        EXPECT_FALSE(ErrorOf({"--origin", std::string(500, 'a').c_str()}).empty());
    }

    TEST(RoomServerArgsTest, ToSettingsMapsTheValuesAndAlwaysUsesThePublicPolicy) {
        const auto args = Parse({"--port", "9001", "--bind", "loopback", "--origin", "https://a.example", "--origin", "*", "--max-connections", "100", "--max-per-address", "5", "--computer-delay-ms",
                                 "10", "--reconnect-grace-ms", "300", "--ping-interval-ms", "1500"});
        ASSERT_TRUE(args.has_value());
        const RoomServerSettings settings = ToSettings(*args);
        EXPECT_EQ(settings.port, 9001);
        EXPECT_EQ(settings.bind, SocketBind::Loopback);
        EXPECT_FALSE(settings.policy.loopbackOnly) << "the server checks the Origin list, not the loopback";
        ASSERT_EQ(settings.policy.allowedOrigins.size(), 2u);
        EXPECT_EQ(settings.policy.allowedOrigins[1], "*");
        EXPECT_TRUE(settings.policy.allowMissingOrigin);
        EXPECT_EQ(settings.hub.maxConnections, 100u);
        EXPECT_EQ(settings.maxConnections, 100u + kHandshakeConnectionReserve) << "the hub answers server_full before the transport refuses";
        EXPECT_EQ(settings.maxConnectionsPerAddress, 5u);
        EXPECT_EQ(settings.hub.timings.computerDelay, 10ms);
        EXPECT_EQ(settings.hub.timings.reconnectGrace, 300ms);
        EXPECT_EQ(settings.pingInterval, 1500ms);
    }

    TEST(RoomServerArgsTest, ToSettingsCarriesTheHealthPath) {
        const RoomServerSettings settings = ToSettings(RoomServerArgs{});
        EXPECT_EQ(settings.policy.healthPath, "/healthz");
        EXPECT_EQ(settings.policy.healthPath, kRoomServerHealthPath);
    }

    TEST(RoomServerArgsTest, EmptyEnvironmentGivesTheDefaults) {
        const auto args = ReadEnvironment({});
        ASSERT_TRUE(args.has_value());
        const RoomServerArgs defaults;
        EXPECT_EQ(args->port, defaults.port);
        EXPECT_EQ(args->bind, defaults.bind);
        EXPECT_TRUE(args->origins.empty());
        EXPECT_EQ(args->maxConnections, defaults.maxConnections);
        EXPECT_EQ(args->maxConnectionsPerAddress, defaults.maxConnectionsPerAddress);
        EXPECT_EQ(args->pingInterval, defaults.pingInterval);
        EXPECT_FALSE(args->testGames);
    }

    TEST(RoomServerArgsTest, EveryVariableIsRead) {
        const auto args = ReadEnvironment({{"PORT", "9001"},
                                           {"BAGI_ROOMS_BIND", "loopback"},
                                           {"BAGI_ROOMS_ORIGINS", "https://a.example"},
                                           {"BAGI_ROOMS_MAX_CONNECTIONS", "100"},
                                           {"BAGI_ROOMS_MAX_PER_ADDRESS", "0"},
                                           {"BAGI_ROOMS_COMPUTER_DELAY_MS", "10"},
                                           {"BAGI_ROOMS_RECONNECT_GRACE_MS", "300"},
                                           {"BAGI_ROOMS_PING_INTERVAL_MS", "1500"},
                                           {"BAGI_ROOMS_STATS_INTERVAL_MS", "2000"},
                                           {"BAGI_ROOMS_SHUTDOWN_GRACE_MS", "2500"},
                                           {"BAGI_ROOMS_TEST_GAMES", "1"}});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->port, 9001);
        EXPECT_EQ(args->bind, SocketBind::Loopback);
        ASSERT_EQ(args->origins.size(), 1u);
        EXPECT_EQ(args->origins[0], "https://a.example");
        EXPECT_EQ(args->maxConnections, 100u);
        EXPECT_EQ(args->maxConnectionsPerAddress, 0u);
        EXPECT_EQ(args->computerDelay, 10ms);
        EXPECT_EQ(args->reconnectGrace, 300ms);
        EXPECT_EQ(args->pingInterval, 1500ms);
        EXPECT_EQ(args->statsInterval, 2000ms);
        EXPECT_EQ(args->shutdownGrace, 2500ms);
        EXPECT_TRUE(args->testGames);
    }

    TEST(RoomServerArgsTest, EmptyVariableIsUnset) {
        const auto args = ReadEnvironment({{"PORT", ""}, {"BAGI_ROOMS_BIND", ""}, {"BAGI_ROOMS_ORIGINS", ""}, {"BAGI_ROOMS_MAX_CONNECTIONS", ""}, {"BAGI_ROOMS_TEST_GAMES", ""}});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->port, RoomServerArgs{}.port);
        EXPECT_EQ(args->bind, RoomServerArgs{}.bind);
        EXPECT_TRUE(args->origins.empty());
        EXPECT_FALSE(args->testGames);
    }

    TEST(RoomServerArgsTest, PortZeroPicksAFreePort) {
        const auto args = ReadEnvironment({{"PORT", "0"}});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->port, 0);
    }

    TEST(RoomServerArgsTest, OriginsVariableIsAListWithBlanksAndEmptyItems) {
        const auto args = ReadEnvironment({{"BAGI_ROOMS_ORIGINS", " https://a.example , ,https://b.example:8443,, *"}});
        ASSERT_TRUE(args.has_value());
        ASSERT_EQ(args->origins.size(), 3u);
        EXPECT_EQ(args->origins[0], "https://a.example");
        EXPECT_EQ(args->origins[1], "https://b.example:8443");
        EXPECT_EQ(args->origins[2], "*");

        const auto onlyBlanks = ReadEnvironment({{"BAGI_ROOMS_ORIGINS", " , ,"}});
        ASSERT_TRUE(onlyBlanks.has_value());
        EXPECT_TRUE(onlyBlanks->origins.empty());
    }

    TEST(RoomServerArgsTest, BadVariableNamesTheVariable) {
        const std::pair<const char*, const char*> bad[] = {
            {"PORT", "70000"},
            {"PORT", "abc"},
            {"BAGI_ROOMS_BIND", "everywhere"},
            {"BAGI_ROOMS_ORIGINS", "example.com"},
            {"BAGI_ROOMS_MAX_CONNECTIONS", "0"},
            {"BAGI_ROOMS_MAX_PER_ADDRESS", "-1"},
            {"BAGI_ROOMS_COMPUTER_DELAY_MS", "soon"},
            {"BAGI_ROOMS_RECONNECT_GRACE_MS", "999999999999"},
            {"BAGI_ROOMS_PING_INTERVAL_MS", "1.5"},
            {"BAGI_ROOMS_STATS_INTERVAL_MS", "-1"},
            {"BAGI_ROOMS_SHUTDOWN_GRACE_MS", "60001"},
            {"BAGI_ROOMS_TEST_GAMES", "yes"},
        };
        for (const auto& [name, value] : bad) {
            const std::string error = EnvironmentErrorOf({{name, value}});
            EXPECT_NE(error.find(name), std::string::npos) << name << "=" << value << ": " << error;
            EXPECT_NE(error.find(value), std::string::npos) << name << "=" << value << ": " << error;
        }
    }

    TEST(RoomServerArgsTest, OneBadItemOfTheOriginsListIsAnError) {
        const std::string error = EnvironmentErrorOf({{"BAGI_ROOMS_ORIGINS", "https://a.example,example.com"}});
        EXPECT_NE(error.find("BAGI_ROOMS_ORIGINS"), std::string::npos) << error;
        EXPECT_NE(error.find("example.com"), std::string::npos) << error;
    }

    TEST(RoomServerArgsTest, TestGamesVariableTakesOneZeroTrueFalse) {
        for (const char* on : {"1", "true"}) {
            const auto args = ReadEnvironment({{"BAGI_ROOMS_TEST_GAMES", on}});
            ASSERT_TRUE(args.has_value());
            EXPECT_TRUE(args->testGames) << on;
        }
        for (const char* off : {"0", "false"}) {
            const auto args = ReadEnvironment({{"BAGI_ROOMS_TEST_GAMES", off}});
            ASSERT_TRUE(args.has_value());
            EXPECT_FALSE(args->testGames) << off;
        }
    }

    TEST(RoomServerArgsTest, FlagBeatsVariableBeatsDefault) {
        const Environment environment = {{"PORT", "9001"}, {"BAGI_ROOMS_BIND", "loopback"}, {"BAGI_ROOMS_PING_INTERVAL_MS", "1500"}};
        const auto args = ParseOver(environment, {"--port", "9002", "--max-per-address", "7"});
        ASSERT_TRUE(args.has_value());
        EXPECT_EQ(args->port, 9002) << "the flag wins over the variable";
        EXPECT_EQ(args->bind, SocketBind::Loopback) << "the variable wins over the default";
        EXPECT_EQ(args->pingInterval, 1500ms);
        EXPECT_EQ(args->maxConnectionsPerAddress, 7u) << "no variable: the flag over the default";
        EXPECT_EQ(args->maxConnections, RoomLimits::kMaxConnections) << "neither: the default";
    }

    TEST(RoomServerArgsTest, FlagOriginsReplaceTheOriginsOfTheEnvironment) {
        const Environment environment = {{"BAGI_ROOMS_ORIGINS", "https://env.example"}};
        const auto kept = ParseOver(environment, {});
        ASSERT_TRUE(kept.has_value());
        ASSERT_EQ(kept->origins.size(), 1u);
        EXPECT_EQ(kept->origins[0], "https://env.example");

        const auto replaced = ParseOver(environment, {"--origin", "https://a.example", "--origin", "https://b.example"});
        ASSERT_TRUE(replaced.has_value());
        ASSERT_EQ(replaced->origins.size(), 2u);
        EXPECT_EQ(replaced->origins[0], "https://a.example");
        EXPECT_EQ(replaced->origins[1], "https://b.example");
    }

    TEST(RoomServerArgsTest, ABadFlagStillFailsOverAGoodEnvironment) {
        const auto args = ParseOver({{"PORT", "9001"}}, {"--port", "abc"});
        ASSERT_FALSE(args.has_value());
        EXPECT_NE(std::string(args.error().c_str()).find("--port"), std::string::npos);
    }

    TEST(RoomServerArgsTest, UsageNamesTheVariablesAndTheHealthPath) {
        const std::string usage = GetRoomServerUsage().c_str();
        for (const char* name : {"PORT", "BAGI_ROOMS_BIND", "BAGI_ROOMS_ORIGINS", "BAGI_ROOMS_MAX_CONNECTIONS", "BAGI_ROOMS_MAX_PER_ADDRESS", "BAGI_ROOMS_COMPUTER_DELAY_MS",
                                 "BAGI_ROOMS_RECONNECT_GRACE_MS", "BAGI_ROOMS_PING_INTERVAL_MS", "BAGI_ROOMS_STATS_INTERVAL_MS", "BAGI_ROOMS_SHUTDOWN_GRACE_MS", "BAGI_ROOMS_TEST_GAMES", "/healthz"}) {
            EXPECT_NE(usage.find(name), std::string::npos) << name;
        }
    }

    TEST(RoomServerArgsTest, DefaultArgsGiveTheDefaultSettings) {
        const RoomServerSettings settings = ToSettings(RoomServerArgs{});
        const RoomServerSettings defaults;
        EXPECT_EQ(settings.maxConnections, defaults.maxConnections);
        EXPECT_EQ(settings.hub.maxConnections, defaults.hub.maxConnections);
        EXPECT_EQ(settings.maxConnectionsPerAddress, defaults.maxConnectionsPerAddress);
        EXPECT_EQ(settings.pingInterval, defaults.pingInterval);
        EXPECT_EQ(settings.hub.timings.computerDelay, defaults.hub.timings.computerDelay);
        EXPECT_TRUE(settings.policy.allowedOrigins.empty());
    }

}  // namespace BECore::Tests
