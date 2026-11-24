#pragma once

#include <RoomClient/RoomClient.h>
#include <Rooms/RateLimiter.h>

namespace BECore {

    CORE_ENUM(ProbeStep, uint8_t, Running, Done, Failed)

    /// The text of the first action of @p actionsText, the `actions` of a view; empty when it is no array or holds none. The result is a view of @p actionsText
    /// and the tape borrows it, so both outlive the use
    [[nodiscard]] StringView FirstActionText(JsonTape& tape, StringView actionsText);

    /**
     * @brief One client of the load mode: hosts a room, starts it, plays the first action of every turn, and when the hand ends leaves
     * and hosts the next one on the same connection, until it has sent the wanted number of actions.
     *
     * Paces its own sends below the hub's per-connection bucket (RoomLimits::kRateBurst and kRatePerSecond), so a fast loopback does
     * not turn the load into `rate_limited`; every client of one process is stepped from one thread.
     */
    class ProbeClient {
    public:
        ProbeClient(String url, String game, String name, uint64_t wantedActions);

        /// Polls the client and does what the room asks of it; Failed holds GetFailure()
        ProbeStep Step(RoomTime now);

        /// Whether the last Step() received an event or sent a message (the caller sleeps only when no client did)
        [[nodiscard]] bool HadActivity() const {
            return _hadActivity;
        }

        [[nodiscard]] uint64_t GetActions() const {
            return _actions;
        }

        [[nodiscard]] uint64_t GetHands() const {
            return _hands;
        }

        [[nodiscard]] const String& GetFailure() const {
            return _failure;
        }

    private:
        ProbeStep Fail(String message);
        bool HandleEvent(const RoomClientEvent& event);
        ProbeStep SendPending(RoomTime now);

        RoomClient _client;
        RateLimiter _pace;
        Vector<RoomClientEvent> _events;
        JsonTape _tape;
        String _url;
        String _game;
        String _name;
        String _failure;
        uint64_t _wantedActions = 0;
        uint64_t _actions = 0;
        uint64_t _hands = 0;
        uint64_t _actedVersion = 0;
        bool _hadActivity = false;
        bool _needHost = true;
        bool _needStart = false;
        bool _needLeave = false;
        bool _leaving = false;
    };

}  // namespace BECore
