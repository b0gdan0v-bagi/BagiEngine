#include "GameControl.h"

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Time/Time.h>
#include <Games/GameControlAnswers.h>
#include <Games/GameManager.h>
#include <Games/GameText.h>
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

        /// The answer of a call that changed the active game and says nothing else
        GameVersionAnswer MakeVersionAnswer(const ActiveGame& active) {
            GameVersionAnswer answer;
            answer.game = active.game->GetName();
            answer.version = active.control->GetStateVersion();
            return answer;
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
                GameStateAnswer answer;
                answer.game = active->game->GetName();
                if (hasState) {
                    answer.state = JsonPayload::Text(_state);
                }
                answer.version = version;
                _service.Publish(GameControlEvents::kStateChanged, [&answer](JsonWriter& writer) { answer.WriteJson(writer); });
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

        /// `get_state` as a document, {"game", "state", "version"}, written into @p out (cleared first): what the `pointer` of `run_until` is evaluated against
        std::expected<void, ControlError> WriteStateDocument(const ActiveGame& active, String& out) {
            out.clear();
            JsonWriter writer(out);
            writer.BeginObject();
            writer.Member("game", active.game->GetName());
            writer.Key("state");
            if (auto written = active.control->WriteState(writer); !written) {
                return std::unexpected(written.error());
            }
            writer.Member("version", active.control->GetStateVersion());
            writer.EndObject();
            if (!writer.IsComplete()) {
                return std::unexpected(MakeControlError(ControlErrors::kInternalError, "the game wrote no complete state"));
            }
            return {};
        }

        void HandleListGames(ControlCall& call) {
            const GameManager& manager = GameManager::GetInstance();
            const IGame* active = manager.GetActive();
            GameListAnswer answer;
            if (active != nullptr) {
                answer.active = active->GetName();
            }
            answer.games.reserve(manager.GetAll().size());
            for (const IntrusivePtr<IGame>& game : manager.GetAll()) {
                GameListedGame& listed = answer.games.emplace_back();
                listed.controllable = game->AsControllable() != nullptr;
                listed.name = game->GetName();
                listed.scene = game->GetSceneName();
            }
            call.ReplyObject(answer);
        }

        void HandleSwitchGame(ControlCall& call) {
            const auto name = GetParamString(call.Params(), "name");
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
                        LOG_INFO("The recording ended: the game switches to '{}'", game->GetName().ToStringView());
                        SessionRecorder::GetInstance().Stop();
                    }
                    manager.SwitchTo(game->GetName());
                    GameSwitchAnswer answer;
                    answer.active = game->GetName();
                    call.ReplyObject(answer);
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
                GameVersionAnswer answer;
                answer.game = game->GetName();
                call.ReplyObject(answer);
                return;
            }

            if (auto reset = ControlledReset(*control, call.Params()); !reset) {
                call.Fail(reset.error());
                return;
            }
            call.ReplyObject(MakeVersionAnswer(ActiveGame{game, control}));
        }

        void HandleGetState(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            const auto writeState = [&](JsonWriter& writer) { return active->control->WriteState(writer); };
            GameStateAnswer answer;
            answer.game = active->game->GetName();
            answer.state = JsonPayload::Streamed(writeState);
            answer.version = active->control->GetStateVersion();
            call.ReplyObject(answer);
        }

        void HandleListActions(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            const auto writeActions = [&](JsonWriter& writer) { return active->control->WriteActions(writer); };
            GameActionsAnswer answer;
            answer.actions = JsonPayload::Streamed(writeActions);
            answer.game = active->game->GetName();
            answer.version = active->control->GetStateVersion();
            call.ReplyObject(answer);
        }

        /// @p outcome is the buffer of the registered handler, which keeps its capacity from call to call
        void HandlePerformAction(ControlCall& call, String& outcome) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            if (const auto performed = ControlledAction(*active->control, call.Params(), outcome); !performed) {
                call.Fail(performed.error());
                return;
            }
            GameActionAnswer answer;
            answer.game = active->game->GetName();
            answer.outcome = JsonPayload::Text(outcome);
            answer.version = active->control->GetStateVersion();  // the one after the action
            call.ReplyObject(answer);
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
            call.ReplyObject(MakeVersionAnswer(*active));
        }

        constexpr uint32_t kDefaultRunUntilFrames = 600;

        /// The member names of a `condition`; `pointer`, `versionMoved` and `event` are also the `reason` of a met wait
        struct RunUntilNames {
            static constexpr auto pointer = "pointer"_intern;
            static constexpr auto equals = "equals"_intern;
            static constexpr auto versionMoved = "versionMoved"_intern;
            static constexpr auto event = "event"_intern;
            static constexpr auto eventName = "name"_intern;
            static constexpr auto eventEqual = "equal"_intern;
        };

        /// What a `run_until` waits for: the three condition keys, one at a time
        CORE_ENUM(RunUntilKind, uint8_t, Pointer, VersionMoved, Event)

        /**
         * One `run_until` that waits for frames. The continuation of the deferred call holds it (the handler has returned by then), so whatever ends the wait
         * (an answer, an error, the limit, a Shutdown() that drops the call) destroys it, and the destructor gives the observer of `event` back.
         *
         * A tape borrows its text, so every text lives next to its tape and the state never moves: it is built in place and held by a `UniquePtr`. The texts
         * are copies, because the call's own params are gone by the time the wait ends.
         */
        struct RunUntilState {
            explicit RunUntilState(ControlService& owner) : service(owner) {}
            RunUntilState(const RunUntilState&) = delete;
            RunUntilState& operator=(const RunUntilState&) = delete;

            ~RunUntilState() {
                service.RemovePublishObserver(observer);
            }

            /// Copies @p value into @p text and tapes it there. The text was read once already, so it parses; were it not to, the tape stays empty
            static void Keep(JsonView value, String& text, JsonTape& tape) {
                text = String(value.Raw());
                static_cast<void>(tape.Parse(text));
            }

            ControlService& service;
            RunUntilKind kind = RunUntilKind::Pointer;
            String pointer;
            String equalsText;
            JsonTape equalsTape;
            String eventName;
            String eventEqualText;
            JsonTape eventEqualTape;  // empty when the event has no `equal`: every event of the name counts
            uint32_t maxFrames = kDefaultRunUntilFrames;
            double seconds = kDefaultStepSeconds;
            uint32_t frames = 0;
            uint64_t startVersion = 0;
            ControlService::PublishObserverId observer = ControlService::kNoPublishObserver;
            bool eventSeen = false;

            // What `get_state` answers, read again only when the game or its state version moved
            String documentText;
            JsonTape documentTape;
            bool hasDocument = false;
            String documentGame;
            uint64_t documentVersion = 0;
        };

        /// True when every member of @p expected is in @p data with an equal value
        bool HasMembers(JsonView data, JsonView expected) {
            String decoded;
            for (const JsonMember member : expected.Members()) {
                StringView key;
                if (const Optional<StringView> plain = member.key.Text()) {
                    key = *plain;
                } else {
                    member.key.ReadText(decoded);
                    key = decoded;
                }
                const Optional<JsonView> found = data.Find(key);
                if (!found || !JsonEquals(*found, member.value)) {
                    return false;
                }
            }
            return true;
        }

        std::expected<void, ControlError> ReadRunUntilPointer(JsonView condition, RunUntilState& state) {
            const auto pointer = GetParamString(condition, RunUntilNames::pointer.View());
            if (!pointer) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'pointer' has to be a string"));
            }
            const Optional<JsonView> equals = FindParam(condition, RunUntilNames::equals.View());
            if (!equals) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "a 'pointer' condition needs 'equals'"));
            }
            // Only the syntax can be judged without the document
            if (const auto checked = CheckJsonPointer(*pointer); !checked) {
                return std::unexpected(checked.error());
            }
            state.kind = RunUntilKind::Pointer;
            state.pointer = *pointer;
            RunUntilState::Keep(*equals, state.equalsText, state.equalsTape);
            return {};
        }

        std::expected<void, ControlError> ReadRunUntilEvent(JsonView condition, RunUntilState& state) {
            const Optional<JsonView> event = FindParam(condition, RunUntilNames::event.View());
            const auto name = event ? GetParamString(*event, RunUntilNames::eventName.View()) : Optional<String>();
            if (!event || !event->IsObject() || !name || name->empty()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'event' has to be an object with a non-empty string 'name'"));
            }
            String key;
            for (const JsonMember member : event->Members()) {
                if (!member.key.TextEquals(RunUntilNames::eventName.View()) && !member.key.TextEquals(RunUntilNames::eventEqual.View())) {
                    member.key.ReadText(key);
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'event' has no member '{}' (it has 'name' and 'equal')", key)));
                }
            }
            const Optional<JsonView> equal = FindParam(*event, RunUntilNames::eventEqual.View());
            if (equal && !equal->IsObject()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'equal' has to be an object"));
            }
            state.kind = RunUntilKind::Event;
            state.eventName = *name;
            if (equal) {
                RunUntilState::Keep(*equal, state.eventEqualText, state.eventEqualTape);
            }
            return {};
        }

        /// The condition of a `run_until`: exactly one of `pointer` (with `equals`), `versionMoved` or `event`, and nothing else
        std::expected<void, ControlError> ReadRunUntilCondition(JsonView params, RunUntilState& state) {
            const Optional<JsonView> condition = FindParam(params, "condition");
            if (!condition || !condition->IsObject()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'condition' has to be an object"));
            }

            const bool hasPointer = FindParam(*condition, RunUntilNames::pointer.View()).has_value();
            const bool hasVersionMoved = FindParam(*condition, RunUntilNames::versionMoved.View()).has_value();
            const bool hasEvent = FindParam(*condition, RunUntilNames::event.View()).has_value();
            if (static_cast<int>(hasPointer) + static_cast<int>(hasVersionMoved) + static_cast<int>(hasEvent) != 1) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'condition' has to have exactly one of 'pointer' (with 'equals'), 'versionMoved' and 'event'"));
            }
            String key;
            for (const JsonMember member : condition->Members()) {
                const bool known = member.key.TextEquals(RunUntilNames::pointer.View()) || member.key.TextEquals(RunUntilNames::versionMoved.View()) ||
                                   member.key.TextEquals(RunUntilNames::event.View()) || (hasPointer && member.key.TextEquals(RunUntilNames::equals.View()));
                if (!known) {
                    member.key.ReadText(key);
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'condition' has no member '{}' here", key)));
                }
            }

            if (hasPointer) {
                return ReadRunUntilPointer(*condition, state);
            }
            if (hasEvent) {
                return ReadRunUntilEvent(*condition, state);
            }
            if (GetParamBool(*condition, RunUntilNames::versionMoved.View()) != true) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'versionMoved' has to be true"));
            }
            state.kind = RunUntilKind::VersionMoved;
            return {};
        }

        /// The reason a condition is met, or nullopt while it is not; an error when the game cannot be read any more
        std::expected<Optional<PoolString>, ControlError> EvaluateRunUntil(RunUntilState& state) {
            if (state.kind == RunUntilKind::Event) {
                return state.eventSeen ? Optional<PoolString>(RunUntilNames::event) : Optional<PoolString>();
            }

            const auto active = FindActiveControllable();
            if (!active) {
                return std::unexpected(active.error());
            }
            const uint64_t version = active->control->GetStateVersion();
            if (state.kind == RunUntilKind::VersionMoved) {
                return version != state.startVersion ? Optional<PoolString>(RunUntilNames::versionMoved) : Optional<PoolString>();
            }

            const StringView game = active->game->GetName().ToStringView();
            if (!state.hasDocument || version != state.documentVersion || game != StringView(state.documentGame)) {
                // Not yet marked as read: a document that fails below is written again by the next look
                state.hasDocument = false;
                if (auto written = WriteStateDocument(*active, state.documentText); !written) {
                    return std::unexpected(written.error());
                }
                if (const auto taped = state.documentTape.Parse(state.documentText); !taped) {
                    return std::unexpected(MakeControlError(ControlErrors::kInternalError, Format("the state cannot be read as a document: {}", taped.error().message)));
                }
                state.hasDocument = true;
                state.documentGame = String(game);
                state.documentVersion = version;
            }
            const auto found = FindJsonPointer(JsonView(state.documentTape, 0), state.pointer);
            if (!found) {
                return std::unexpected(found.error());
            }
            return *found && JsonEquals(**found, JsonView(state.equalsTape, 0)) ? Optional<PoolString>(RunUntilNames::pointer) : Optional<PoolString>();
        }

        /// The answer of a `run_until` that ended: the clock of `step`; the game is left out when none can be read (an `event` wait needs none)
        GameRunUntilAnswer MakeRunUntilAnswer(const RunUntilState& state, Optional<PoolString> reason) {
            GameRunUntilAnswer answer;
            static_cast<ControlClock&>(answer) = ControlClock::Read();
            answer.frames = state.frames;
            answer.met = reason.has_value();
            answer.reason = reason;
            if (const auto active = FindActiveControllable()) {
                answer.game = active->game->GetName();
                answer.version = active->control->GetStateVersion();
            }
            return answer;
        }

        /// Checks the condition, then answers or waits for one more frame; every frame is a barrier frame of its own, like a `step`
        void AdvanceRunUntil(ControlService& service, ControlCall& call, UniquePtr<RunUntilState> state) {
            const auto met = EvaluateRunUntil(*state);
            if (!met) {
                call.Fail(met.error());
                return;
            }
            if (*met || state->frames >= state->maxFrames) {
                call.ReplyObject(MakeRunUntilAnswer(*state, *met));
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
                state->observer = service.AddPublishObserver([raw = state.get()](StringView event, JsonView data) {
                    if (event == StringView(raw->eventName) && HasMembers(data, JsonView(raw->eventEqualTape, 0))) {
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

    std::expected<void, ControlError> ControlledReset(IControllableGame& game, JsonView options) {
        auto reset = game.Reset(options);
        if (reset && SessionRecorder::GetInstance().IsRecording()) {
            const auto start = ReplayStartText(game);
            if (!start) {
                LOG_WARNING("The recording keeps no start of the game: {}", start.error().message);
            }
            // No text is recorded as `{}`, which a replay then reports as a start that differs
            SessionRecorder::GetInstance().ReportReset(start ? StringView(*start) : StringView());
        }
        return reset;
    }

    std::expected<void, ControlError> ControlledAction(IControllableGame& game, JsonView action, String& outcomeText) {
        outcomeText.clear();
        JsonWriter writer(outcomeText);
        auto performed = game.PerformAction(action, writer);
        if (performed && !writer.IsComplete()) {
            performed = std::unexpected(MakeControlError(ControlErrors::kInternalError, "the game answered an action without one complete outcome"));
        }
        if (!performed) {
            outcomeText.clear();
            return performed;
        }
        if (SessionRecorder::GetInstance().IsRecording()) {
            SessionRecorder::GetInstance().ReportAction(action, outcomeText);
        }
        return performed;
    }

    std::expected<void, ControlError> ControlledSetState(IControllableGame& game, JsonView state) {
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
        service.RegisterHandler("perform_action", [outcome = MakeUnique<String>()](ControlCall& call) { HandlePerformAction(call, *outcome); });
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
