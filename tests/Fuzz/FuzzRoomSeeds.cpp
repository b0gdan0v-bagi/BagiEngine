#include <BECore/Control/ControlParams.h>
#include <Common/JsonDoc.h>
#include <Fuzz/FuzzJsonText.h>
#include <Fuzz/FuzzRoomInput.h>
#include <Fuzz/FuzzSeeds.h>
#include <RoomServer/DemoCardGame.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomProtocol.h>

namespace BECore::Fuzz {

    namespace {

        constexpr const char* kRoomProtocolFolder = "room_protocol";
        constexpr const char* kRoomHubFolder = "room_hub";
        constexpr const char* kControlRequestFolder = "control_request";

        constexpr StringView kGame = "demo";
        constexpr StringView kUnknownCode = "ZZZZZ";
        constexpr size_t kBigOptionsBytes = RoomHubInput::kMaxMessageBytes + 100;
        constexpr uint16_t kPlayPause = 900;
        constexpr uint16_t kLongPause = 40000;
        constexpr uint16_t kListPause = 2100;  // a little over the refill of the list bucket
        constexpr int kPlayRounds = 6;
        constexpr int kFailedJoins = 6;
        constexpr int kListFlood = RoomLimits::kListBurst + 2;

        FuzzBytes TextWithHeader(uint8_t header, StringView text) {
            FuzzBytes bytes;
            bytes.push_back(header);
            bytes.insert(bytes.end(), text.begin(), text.end());
            return bytes;
        }

        // =============================================================================
        // room_protocol: one message per file, made by the builders of the protocol
        // =============================================================================

        void AddRoomProtocolSeeds(Vector<FuzzSeedFile>& seeds) {
            using RoomProtocolInput::PackHeader;
            const auto client = [&seeds](const char* name, const ClientMessage& message, size_t limitIndex = 0) {
                seeds.push_back({kRoomProtocolFolder, name, TextWithHeader(PackHeader(false, limitIndex), SerializeClientMessage(message))});
            };
            const auto server = [&seeds](const char* name, const String& text, size_t limitIndex = 0) {
                seeds.push_back({kRoomProtocolFolder, name, TextWithHeader(PackHeader(true, limitIndex), text)});
            };

            // Game documents as the old DOM dump wrote them: the keys in byte order
            constexpr StringView options = R"({"firstSeat":2,"rules":["fast",3,null,true]})";
            client("client-create-room.bin", CreateRoomMessage{String(kGame), "Ann", String(options)});
            client("client-create-room-name-spaces.bin", CreateRoomMessage{String(kGame), "   Bob the Builder  ", String()});
            client("client-create-room-name-utf8.bin", CreateRoomMessage{String(kGame), "Zo\xC3\xAB \xF0\x9F\x98\x80", String()});
            client("client-create-room-name-bidi.bin", CreateRoomMessage{String(kGame), "Mal\xE2\x80\xAEory", String()});
            client("client-create-room-name-too-long.bin", CreateRoomMessage{String(kGame), String(RoomLimits::kMaxNameBytes + 1, 'n'), String()});
            client("client-create-room-small-limit.bin", CreateRoomMessage{String(kGame), "Ann", String(options)}, 1);
            client("client-create-room-public.bin", CreateRoomMessage{String(kGame), "Ann", String(options), true});
            client("client-list-rooms.bin", ListRoomsMessage{String(kGame)});
            JoinRoomMessage joinByName;
            joinByName.code = "ABCDE";
            joinByName.name = "Cy";
            client("client-join-by-name.bin", joinByName);
            JoinRoomMessage joinByToken;
            joinByToken.code = "ABCDE";
            joinByToken.token = String(RoomLimits::kTokenLength, 'a');
            client("client-join-by-token.bin", joinByToken);
            client("client-choose-seat.bin", ChooseSeatMessage{3});
            client("client-choose-seat-negative.bin", ChooseSeatMessage{-7});
            client("client-start.bin", StartMessage{});
            constexpr StringView card = R"({"card":"c1_2","type":"play"})";
            client("client-action.bin", ActionMessage{12, String(card)});
            client("client-leave.bin", LeaveMessage{});
            client("client-ping.bin", PingMessage{});

            server("server-joined.bin", MakeJoinedMessage("ABCDE", 3, String(RoomLimits::kTokenLength, 'b'), 2, false));
            LobbySnapshot lobby;
            lobby.room = "ABCDE";
            lobby.game = kGame;
            lobby.phase = RoomPhase::Lobby;
            lobby.hostSeat = 0;
            lobby.seats = {LobbySeat{0, SeatKind::Human, "Ann", true}, LobbySeat{1, SeatKind::Human, "Bob", false}, LobbySeat{2, SeatKind::Computer, {}, false},
                           LobbySeat{3, SeatKind::Empty, {}, false}};
            server("server-lobby.bin", MakeLobbyMessage(lobby));
            constexpr StringView view = R"({"hand":["c1_0","c1_1"],"turn":1})";
            const String actions = Format("[{}]", card);
            server("server-view.bin", MakeViewMessage("ABCDE", 1, 7, false, String(view), actions));
            server("server-view-small-limit.bin", MakeViewMessage("ABCDE", 1, 7, false, String(view), actions), 1);
            server("server-error.bin", MakeErrorMessage(RoomErrors::kNotYourTurn, "it is not your turn", RoomMessageTypes::kAction));
            server("server-left.bin", MakeLeftMessage("closed"));
            server("server-pong.bin", MakePongMessage());
            RoomListMessage list;
            list.game = kGame;
            list.rooms = {RoomListing{"ABCDE", "Ann", 1, DemoCardGame::kSeatCount, false, R"({"firstSeat":2})"}, RoomListing{"FGHJK", "Bob", 3, DemoCardGame::kSeatCount, false, String()}};
            server("server-rooms.bin", MakeRoomListMessage(list));
            list.rooms.clear();
            list.more = true;
            server("server-rooms-empty.bin", MakeRoomListMessage(list));
        }

        // =============================================================================
        // room_hub: sessions recorded against a real hub, so that the codes, the tokens and the versions in them are the ones the hub makes
        // =============================================================================

        /// A hub driven from code, recording the operations it was driven by as an input of FuzzRoomHub
        class Recorder {
        public:
            Recorder() : _hub(_registry, RoomHubInput::MakeSettings(), RoomHubInput::MakeRandom(), MakeSink()) {
                RoomHubInput::RegisterDemoGame(_registry);
            }

            void Connect(uint8_t number) {
                RoomHubInput::AppendOperation(_bytes, RoomHubInput::Op::Connect, number, 0);
                _hub.OnConnected(RoomHubInput::ConnectionOf(number), _now);
            }

            void Disconnect(uint8_t number) {
                RoomHubInput::AppendOperation(_bytes, RoomHubInput::Op::Disconnect, number, 0);
                _hub.OnDisconnected(RoomHubInput::ConnectionOf(number), _now);
            }

            void Tick(uint16_t milliseconds) {
                RoomHubInput::AppendOperation(_bytes, RoomHubInput::Op::Tick, 0, milliseconds);
                _now += std::chrono::milliseconds(milliseconds);
                _hub.Tick(_now);
            }

            void Send(uint8_t number, const ClientMessage& message) {
                const String text = SerializeClientMessage(message);
                RoomHubInput::AppendMessage(_bytes, number, text);
                _hub.OnMessage(RoomHubInput::ConnectionOf(number), text, _now);
            }

            void Create(uint8_t number, StringView name, StringView game = kGame, bool isPublic = false) {
                Send(number, CreateRoomMessage{String(game), String(name), String(), isPublic});
            }

            void List(uint8_t number, StringView game = kGame) {
                Send(number, ListRoomsMessage{String(game)});
            }

            void JoinByName(uint8_t number, StringView code, StringView name) {
                JoinRoomMessage join;
                join.code = String(code);
                join.name = String(name);
                Send(number, join);
            }

            void JoinByToken(uint8_t number, StringView code, StringView token) {
                JoinRoomMessage join;
                join.code = String(code);
                join.token = String(token);
                Send(number, join);
            }

            /// The member plays the first action its last view offers; nothing when it has none
            void PlayFirstAction(uint8_t number) {
                const Known& known = _known[number];
                if (known.firstAction.empty()) {
                    return;
                }
                Send(number, ActionMessage{known.version, known.firstAction});
            }

            [[nodiscard]] const String& RoomOf(uint8_t number) {
                return _known[number].room;
            }

            /// The code of the first room of the last list the connection was sent; empty when it has none
            [[nodiscard]] const String& FirstListedOf(uint8_t number) {
                return _known[number].listed;
            }

            [[nodiscard]] const String& TokenOf(uint8_t number) {
                return _known[number].token;
            }

            [[nodiscard]] FuzzBytes TakeBytes() const {
                return _bytes;
            }

        private:
            struct Known {
                String room;
                String token;
                String listed;
                uint64_t version = 0;
                /// The text of the first action of the last view; empty when it offered none
                String firstAction;
            };

            HubSink MakeSink() {
                HubSink sink;
                sink.send = [this](ConnectionId id, String text) {
                    const Tests::JsonDoc parsed(text);
                    if (!parsed.IsParsed()) {
                        return;
                    }
                    const JsonView root = parsed.Root();
                    Known& known = _known[static_cast<uint8_t>(id - 1)];
                    const auto type = GetParamString(root, "type").value_or(String());
                    if (type == RoomMessageTypes::kJoined) {
                        known.room = GetParamString(root, "room").value_or(String());
                        known.token = GetParamString(root, "token").value_or(String());
                    } else if (type == RoomMessageTypes::kRooms) {
                        known.listed.clear();
                        const Optional<JsonView> rooms = FindParam(root, "rooms");
                        if (rooms && rooms->IsArray() && rooms->Size() > 0) {
                            known.listed = GetParamString(Tests::ItemAt(*rooms, 0), "room").value_or(String());
                        }
                    } else if (type == RoomMessageTypes::kView) {
                        known.version = static_cast<uint64_t>(GetParamInt(root, "version").value_or(0));
                        known.firstAction.clear();
                        const Optional<JsonView> actions = FindParam(root, "actions");
                        if (actions && actions->IsArray() && actions->Size() > 0) {
                            known.firstAction = String(Tests::ItemAt(*actions, 0).Raw());
                        }
                    }
                };
                sink.close = [](ConnectionId, String) {};
                return sink;
            }

            SeatedGameRegistry _registry;
            RoomHub _hub;
            RoomTime _now{std::chrono::seconds(1000)};
            FuzzBytes _bytes;
            Map<uint8_t, Known> _known;
        };

        /// Three humans in a room, a seat chosen, a start, and rounds of plays by whoever has an action
        FuzzBytes RecordPlay() {
            Recorder recorder;
            recorder.Connect(0);
            recorder.Create(0, "Ann");
            const String code = recorder.RoomOf(0);
            recorder.Connect(1);
            recorder.JoinByName(1, code, "Bob");
            recorder.Connect(2);
            recorder.JoinByName(2, code, "Cy");
            recorder.Send(1, ChooseSeatMessage{3});
            recorder.Send(0, PingMessage{});
            recorder.Send(0, StartMessage{});
            for (int round = 0; round < kPlayRounds; ++round) {
                for (uint8_t member = 0; member < 3; ++member) {
                    recorder.PlayFirstAction(member);
                }
                recorder.Tick(kPlayPause);
            }
            return recorder.TakeBytes();
        }

        /// A member drops during the game, the computer plays for it, and it comes back by its token on another connection
        FuzzBytes RecordReconnect() {
            Recorder recorder;
            recorder.Connect(0);
            recorder.Create(0, "Ann");
            const String code = recorder.RoomOf(0);
            recorder.Connect(1);
            recorder.JoinByName(1, code, "Bob");
            recorder.Send(0, StartMessage{});
            recorder.PlayFirstAction(0);
            const String token = recorder.TokenOf(1);
            recorder.Disconnect(1);
            recorder.Tick(kLongPause);
            recorder.Connect(2);
            recorder.JoinByToken(2, code, token);
            recorder.Tick(kPlayPause);
            recorder.PlayFirstAction(2);
            recorder.Send(2, LeaveMessage{});
            recorder.Send(0, LeaveMessage{});
            recorder.Tick(kLongPause);
            return recorder.TakeBytes();
        }

        /// Messages that are refused: guessed codes (the connection is closed), a start by a guest, a seat out of range, an action out of turn, a join after the start
        FuzzBytes RecordRefusals() {
            Recorder recorder;
            recorder.Connect(0);
            for (int attempt = 0; attempt < kFailedJoins; ++attempt) {
                recorder.JoinByName(0, kUnknownCode, "Guesser");
            }
            recorder.Connect(1);
            recorder.Create(1, "Host", "nogame");
            recorder.Send(1, StartMessage{});
            recorder.Send(1, ChooseSeatMessage{9});
            recorder.Send(1, ActionMessage{0, String()});
            recorder.Send(1, LeaveMessage{});
            recorder.Create(1, "Host");
            const String code = recorder.RoomOf(1);
            recorder.Create(1, "Host again");
            recorder.Connect(2);
            recorder.JoinByName(2, code, "Guest");
            recorder.Send(2, StartMessage{});
            recorder.Send(2, ChooseSeatMessage{1});
            recorder.Send(1, ChooseSeatMessage{1});
            recorder.Send(1, StartMessage{});
            recorder.Send(1, ChooseSeatMessage{2});
            recorder.Send(1, ActionMessage{99, String()});
            recorder.Send(1, ActionMessage{0, R"({"card":"c3_0","type":"play"})"});
            recorder.Connect(3);
            recorder.JoinByName(3, code, "Late");
            recorder.JoinByToken(3, code, String(RoomLimits::kTokenLength, '0'));
            recorder.Send(3, CreateRoomMessage{String(kGame), "Big", Format(R"({{"pad":"{}"}})", String(kBigOptionsBytes, 'x'))});
            recorder.Tick(kLongPause);
            return recorder.TakeBytes();
        }

        /// Two public rooms and a private one are listed by a connection in no room; it joins the first from the list; a member asks for a list and is refused;
        /// a list of an unknown game and a list flood past the bucket
        FuzzBytes RecordList() {
            Recorder recorder;
            for (uint8_t number = 0; number < 4; ++number) {
                recorder.Connect(number);
            }
            recorder.List(0);
            recorder.Create(1, "Open", kGame, true);
            recorder.Create(2, "Closed");
            recorder.Connect(4);
            recorder.List(4);
            recorder.List(4, "nogame");
            recorder.JoinByName(4, recorder.FirstListedOf(4), "Browser");
            recorder.List(4);
            recorder.Send(1, StartMessage{});
            recorder.Create(3, "Second", kGame, true);
            recorder.Connect(5);
            for (int attempt = 0; attempt < kListFlood; ++attempt) {
                recorder.List(5);
            }
            recorder.Tick(kListPause);
            recorder.List(5);
            recorder.Disconnect(1);
            recorder.Tick(kListPause);
            recorder.List(5);
            recorder.Tick(kLongPause);
            return recorder.TakeBytes();
        }

        /// More connections than the hub takes and more rooms than it holds
        FuzzBytes RecordFullHub() {
            Recorder recorder;
            for (uint8_t number = 0; number < RoomHubInput::kMaxConnections + 2; ++number) {
                recorder.Connect(number);
            }
            for (uint8_t number = 0; number < RoomHubInput::kMaxRooms + 1; ++number) {
                recorder.Create(number, "Host");
            }
            recorder.JoinByName(RoomHubInput::kMaxRooms, recorder.RoomOf(0), "Late");
            recorder.Send(RoomHubInput::kMaxConnections - 1, PingMessage{});
            recorder.Disconnect(0);
            recorder.Tick(kLongPause);
            return recorder.TakeBytes();
        }

        // =============================================================================
        // control_request: every method of the API with parameters as the tools send them
        // =============================================================================

        /// One request; @p id and @p params are JSON text (empty @p params: the request has none). The keys are written in byte order, as the DOM dump did
        FuzzBytes Request(StringView id, StringView method, StringView params = StringView()) {
            const String request = params.empty() ? Format(R"({{"id":{},"method":"{}"}})", id, method) : Format(R"({{"id":{},"method":"{}","params":{}}})", id, method, params);
            return TextBytes(SortedText(request));
        }

        constexpr int kNestedWithinLimit = 30;
        constexpr int kBeyondRequestDepth = 70;

        /// A `ProbeNode` holding @p depth nodes in a chain, each one a level of object and a level of array
        String NodeChain(int depth) {
            String text;
            for (int level = 0; level < depth; ++level) {
                text += R"({"children":[)";
            }
            text += R"({"id":1})";
            for (int level = 0; level < depth; ++level) {
                text += Format(R"(],"id":{}}})", level);
            }
            return text;
        }

        void AddControlSeeds(Vector<FuzzSeedFile>& seeds) {
            const auto add = [&seeds](const char* name, FuzzBytes bytes) { seeds.push_back({kControlRequestFolder, name, std::move(bytes)}); };
            add("ping.bin", Request("1", "ping"));
            add("status.bin", Request("2", "status"));
            add("step.bin", Request("3", "step", R"({"frames":3,"dt":0.016})"));
            add("step-bad-frames.bin", Request("4", "step", R"({"frames":1.5})"));
            add("log-mark.bin", Request("40", "log_mark"));
            add("log-since.bin", Request("41", "log_since", R"({"mark":0,"level":"error"})"));
            add("log-since-bad-mark.bin", Request("42", "log_since", R"({"mark":-1,"level":"info"})"));
            add("log-subscribe.bin", Request("43", "log_subscribe", R"({"enabled":true,"level":"warning"})"));
            add("batch.bin",
                Request("44", "batch",
                        R"({"calls":[{"method":"ping"},{"method":"status","params":{}},{"method":"batch","params":{"calls":[{"method":"ping"}]}},{"method":"quit"}],"continueOnError":true})"));
            add("batch-step.bin", Request("45", "batch", R"({"calls":[{"method":"step","params":{"frames":2}},{"method":"get_state"}]})"));
            add("batch-bad-entry.bin", Request("46", "batch", R"({"calls":[{"method":"ping"},"status",{"method":7}]})"));
            add("pause.bin", Request("5", "pause"));
            add("resume.bin", Request("6", "resume"));
            add("quit.bin", Request(R"("quit-1")", "quit"));
            add("list-games.bin", Request("7", "list_games"));
            add("switch-game.bin", Request("8", "switch_game", R"({"name":"Stress"})"));
            add("reset.bin", Request("9", "reset", R"({"seed":42})"));
            add("get-state.bin", Request("10", "get_state"));
            add("list-actions.bin", Request("11", "list_actions"));
            add("perform-action.bin", Request("12", "perform_action", R"({"type":"swap","from":[0,1],"to":[0,2]})"));
            add("set-state.bin", Request("13", "set_state", R"({"fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"})"));
            add("screenshot.bin", Request("14", "screenshot", R"({"maxSize":256})"));
            add("ui-tree.bin", Request("15", "ui_tree"));
            add("ui-click.bin", Request("16", "ui_click", R"({"name":"PlayButton","canvas":"Hud","at":0.5})"));
            add("scene-tree.bin", Request("50", "scene_tree", R"({"node":"Board","limit":2})"));
            add("scene-get.bin", Request("51", "scene_get", R"({"node":"Board/Cell[1]","component":"ChangeToRandomColorDebugComponent","index":0})"));
            add("scene-get-bad-path.bin", Request("52", "scene_get", R"({"node":"Board/a\\/b\\x[99999999999]","component":"TransformComponent","path":"x"})"));
            add("scene-set.bin", Request("53", "scene_set", R"({"node":"Board","component":"QuadRendererComponent","path":"color.r","value":200})"));
            add("scene-set-huge.bin", Request("54", "scene_set", R"({"node":"Sparks","component":"ParticleEmitterComponent","path":"effect.maxParticles","value":18446744073709551615})"));
            add("scene-set-name.bin", Request("56", "scene_set", R"({"node":"Board/Cell[1]","component":"ChangeToRandomColorDebugComponent","path":"actionFilter","value":""})"));
            add("scene-set-interning.bin", Request("55", "scene_set", R"({"node":"Board/Cell[1]","component":"ChangeToRandomColorDebugComponent","path":"actionFilter","value":"fuzz_seed_action"})"));
            add("scene-set-node.bin", Request("57", "scene_set_node", R"({"node":"Board/Cell","layer":-2147483648})"));
            add("frame-stats.bin", Request("17", "frame_stats", R"({"frames":10})"));
            add("profiler-dump.bin", Request("18", "profiler_dump", R"({"path":"trace.json"})"));
            add("zone-allocs.bin", Request("19", "zone_allocs", R"({"top":5,"reset":false})"));
            add("list-types.bin", Request("22", "list_types", R"({"base":"IWidget","recursive":false,"creatable":true})"));
            add("list-types-bad-flag.bin", Request("23", "list_types", R"({"base":5,"recursive":"yes"})"));
            add("describe-type.bin", Request("24", "describe_type", R"({"name":"ProbeState"})"));
            add("describe-type-unknown.bin", Request("25", "describe_type", R"({"name":"NoSuchType"})"));
            add("schema.bin", Request("26", "schema"));
            add("get-property.bin", Request("27", "get_property", R"({"root":"state","path":"leaves[0].label"})"));
            add("get-property-bad-path.bin", Request("28", "get_property", R"({"root":"state","path":"tree.children[99999999999999999999]..x"})"));
            add("set-property.bin", Request("29", "set_property", R"({"root":"state","path":"i32","value":7})"));
            add("set-property-bad-value.bin", Request("30", "set_property", R"({"root":"state","path":"i8","value":[1,2]})"));
            add("set-property-huge.bin", Request("31", "set_property", R"({"root":"state","path":"u64","value":18446744073709551615})"));
            add("set-property-interning.bin", Request("32", "set_property", R"({"root":"state","path":"name","value":"fuzz_seed_name"})"));
            add("call-method.bin", Request("33", "call_method", R"({"root":"probe","method":"Total","arguments":{"numbers":[1,2,3]}})"));
            add("call-method-bad-argument.bin", Request("34", "call_method", R"({"root":"probe","method":"Echo","arguments":{"text":5,"extra":true}})"));
            add("call-method-deep.bin", Request("35", "call_method", Format(R"({{"root":"probe","method":"Leaves","arguments":{{"node":{}}}}})", NodeChain(kBeyondRequestDepth))));
            add("call-method-nested.bin", Request("38", "call_method", Format(R"({{"root":"probe","method":"Leaves","arguments":{{"node":{}}}}})", NodeChain(kNestedWithinLimit))));
            add("call-method-unknown-root.bin", Request("36", "call_method", R"({"root":"nowhere","method":"Touch"})"));
            add("call-method-game-root.bin", Request("37", "call_method", R"({"root":"game","method":"Anything"})"));
            add("unknown-method.bin", Request("20", "no_such_method", R"({"x":{}})"));
            add("params-not-an-object.bin", Request("21", "ping", "[1,2]"));
            add("id-is-a-float.bin", Request("1.5", "ping"));
        }

    }  // namespace

    void AddRoomAndControlSeeds(Vector<FuzzSeedFile>& seeds) {
        AddRoomProtocolSeeds(seeds);
        seeds.push_back({kRoomHubFolder, "play.bin", RecordPlay()});
        seeds.push_back({kRoomHubFolder, "reconnect-by-token.bin", RecordReconnect()});
        seeds.push_back({kRoomHubFolder, "refusals.bin", RecordRefusals()});
        seeds.push_back({kRoomHubFolder, "full-hub.bin", RecordFullHub()});
        seeds.push_back({kRoomHubFolder, "list.bin", RecordList()});
        AddControlSeeds(seeds);
    }

}  // namespace BECore::Fuzz
