#pragma once

#include <Rooms/RoomHub.h>

namespace BECore::Fuzz {

    namespace RoomProtocolInput {

        /// FuzzRoomProtocol: the first byte is [limit: 2 bits | side: 1 bit]; the rest is the text of one message
        inline constexpr uint8_t kServerBit = 0x01;
        inline constexpr uint8_t kLimitShift = 1;
        /// The size limits an input can pick (the index is the bits above the side bit, modulo the count); 0 is the real limit of the side
        inline constexpr size_t kMessageLimits[] = {0, 64, 1024, 12};

        constexpr uint8_t PackHeader(bool server, size_t limitIndex) {
            return static_cast<uint8_t>((server ? kServerBit : 0) | (limitIndex << kLimitShift));
        }

    }  // namespace RoomProtocolInput

    namespace RoomHubInput {

        /// FuzzRoomHub: a stream of operations, each `[op: 2 bits | connection: 6 bits][length: u16, little endian]`, and for a message `length` bytes of text follow
        enum class Op : uint8_t {
            Message = 0,     ///< `length` bytes of text arrive from the connection
            Connect = 1,     ///< the transport opens the connection
            Disconnect = 2,  ///< the transport reports the connection dropped
            Tick = 3,        ///< `length` milliseconds pass and the hub is ticked
        };

        inline constexpr uint8_t kOpMask = 0x03;
        inline constexpr uint8_t kConnectionShift = 2;
        inline constexpr size_t kOperationHeaderBytes = 3;
        /// Operations of one input beyond this are ignored: the cost of an input is bounded
        inline constexpr size_t kMaxOperations = 200;

        /// What the hub of the target is set up with: small limits, so that the full-hub and full-room refusals are within reach of an input
        inline constexpr size_t kMaxRooms = 3;
        inline constexpr size_t kMaxConnections = 6;
        inline constexpr size_t kMaxMessageBytes = 2048;
        /// The words of the random function of the hub (a recorded session names the codes and tokens this makes)
        inline constexpr uint64_t kRandomSeed = 0x9E3779B97F4A7C15ULL;

        /// Words of splitmix64: the same on every platform, no state outside the hub. The target and the recorder of the seeds both make the hub's random function here
        inline RandomFn MakeRandom() {
            auto state = eastl::make_shared<uint64_t>(kRandomSeed);
            return [state]() -> uint64_t {
                uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                return z ^ (z >> 31);
            };
        }

        /// The limits of the hub of the target. The rate limit is out of reach on purpose (the time of an input is whatever its ticks add up to; the limiter has tests of its own)
        inline RoomHubSettings MakeSettings() {
            constexpr double kUnlimitedRate = 1000.0;
            constexpr int kUnlimitedBurst = 1000;
            RoomHubSettings settings;
            settings.maxRooms = kMaxRooms;
            settings.maxConnections = kMaxConnections;
            settings.maxMessageBytes = kMaxMessageBytes;
            settings.rateBurst = kUnlimitedBurst;
            settings.ratePerSecond = kUnlimitedRate;
            settings.timings.computerDelay = std::chrono::milliseconds(0);
            return settings;
        }

        /// The connection id a number of an operation stands for
        constexpr ConnectionId ConnectionOf(uint8_t number) {
            return static_cast<ConnectionId>(number) + 1;
        }

        inline void AppendOperation(eastl::vector<uint8_t>& bytes, Op op, uint8_t connection, uint16_t length, eastl::string_view text = {}) {
            bytes.push_back(static_cast<uint8_t>(static_cast<uint8_t>(op) | (connection << kConnectionShift)));
            bytes.push_back(static_cast<uint8_t>(length & 0xFF));
            bytes.push_back(static_cast<uint8_t>(length >> 8));
            bytes.insert(bytes.end(), text.begin(), text.end());
        }

        inline void AppendMessage(eastl::vector<uint8_t>& bytes, uint8_t connection, eastl::string_view text) {
            AppendOperation(bytes, Op::Message, connection, static_cast<uint16_t>(text.size()), text);
        }

    }  // namespace RoomHubInput

}  // namespace BECore::Fuzz
