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
        EXPECT_FALSE(args->testGames);
        EXPECT_FALSE(args->help);
    }

    TEST(RoomServerArgsTest, EveryFlagIsRead) {
        const auto args = Parse({"--port", "9001", "--bind", "loopback", "--origin", "https://a.example", "--max-connections", "100", "--max-per-address", "5", "--computer-delay-ms", "0",
                                 "--reconnect-grace-ms", "300", "--ping-interval-ms", "1500", "--test-games"});
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
        EXPECT_TRUE(args->testGames);
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
        for (const char* flag :
             {"--port", "--bind", "--origin", "--max-connections", "--max-per-address", "--computer-delay-ms", "--reconnect-grace-ms", "--ping-interval-ms", "--test-games", "--help"}) {
            EXPECT_NE(usage.find(flag), std::string::npos) << flag;
        }
    }

    TEST(RoomServerArgsTest, UnknownFlagAndStrayWordAreErrors) {
        EXPECT_NE(ErrorOf({"--nope"}).find("--nope"), std::string::npos);
        EXPECT_NE(ErrorOf({"extra"}).find("extra"), std::string::npos);
        EXPECT_NE(ErrorOf({"--port=80"}).find("--port=80"), std::string::npos);
    }

    TEST(RoomServerArgsTest, MissingValueIsAnError) {
        for (const char* flag : {"--port", "--bind", "--origin", "--max-connections", "--max-per-address", "--computer-delay-ms", "--reconnect-grace-ms", "--ping-interval-ms"}) {
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
