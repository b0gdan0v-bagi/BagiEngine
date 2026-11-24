#include "ProbeLoad.h"

namespace BECore {

    namespace {

        // Below the hub's bucket on purpose: the two clocks and the network jitter would otherwise spend the margin
        constexpr int kPaceBurst = RoomLimits::kRateBurst / 2;
        constexpr double kPaceRatePerSecond = RoomLimits::kRatePerSecond * 0.8;

        RoomClientSettings MakeSettings() {
            // A lost connection ends the probe: it is a check, not a player, so it does not wait for a reconnect
            RoomClientSettings settings;
            settings.reconnect = false;
            return settings;
        }

    }  // namespace

    StringView FirstActionText(JsonTape& tape, StringView actionsText) {
        if (!tape.Parse(actionsText) || !JsonView(tape, 0).IsArray()) {
            return {};
        }
        for (const JsonView action : JsonView(tape, 0).Items()) {
            return action.Raw();
        }
        return {};
    }

    ProbeClient::ProbeClient(String url, String game, String name, uint64_t wantedActions)
        : _client(MakeSettings()), _pace(kPaceBurst, kPaceRatePerSecond), _url(std::move(url)), _game(std::move(game)), _name(std::move(name)), _wantedActions(wantedActions) {}

    ProbeStep ProbeClient::Fail(String message) {
        _failure = std::move(message);
        return ProbeStep::Failed;
    }

    bool ProbeClient::HandleEvent(const RoomClientEvent& event) {
        switch (event.kind) {
            case RoomClientEventKind::Error:
            case RoomClientEventKind::Closed:
                _failure = Format("{} {} {}", EnumUtils<RoomClientEventKind>::ToString(event.kind), event.code, event.message);
                return false;
            case RoomClientEventKind::ConnectionLost:
                _failure = Format("connection lost: {}", event.message);
                return false;
            case RoomClientEventKind::Joined:
                _needStart = true;
                _leaving = false;
                _actedVersion = 0;
                break;
            case RoomClientEventKind::Left:
                // Only the answer to our own Leave() ends a hand; the server closing the room under us is a failure
                if (!_leaving) {
                    _failure = Format("left the room: {}", event.code);
                    return false;
                }
                _leaving = false;
                _needHost = true;
                break;
            default:
                break;
        }
        return true;
    }

    ProbeStep ProbeClient::SendPending(RoomTime now) {
        if (_needHost) {
            if (!_pace.TryTake(now)) {
                return ProbeStep::Running;
            }
            if (const auto hosted = _client.Host(_url, _game, _name); !hosted) {
                return Fail(Format("{}: {}", hosted.error().code, hosted.error().message));
            }
            _needHost = false;
            _hadActivity = true;
            return ProbeStep::Running;
        }
        if (_needStart) {
            if (!_pace.TryTake(now)) {
                return ProbeStep::Running;
            }
            if (const auto result = _client.Start(); !result) {
                return Fail(Format("{}: {}", result.error().code, result.error().message));
            }
            _needStart = false;
            _hadActivity = true;
            return ProbeStep::Running;
        }
        if (_leaving) {
            return ProbeStep::Running;
        }
        if (_needLeave) {
            if (!_pace.TryTake(now)) {
                return ProbeStep::Running;
            }
            if (const auto result = _client.Leave(); !result) {
                return Fail(Format("{}: {}", result.error().code, result.error().message));
            }
            _needLeave = false;
            _leaving = true;
            _hadActivity = true;
            return ProbeStep::Running;
        }

        // Its turn: the view carries actions, and one version is answered once
        const StringView action = FirstActionText(_tape, _client.GetActionsText());
        if (_client.HasView() && !action.empty() && _client.GetViewVersion() != _actedVersion) {
            if (!_pace.TryTake(now)) {
                return ProbeStep::Running;
            }
            _actedVersion = _client.GetViewVersion();
            if (const auto result = _client.SendAction(action); !result) {
                return Fail(Format("{}: {}", result.error().code, result.error().message));
            }
            ++_actions;
            _hadActivity = true;
            if (_actions >= _wantedActions) {
                _client.Disconnect();
                return ProbeStep::Done;
            }
        }
        return ProbeStep::Running;
    }

    ProbeStep ProbeClient::Step(RoomTime now) {
        _client.Poll(now);
        _events.clear();
        _client.DrainEvents(_events);
        _hadActivity = !_events.empty();
        for (const RoomClientEvent& event : _events) {
            if (!HandleEvent(event)) {
                return ProbeStep::Failed;
            }
        }

        if (_client.IsFinished() && !_leaving && !_needLeave && !_needHost) {
            ++_hands;
            _needLeave = true;
        }
        return SendPending(now);
    }

}  // namespace BECore
