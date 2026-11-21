#include "GameControl.h"

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Time/Time.h>
#include <Games/GameManager.h>
#include <Games/IControllableGame.h>
#include <Games/ReplayControl.h>
#include <Games/SessionRecorder.h>

namespace BECore {

    namespace {

        struct ActiveGame {
            IGame* game = nullptr;
            IControllableGame* control = nullptr;
        };

        /// The active game with its control interface, or the error to answer with
        std::expected<ActiveGame, ControlError> FindActiveControllable() {
            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                return std::unexpected(MakeControlError(GameControlErrors::kNoActiveGame, "no game is active"));
            }
            IControllableGame* control = game->AsControllable();
            if (control == nullptr) {
                return std::unexpected(MakeControlError(GameControlErrors::kNotControllable, Format("the game '{}' cannot be controlled from outside", game->GetName().ToStringView())));
            }
            return ActiveGame{game, control};
        }

        /// {"game": name, "version": state version}: what every answer about the active game starts with
        Json DescribeGame(const ActiveGame& active) {
            Json result = Json::object();
            result["game"] = MakeJsonString(active.game->GetName().ToStringView());
            result["version"] = active.control->GetStateVersion();
            return result;
        }

        /**
         * Publishes `state_changed` at the end of a frame when the state version of the active game moved since the
         * last look. It sees every change: an action of a client, a reset, a click of a person. The first look only
         * records the version, because there is nothing to compare with and no client can have missed anything.
         */
        class StateChangeWatcher {
        public:
            explicit StateChangeWatcher(ControlService& service) : _service(service) {}

            void OnFrameEnd() {
                const auto active = FindActiveControllable();
                if (!active) {
                    _seen.reset();
                    return;
                }

                const String name(active->game->GetName().ToStringView());
                const uint64_t version = active->control->GetStateVersion();
                if (_seen && _seen->game == name && _seen->version == version) {
                    return;
                }
                const bool firstLook = !_seen;
                _seen = Seen{name, version};
                if (firstLook) {
                    return;
                }

                // The state is written first, apart: a state that fails leaves `state` out of the push, and a writer cannot be rewound after a partial write
                _state.clear();
                JsonWriter stateWriter(_state);
                const bool hasState = active->control->WriteState(stateWriter).has_value() && stateWriter.IsComplete();
                _service.Publish(GameControlEvents::kStateChanged, [&](JsonWriter& writer) {
                    writer.BeginObject();
                    writer.Member("game", active->game->GetName().ToStringView());
                    if (hasState) {
                        writer.Key("state");
                        writer.Raw(_state);
                    }
                    writer.Member("version", version);
                    writer.EndObject();
                });
            }

        private:
            struct Seen {
                String game;
                uint64_t version = 0;
            };

            ControlService& _service;
            Optional<Seen> _seen;
            String _state;  // the text of the state being pushed; keeps its capacity
        };

        /// `get_state` as a value: {"game", "version", "state"}
        std::expected<Json, ControlError> DescribeState(const ActiveGame& active) {
            auto state = active.control->GetState();
            if (!state) {
                return std::unexpected(state.error());
            }
            Json result = DescribeGame(active);
            result["state"] = std::move(*state);
            return result;
        }

        void HandleListGames(ControlCall& call) {
            const GameManager& manager = GameManager::GetInstance();

            Json games = Json::array();
            for (const IntrusivePtr<IGame>& game : manager.GetAll()) {
                Json entry = Json::object();
                entry["name"] = MakeJsonString(game->GetName().ToStringView());
                entry["scene"] = MakeJsonString(game->GetSceneName().ToStringView());
                entry["controllable"] = game->AsControllable() != nullptr;
                games.push_back(std::move(entry));
            }

            const IGame* active = manager.GetActive();
            Json result = Json::object();
            result["active"] = active != nullptr ? MakeJsonString(active->GetName().ToStringView()) : Json();
            result["games"] = std::move(games);
            call.Reply(std::move(result));
        }

        void HandleSwitchGame(ControlCall& call) {
            const auto name = GetJsonString(call.Params(), "name");
            if (!name) {
                call.Fail(ControlErrors::kInvalidParams, "'name' has to be a string");
                return;
            }

            // Look the name up instead of interning it: a client must not grow the PoolString table
            GameManager& manager = GameManager::GetInstance();
            for (const IntrusivePtr<IGame>& game : manager.GetAll()) {
                if (game->GetName().ToStringView() == StringView(name->c_str(), name->size())) {
                    if (manager.GetActive() != game.Get() && SessionRecorder::GetInstance().IsRecording()) {
                        // A file is of one game
                        LOG_INFO("The recording ended: the game switches to '{}'"_format(game->GetName().ToStringView()));
                        SessionRecorder::GetInstance().Stop();
                    }
                    manager.SwitchTo(game->GetName());
                    Json result = Json::object();
                    result["active"] = MakeJsonString(game->GetName().ToStringView());
                    call.Reply(std::move(result));
                    return;
                }
            }
            call.Fail(GameControlErrors::kUnknownGame, Format("no game is named '{}'", *name));
        }

        void HandleReset(ControlCall& call) {
            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                call.Fail(GameControlErrors::kNoActiveGame, "no game is active");
                return;
            }

            IControllableGame* control = game->AsControllable();
            if (control == nullptr) {
                GameManager::GetInstance().ResetActive();
                Json result = Json::object();
                result["game"] = MakeJsonString(game->GetName().ToStringView());
                call.Reply(std::move(result));
                return;
            }

            if (auto reset = ControlledReset(*control, call.Params()); !reset) {
                call.Fail(reset.error());
                return;
            }
            call.Reply(DescribeGame(ActiveGame{game, control}));
        }

        void HandleGetState(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            const uint64_t version = active->control->GetStateVersion();
            call.TryReply([&](JsonWriter& writer) -> std::expected<void, ControlError> {
                writer.BeginObject();
                writer.Member("game", active->game->GetName().ToStringView());
                writer.Key("state");
                if (auto written = active->control->WriteState(writer); !written) {
                    return written;
                }
                writer.Member("version", version);
                writer.EndObject();
                return {};
            });
        }

        void HandleListActions(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            const uint64_t version = active->control->GetStateVersion();
            call.TryReply([&](JsonWriter& writer) -> std::expected<void, ControlError> {
                writer.BeginObject();
                writer.Key("actions");
                if (auto written = active->control->WriteActions(writer); !written) {
                    return written;
                }
                writer.Member("game", active->game->GetName().ToStringView());
                writer.Member("version", version);
                writer.EndObject();
                return {};
            });
        }

        void HandlePerformAction(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            auto outcome = ControlledAction(*active->control, call.Params());
            if (!outcome) {
                call.Fail(outcome.error());
                return;
            }
            Json result = DescribeGame(*active);  // the version after the action
            result["outcome"] = std::move(*outcome);
            call.Reply(std::move(result));
        }

        void HandleSetState(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            if (auto loaded = ControlledSetState(*active->control, call.Params()); !loaded) {
                call.Fail(loaded.error());
                return;
            }
            call.Reply(DescribeGame(*active));
        }

        constexpr uint32_t kDefaultRunUntilFrames = 600;
        constexpr StringView kRunUntilPointer = "pointer";
        constexpr StringView kRunUntilEquals = "equals";
        constexpr StringView kRunUntilVersionMoved = "versionMoved";
        constexpr StringView kRunUntilEvent = "event";
        constexpr StringView kRunUntilEventName = "name";
        constexpr StringView kRunUntilEventEqual = "equal";

        /// What a `run_until` waits for: the three condition keys, one at a time
        CORE_ENUM(RunUntilKind, uint8_t, Pointer, VersionMoved, Event)

        /**
         * One `run_until` that waits for frames. The continuation of the deferred call holds it (the handler has returned by then), so whatever ends the wait
         * (an answer, an error, the limit, a Shutdown() that drops the call) destroys it, and the destructor gives the observer of `event` back.
         */
        struct RunUntilState {
            explicit RunUntilState(ControlService& owner) : service(owner) {}
            RunUntilState(const RunUntilState&) = delete;
            RunUntilState& operator=(const RunUntilState&) = delete;

            ~RunUntilState() {
                service.RemovePublishObserver(observer);
            }

            ControlService& service;
            RunUntilKind kind = RunUntilKind::Pointer;
            String pointer;
            Json equals;
            String eventName;
            Json eventEqual = Json::object();
            uint32_t maxFrames = kDefaultRunUntilFrames;
            double seconds = kDefaultStepSeconds;
            uint32_t frames = 0;
            uint64_t startVersion = 0;
            ControlService::PublishObserverId observer = ControlService::kNoPublishObserver;
            bool eventSeen = false;

            // What `get_state` answered the last time, read again only when the game or its state version moved
            Optional<Json> document;
            String documentGame;
            uint64_t documentVersion = 0;
        };

        /// True when every member of @p expected is in @p data with an equal value
        bool HasMembers(const Json& data, const Json& expected) {
            for (const auto& member : expected.items()) {
                const Json* found = FindJsonMember(data, StringView(member.key().c_str(), member.key().size()));
                if (found == nullptr || *found != member.value()) {
                    return false;
                }
            }
            return true;
        }

        std::expected<void, ControlError> ReadRunUntilPointer(const Json& condition, RunUntilState& state) {
            const auto pointer = GetJsonString(condition, kRunUntilPointer);
            if (!pointer) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'pointer' has to be a string"));
            }
            const Json* equals = FindJsonMember(condition, kRunUntilEquals);
            if (equals == nullptr) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "a 'pointer' condition needs 'equals'"));
            }
            // Only the syntax can be judged without the document
            if (const auto checked = FindJsonPointer(Json::object(), *pointer); !checked) {
                return std::unexpected(checked.error());
            }
            state.kind = RunUntilKind::Pointer;
            state.pointer = *pointer;
            state.equals = *equals;
            return {};
        }

        std::expected<void, ControlError> ReadRunUntilEvent(const Json& condition, RunUntilState& state) {
            const Json* event = FindJsonMember(condition, kRunUntilEvent);
            const auto name = event != nullptr ? GetJsonString(*event, kRunUntilEventName) : Optional<String>();
            if (event == nullptr || !event->is_object() || !name || name->empty()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'event' has to be an object with a non-empty string 'name'"));
            }
            for (const auto& member : event->items()) {
                const StringView key(member.key().c_str(), member.key().size());
                if (key != kRunUntilEventName && key != kRunUntilEventEqual) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'event' has no member '{}' (it has 'name' and 'equal')", key)));
                }
            }
            const Json* equal = FindJsonMember(*event, kRunUntilEventEqual);
            if (equal != nullptr && !equal->is_object()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'equal' has to be an object"));
            }
            state.kind = RunUntilKind::Event;
            state.eventName = *name;
            if (equal != nullptr) {
                state.eventEqual = *equal;
            }
            return {};
        }

        /// The condition of a `run_until`: exactly one of `pointer` (with `equals`), `versionMoved` or `event`, and nothing else
        std::expected<void, ControlError> ReadRunUntilCondition(const Json& params, RunUntilState& state) {
            const Json* condition = FindJsonMember(params, "condition");
            if (condition == nullptr || !condition->is_object()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'condition' has to be an object"));
            }

            const bool hasPointer = FindJsonMember(*condition, kRunUntilPointer) != nullptr;
            const bool hasVersionMoved = FindJsonMember(*condition, kRunUntilVersionMoved) != nullptr;
            const bool hasEvent = FindJsonMember(*condition, kRunUntilEvent) != nullptr;
            if (static_cast<int>(hasPointer) + static_cast<int>(hasVersionMoved) + static_cast<int>(hasEvent) != 1) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'condition' has to have exactly one of 'pointer' (with 'equals'), 'versionMoved' and 'event'"));
            }
            for (const auto& member : condition->items()) {
                const StringView key(member.key().c_str(), member.key().size());
                const bool known = key == kRunUntilPointer || key == kRunUntilVersionMoved || key == kRunUntilEvent || (hasPointer && key == kRunUntilEquals);
                if (!known) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'condition' has no member '{}' here", key)));
                }
            }

            if (hasPointer) {
                return ReadRunUntilPointer(*condition, state);
            }
            if (hasEvent) {
                return ReadRunUntilEvent(*condition, state);
            }
            if (GetJsonBool(*condition, kRunUntilVersionMoved) != true) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'versionMoved' has to be true"));
            }
            state.kind = RunUntilKind::VersionMoved;
            return {};
        }

        /// The reason a condition is met, or nullopt while it is not; an error when the game cannot be read any more
        std::expected<Optional<StringView>, ControlError> EvaluateRunUntil(RunUntilState& state) {
            if (state.kind == RunUntilKind::Event) {
                return state.eventSeen ? Optional<StringView>(kRunUntilEvent) : Optional<StringView>();
            }

            const auto active = FindActiveControllable();
            if (!active) {
                return std::unexpected(active.error());
            }
            const uint64_t version = active->control->GetStateVersion();
            if (state.kind == RunUntilKind::VersionMoved) {
                return version != state.startVersion ? Optional<StringView>(kRunUntilVersionMoved) : Optional<StringView>();
            }

            const StringView game = active->game->GetName().ToStringView();
            if (!state.document || version != state.documentVersion || game != StringView(state.documentGame)) {
                auto described = DescribeState(*active);
                if (!described) {
                    return std::unexpected(described.error());
                }
                state.document = std::move(*described);
                state.documentGame = String(game);
                state.documentVersion = version;
            }
            const auto found = FindJsonPointer(*state.document, state.pointer);
            if (!found) {
                return std::unexpected(found.error());
            }
            return *found != nullptr && **found == state.equals ? Optional<StringView>(kRunUntilPointer) : Optional<StringView>();
        }

        /// {met, frames, reason?, game?, version?} and the clock of `step`; the game is left out when none can be read (an `event` wait needs none)
        Json DescribeRunUntil(const RunUntilState& state, Optional<StringView> reason) {
            Json result = DescribeControlClock();
            result["met"] = reason.has_value();
            result["frames"] = state.frames;
            if (reason) {
                result["reason"] = MakeJsonString(*reason);
            }
            if (const auto active = FindActiveControllable()) {
                result.update(DescribeGame(*active));
            }
            return result;
        }

        /// Checks the condition, then answers or waits for one more frame; every frame is a barrier frame of its own, like a `step`
        void AdvanceRunUntil(ControlService& service, ControlCall& call, UniquePtr<RunUntilState> state) {
            const auto met = EvaluateRunUntil(*state);
            if (!met) {
                call.Fail(met.error());
                return;
            }
            if (*met || state->frames >= state->maxFrames) {
                call.Reply(DescribeRunUntil(*state, *met));
                return;
            }

            const double seconds = state->seconds;
            service.DeferSteppedFrames(call, 1, seconds, [&service, state = std::move(state)](ControlCall& again) mutable {
                ++state->frames;
                AdvanceRunUntil(service, again, std::move(state));
            });
        }

        void HandleRunUntil(ControlCall& call, ControlService& service) {
            auto state = MakeUnique<RunUntilState>(service);
            if (auto condition = ReadRunUntilCondition(call.Params(), *state); !condition) {
                call.Fail(condition.error());
                return;
            }
            const auto maxFrames = ReadControlFrames(call.Params(), "maxFrames", kDefaultRunUntilFrames);
            if (!maxFrames) {
                call.Fail(maxFrames.error());
                return;
            }
            const auto seconds = ReadControlFrameSeconds(call.Params());
            if (!seconds) {
                call.Fail(seconds.error());
                return;
            }
            state->maxFrames = *maxFrames;
            state->seconds = *seconds;

            if (state->kind == RunUntilKind::Event) {
                // Added now and kept by the state: an event of the first frame counts, and no way out of the wait leaves it behind
                state->observer = service.AddPublishObserver([raw = state.get()](StringView event, const Json& data) {
                    if (event == StringView(raw->eventName) && HasMembers(data, raw->eventEqual)) {
                        raw->eventSeen = true;
                    }
                });
            } else {
                const auto active = FindActiveControllable();
                if (!active) {
                    call.Fail(active.error());
                    return;
                }
                state->startVersion = active->control->GetStateVersion();
            }
            AdvanceRunUntil(service, call, std::move(state));
        }

    }  // namespace

    std::expected<void, ControlError> ControlledReset(IControllableGame& game, const Json& options) {
        auto reset = game.Reset(options);
        if (reset && SessionRecorder::GetInstance().IsRecording()) {
            SessionRecorder::GetInstance().ReportReset(game.GetReplayStart());
        }
        return reset;
    }

    std::expected<Json, ControlError> ControlledAction(IControllableGame& game, const Json& action) {
        auto outcome = game.PerformAction(action);
        if (outcome) {
            SessionRecorder::GetInstance().ReportAction(action, *outcome);
        }
        return outcome;
    }

    std::expected<void, ControlError> ControlledSetState(IControllableGame& game, const Json& state) {
        auto loaded = game.SetState(state);
        if (loaded) {
            SessionRecorder::GetInstance().ReportSetState(state);
        }
        return loaded;
    }

    void RegisterGameControl(ControlService& service) {
        service.RegisterHandler("list_games", HandleListGames);
        service.RegisterHandler("switch_game", HandleSwitchGame);
        service.RegisterHandler("reset", HandleReset);
        service.RegisterHandler("get_state", HandleGetState);
        service.RegisterHandler("list_actions", HandleListActions);
        service.RegisterHandler("perform_action", HandlePerformAction);
        service.RegisterHandler("set_state", HandleSetState);
        service.RegisterHandler("run_until", [&service](ControlCall& call) { HandleRunUntil(call, service); });

        // Resolved on every request: the game that is active now, by its most derived type
        service.Roots().Register(kGameRootName, ControlRoot{[] {
                                     IGame* game = GameManager::GetInstance().GetActive();
                                     return game == nullptr ? TypedRef{} : TypedRef::Of(*game);
                                 }});

        service.DescribeMethod("list_games", {"The registered games and the active one", {}});
        service.DescribeMethod("switch_game", {"Makes another game the active one", {{"name", "string", true, "The name of the game, as list_games gives it"}}});
        service.DescribeMethod("reset", {"Starts the active game again; the parameters are the game's own", {}});
        service.DescribeMethod("get_state", {"The state of the active game and its version", {}});
        service.DescribeMethod("list_actions", {"The actions the active game accepts now", {}});
        service.DescribeMethod("perform_action", {"Plays an action of the active game; the parameters are the game's (see list_actions and its control header)", {}});
        service.DescribeMethod("set_state", {"Sets the state of the active game; the parameters are the game's (see its control header)", {}});

        service.DescribeMethod(
            "run_until",
            {"Runs frames until a condition holds, checked before the first frame and after each one; the limit is an answer with met false, not an error. The answer carries the clock of step",
             {{"condition", "object", true,
               "Exactly one of {pointer, equals} (a JSON pointer into the answer of get_state), {versionMoved: true} or {event: {name, equal?}} (a pushed event whose data has these members)"},
              {"maxFrames", "integer", false, Format("Most frames to wait, 1 to {} (default {})", kMaxStepFrames, kDefaultRunUntilFrames)},
              {"dt", "number", false, Format("Seconds each frame lasts on the manual clock, 0 to {} (default 1/60)", Time::kMaxFrameTime)}}});

        RegisterReplayControl(service);

        service.SetFrameEndHook(GameControlEvents::kStateChanged, [watcher = MakeUnique<StateChangeWatcher>(service)] { watcher->OnFrameEnd(); });
    }

}  // namespace BECore
