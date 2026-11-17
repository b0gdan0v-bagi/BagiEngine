#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kFrameGuard = 100;
        constexpr int64_t kSetSeed = 40;

        Json ReadyPoll() {
            Json preset = Json::object();
            preset["name"] = "wood";
            preset["title"] = "wood";
            Json params = Json::object();
            params["state"] = "ready";
            params["generator"] = "fake";
            params["presets"] = Json::array({preset});
            return params;
        }

        Json Report(uint32_t id) {
            Json params = Json::object();
            params["id"] = id;
            return params;
        }

        String PathText(const std::filesystem::path& path) {
            const std::string text = path.generic_string();  //-NoStd: the text of a std::filesystem::path
            return String(text.c_str(), text.size());
        }

    }  // namespace

    class ArtGenSetSessionTest : public ::testing::Test {
    protected:
        ArtGenSetSessionTest() : session([this] { return now; }) {}

        void SetUp() override {
            _root = TestTempDirectory("artgen_set_session");
            std::filesystem::remove_all(_root);
            std::filesystem::create_directories(_root);
            session.SetGameRoot(PathText(_root));
            session.RegisterControl(service);
        }

        void TearDown() override {
            std::error_code error;
            std::filesystem::remove_all(_root, error);
        }

        Json Call(StringView method, const Json& params = Json::object()) {
            Json request = Json::object();
            request["id"] = ++_nextId;
            request["method"] = ToStdString(method);
            request["params"] = params;
            Optional<Json> response;
            service.Post(DumpJson(request), [&response](String reply) {
                auto parsed = ParseJson(reply);
                response = parsed ? std::move(*parsed) : Json();
            });
            for (int frame = 0; !response && frame < kFrameGuard; ++frame) {
                service.Drain();
                service.EndFrame();
            }
            return response ? std::move(*response) : Json();
        }

        static const Json& Result(const Json& response) {
            static const Json kNull;
            const Json* result = FindJsonMember(response, "result");
            return result != nullptr ? *result : kNull;
        }

        /// A poll of the service; its reply
        Json Poll() {
            return Result(Call("artgen_poll", ReadyPoll()));
        }

        std::expected<String, String> Enqueue(StringView name, StringView lines, int32_t count = 2, Optional<int64_t> seed = kSetSeed) {
            ArtGenSetRequest request;
            request.preset = "wood";
            request.subjects = ParseArtGenSubjects(lines).value();
            request.count = count;
            request.seed = seed;
            request.name = String(name);
            return session.EnqueueSet(std::move(request));
        }

        /// The job the last poll handed over: its request, or null
        static const Json* RequestOf(const Json& reply) {
            return FindJsonMember(reply, "request");
        }

        /// What the service does for the request it got: an image per seed, then the end
        void Finish(const Json& request, StringView status, StringView error = {}) {
            const uint32_t id = static_cast<uint32_t>(GetJsonInt(request, "id").value());
            const int64_t seed = GetJsonInt(request, "seed").value();
            const String outDir = GetJsonString(request, "outDir").value();
            const String name = GetJsonString(request, "name").value();
            if (status == "ok") {
                const int64_t count = GetJsonInt(request, "count").value();
                for (int64_t i = 0; i < count; ++i) {
                    Json image = Report(id);
                    image["seed"] = seed + i;
                    image["path"] = ToStdString(Format("{}/artgen/out/{}/{}_{}.png", PathText(_root), outDir, name, seed + i));
                    image["provenance"] = ToStdString(Format("{}/artgen/out/{}/{}_{}.artgen.json", PathText(_root), outDir, name, seed + i));
                    Call("artgen_image", image);
                }
            }
            Json done = Report(id);
            done["status"] = ToStdString(status);
            if (!error.empty()) {
                done["error"] = ToStdString(error);
            }
            Call("artgen_done", done);
        }

        static String ErrorCode(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            return error != nullptr ? GetJsonString(*error, "code").value_or(String()) : String();
        }

        /// The manifest as it is on disk now
        ArtGenSet OnDisk(StringView set) const {
            const auto text = ReadTextFile(ManifestPath(_root, set));
            return ArtGenSetFromJson(ParseJson(text.value()).value()).value();
        }

        static ArtGenSetJobState StateOf(const ArtGenSet& set, size_t job) {
            return set.jobs[job].state;
        }

        double now = 100.0;
        ControlService service;
        ArtGenSession session;
        std::filesystem::path _root;

    private:
        int64_t _nextId = 0;
    };

    TEST_F(ArtGenSetSessionTest, JobsGoOutOneByOneWithTheSameSeedAndTheirOwnFolders) {
        ASSERT_TRUE(Enqueue("pieces", "rook: a rook\nknight: a knight\nbishop: a bishop").has_value());
        EXPECT_TRUE(session.IsBusy());
        EXPECT_EQ(OnDisk("pieces").jobs.size(), 3u);

        const Json first = Poll();
        const Json* request = RequestOf(first);
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(GetJsonString(*request, "outDir"), Optional<String>("pieces/rook"));
        EXPECT_EQ(GetJsonString(*request, "name"), Optional<String>("rook"));
        EXPECT_EQ(GetJsonString(*request, "subject"), Optional<String>("a rook"));
        EXPECT_EQ(GetJsonInt(*request, "seed"), Optional<int64_t>(kSetSeed));
        EXPECT_EQ(GetJsonInt(*request, "count"), Optional<int64_t>(2));
        EXPECT_EQ(StateOf(OnDisk("pieces"), 0), ArtGenSetJobState::Running);
        EXPECT_EQ(StateOf(OnDisk("pieces"), 1), ArtGenSetJobState::Pending);

        // One job at a time: a poll while it runs hands over nothing
        EXPECT_EQ(RequestOf(Poll()), nullptr);

        Finish(*request, "ok");
        {
            const ArtGenSet disk = OnDisk("pieces");
            EXPECT_EQ(StateOf(disk, 0), ArtGenSetJobState::Done);
            ASSERT_EQ(disk.jobs[0].variants.size(), 2u);
            EXPECT_EQ(disk.jobs[0].variants[1].seed, kSetSeed + 1);
            EXPECT_EQ(disk.jobs[0].variants[1].file, "rook/rook_41.png");
            EXPECT_EQ(disk.jobs[0].variants[1].provenance, "rook/rook_41.artgen.json");
        }

        const Json second = Poll();
        request = RequestOf(second);
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(GetJsonString(*request, "outDir"), Optional<String>("pieces/knight"));
        EXPECT_EQ(GetJsonInt(*request, "seed"), Optional<int64_t>(kSetSeed));
        Finish(*request, "ok");

        const Json third = Poll();
        request = RequestOf(third);
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(GetJsonString(*request, "outDir"), Optional<String>("pieces/bishop"));
        Finish(*request, "ok");

        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(CountJobs(*session.FindSet("pieces"), ArtGenSetJobState::Done), 3);
        EXPECT_EQ(RequestOf(Poll()), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, TheSetDrawsASharedSeedWhenNoneIsGiven) {
        ASSERT_TRUE(Enqueue("drawn", "a\nb", 1, Optional<int64_t>()).has_value());
        const ArtGenSet& set = *session.FindSet("drawn");
        EXPECT_GE(set.seed, 0);
        EXPECT_EQ(OnDisk("drawn").seed, set.seed);
        const Json first = Poll();
        EXPECT_EQ(GetJsonInt(*RequestOf(first), "seed"), Optional<int64_t>(set.seed));
        Finish(*RequestOf(first), "ok");
        const Json second = Poll();
        EXPECT_EQ(GetJsonInt(*RequestOf(second), "seed"), Optional<int64_t>(set.seed));
    }

    TEST_F(ArtGenSetSessionTest, AFailedMiddleJobDoesNotStopTheSet) {
        ASSERT_TRUE(Enqueue("fails", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "out of memory");
        {
            const ArtGenSet disk = OnDisk("fails");
            EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Failed);
            EXPECT_EQ(disk.jobs[1].error, "out of memory");
        }
        const Json third = Poll();
        ASSERT_NE(RequestOf(third), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(third), "name"), Optional<String>("c"));
        Finish(*RequestOf(third), "ok");
        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(StateOf(OnDisk("fails"), 2), ArtGenSetJobState::Done);
    }

    TEST_F(ArtGenSetSessionTest, ACancelledPendingJobIsSkippedAtOnce) {
        ASSERT_TRUE(Enqueue("skip", "a\nb\nc").has_value());
        EXPECT_EQ(session.CancelJob("skip", "b"), ArtGenSetEdit::Ok);
        EXPECT_EQ(StateOf(OnDisk("skip"), 1), ArtGenSetJobState::Cancelled);
        EXPECT_EQ(OnDisk("skip").jobs[1].error, "cancelled");
        Finish(*RequestOf(Poll()), "ok");
        const Json next = Poll();
        EXPECT_EQ(GetJsonString(*RequestOf(next), "name"), Optional<String>("c"));
        EXPECT_EQ(session.CancelJob("skip", "nothing"), ArtGenSetEdit::NoSet);
        EXPECT_EQ(session.CancelJob("nothing", "a"), ArtGenSetEdit::NoSet);
    }

    TEST_F(ArtGenSetSessionTest, CancellingTheRunningJobReachesThePollAndTheSetGoesOn) {
        ASSERT_TRUE(Enqueue("running", "a\nb").has_value());
        const Json first = Poll();
        ASSERT_NE(RequestOf(first), nullptr);
        EXPECT_EQ(session.CancelJob("running", "a"), ArtGenSetEdit::Ok);
        // The job ends when the service says so, not before
        EXPECT_EQ(StateOf(OnDisk("running"), 0), ArtGenSetJobState::Running);
        const Json cancel = Poll();
        EXPECT_EQ(GetJsonInt(cancel, "cancel"), GetJsonInt(*RequestOf(first), "id"));
        Finish(*RequestOf(first), "cancelled");
        EXPECT_EQ(StateOf(OnDisk("running"), 0), ArtGenSetJobState::Cancelled);
        const Json next = Poll();
        ASSERT_NE(RequestOf(next), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(next), "name"), Optional<String>("b"));
    }

    TEST_F(ArtGenSetSessionTest, ArtgenCancelWithoutParamsCancelsOnlyTheRunningJob) {
        ASSERT_TRUE(Enqueue("plain", "a\nb").has_value());
        const Json first = Poll();
        Call("artgen_cancel");
        Poll();
        Finish(*RequestOf(first), "cancelled");
        const Json next = Poll();
        ASSERT_NE(RequestOf(next), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(next), "name"), Optional<String>("b"));
    }

    TEST_F(ArtGenSetSessionTest, CancellingASetStopsEverythingOfIt) {
        ASSERT_TRUE(Enqueue("all", "a\nb\nc").has_value());
        ASSERT_TRUE(Enqueue("later", "x").has_value());
        const Json first = Poll();
        EXPECT_EQ(session.CancelSet("all"), ArtGenSetEdit::Ok);
        {
            const ArtGenSet disk = OnDisk("all");
            EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Cancelled);
            EXPECT_EQ(StateOf(disk, 2), ArtGenSetJobState::Cancelled);
        }
        Poll();
        Finish(*RequestOf(first), "cancelled");
        EXPECT_FALSE(IsActive(*session.FindSet("all")));
        // The next set in line starts
        const Json next = Poll();
        ASSERT_NE(RequestOf(next), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(next), "outDir"), Optional<String>("later/x"));
        EXPECT_EQ(session.CancelSet("nothing"), ArtGenSetEdit::NoSet);
    }

    TEST_F(ArtGenSetSessionTest, CancellingARunNobodyPolledYetNeedsNoService) {
        ASSERT_TRUE(Enqueue("queued", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        // The end of the first job starts the next as a queued run that the service has not seen yet
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Queued);
        EXPECT_EQ(session.GetRun().jobName, "b");
        EXPECT_EQ(session.CancelJob("queued", "b"), ArtGenSetEdit::Ok);
        EXPECT_EQ(StateOf(OnDisk("queued"), 1), ArtGenSetJobState::Cancelled);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Queued);
        EXPECT_EQ(session.GetRun().jobName, "c");
        EXPECT_EQ(GetJsonString(*RequestOf(Poll()), "name"), Optional<String>("c"));
    }

    TEST_F(ArtGenSetSessionTest, RetryPutsFailedAndCancelledJobsBackAndClearsTheEarlierTry) {
        ASSERT_TRUE(Enqueue("retry", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "boom");
        session.CancelJob("retry", "c");
        EXPECT_FALSE(IsActive(*session.FindSet("retry")));
        ASSERT_TRUE(session.PickVariant("retry", "a", kSetSeed) == ArtGenSetEdit::Ok);

        EXPECT_EQ(session.RetryJobs("retry", "b"), ArtGenSetEdit::Ok);
        {
            const ArtGenSet disk = OnDisk("retry");
            EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Pending);
            EXPECT_TRUE(disk.jobs[1].error.empty());
            EXPECT_EQ(StateOf(disk, 2), ArtGenSetJobState::Cancelled);
            // A job that is Done is not touched by a retry of the set
            EXPECT_EQ(disk.jobs[0].pick, Optional<int64_t>(kSetSeed));
        }
        EXPECT_EQ(session.RetryJobs("retry"), ArtGenSetEdit::Ok);
        EXPECT_EQ(StateOf(OnDisk("retry"), 2), ArtGenSetJobState::Pending);
        EXPECT_EQ(StateOf(OnDisk("retry"), 0), ArtGenSetJobState::Done);

        const Json next = Poll();
        ASSERT_NE(RequestOf(next), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(next), "name"), Optional<String>("b"));
        EXPECT_EQ(session.RetryJobs("retry", "nothing"), ArtGenSetEdit::NoSet);
    }

    TEST_F(ArtGenSetSessionTest, ARetryOfAnOlderSetWaitsBehindTheSetsQueuedBefore) {
        ASSERT_TRUE(Enqueue("old", "a").has_value());
        Finish(*RequestOf(Poll()), "failed", "boom");
        ASSERT_TRUE(Enqueue("newer", "x").has_value());
        session.RetryJobs("old");
        const Json next = Poll();
        EXPECT_EQ(GetJsonString(*RequestOf(next), "outDir"), Optional<String>("newer/x"));
        Finish(*RequestOf(next), "ok");
        EXPECT_EQ(GetJsonString(*RequestOf(Poll()), "outDir"), Optional<String>("old/a"));
    }

    TEST_F(ArtGenSetSessionTest, OrdinaryRunsAreRefusedWhileASetIsActive) {
        ASSERT_TRUE(Enqueue("busy", "a").has_value());
        EXPECT_TRUE(session.IsBusy());
        EXPECT_FALSE(session.Request("wood", "queen", 1, 1).has_value());
        EXPECT_FALSE(session.RequestSetup(ArtGenSetupAction::Plan).has_value());
        Json params = Json::object();
        params["preset"] = "wood";
        params["subject"] = "queen";
        const Json refused = Call("artgen_generate", params);
        const Json* error = FindJsonMember(refused, "error");
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(GetJsonString(*error, "code"), Optional<String>("artgen_busy"));

        Finish(*RequestOf(Poll()), "ok");
        EXPECT_FALSE(session.IsBusy());
        EXPECT_TRUE(session.Request("wood", "queen", 1, 1).has_value());
    }

    TEST_F(ArtGenSetSessionTest, JobsWaitWhileNoServicePolls) {
        ASSERT_TRUE(Enqueue("wait", "a\nb").has_value());
        session.PumpSets();
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Idle);
        EXPECT_EQ(StateOf(*session.FindSet("wait"), 0), ArtGenSetJobState::Pending);

        const Json first = Poll();
        ASSERT_NE(RequestOf(first), nullptr);
        // The service is silent past the timeout when the job ends: the next one waits for a poll
        now += ArtGenSession::kServiceTimeoutSeconds + 1.0;
        Finish(*RequestOf(first), "ok");
        EXPECT_EQ(StateOf(*session.FindSet("wait"), 1), ArtGenSetJobState::Pending);
        EXPECT_EQ(GetJsonString(*RequestOf(Poll()), "name"), Optional<String>("b"));
    }

    TEST_F(ArtGenSetSessionTest, DisconnectCancelsTheRunningJobAndLeavesTheRestPending) {
        ASSERT_TRUE(Enqueue("bye", "a\nb\nc").has_value());
        ASSERT_NE(RequestOf(Poll()), nullptr);
        session.RequestServiceStop();
        const Json answer = Poll();
        EXPECT_EQ(GetJsonBool(answer, "shutdown"), Optional<bool>(true));
        {
            const ArtGenSet disk = OnDisk("bye");
            EXPECT_EQ(StateOf(disk, 0), ArtGenSetJobState::Cancelled);
            EXPECT_EQ(disk.jobs[0].error, "the service was stopped");
            EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Pending);
            EXPECT_EQ(StateOf(disk, 2), ArtGenSetJobState::Pending);
        }
        // No pump while the service is gone; a service that polls again takes the next job
        session.PumpSets();
        EXPECT_EQ(StateOf(*session.FindSet("bye"), 1), ArtGenSetJobState::Pending);
        const Json again = Poll();
        ASSERT_NE(RequestOf(again), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(again), "name"), Optional<String>("b"));
    }

    TEST_F(ArtGenSetSessionTest, ARestartShowsTheInterruptedJobsCancelled) {
        ASSERT_TRUE(Enqueue("crash", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        ASSERT_NE(RequestOf(Poll()), nullptr);

        ArtGenSession restarted([this] { return now; });
        restarted.SetGameRoot(PathText(_root));
        const ArtGenSet* set = restarted.FindSet("crash");
        ASSERT_NE(set, nullptr);
        EXPECT_EQ(set->jobs[0].state, ArtGenSetJobState::Done);
        EXPECT_EQ(set->jobs[1].state, ArtGenSetJobState::Cancelled);
        EXPECT_EQ(set->jobs[1].error, "the editor was closed");
        EXPECT_EQ(set->jobs[2].state, ArtGenSetJobState::Cancelled);
        EXPECT_FALSE(restarted.IsBusy());

        // Retry re-queues them
        EXPECT_EQ(restarted.RetryJobs("crash"), ArtGenSetEdit::Ok);
        EXPECT_TRUE(restarted.IsBusy());
    }

    TEST_F(ArtGenSetSessionTest, TheSameRootAgainKeepsTheSetsInMemory) {
        ASSERT_TRUE(Enqueue("keep", "a").has_value());
        ASSERT_NE(RequestOf(Poll()), nullptr);
        // The panel calls this at every switch of the run mode; a reload would turn the running job into Cancelled
        session.SetGameRoot(PathText(_root));
        EXPECT_EQ(StateOf(*session.FindSet("keep"), 0), ArtGenSetJobState::Running);
    }

    TEST_F(ArtGenSetSessionTest, AReportOfAnOlderRunIsStale) {
        ASSERT_TRUE(Enqueue("stale", "a\nb").has_value());
        const Json first = Poll();
        const uint32_t firstId = static_cast<uint32_t>(GetJsonInt(*RequestOf(first), "id").value());
        Finish(*RequestOf(first), "ok");
        const Json second = Poll();
        ASSERT_NE(RequestOf(second), nullptr);

        Json late = Report(firstId);
        late["seed"] = 99;
        late["path"] = "x.png";
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_image", late)), "stale"), Optional<bool>(true));
        Json done = Report(firstId);
        done["status"] = "failed";
        done["error"] = "late";
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_done", done)), "stale"), Optional<bool>(true));

        const ArtGenSet disk = OnDisk("stale");
        EXPECT_EQ(disk.jobs[0].variants.size(), 2u);
        EXPECT_EQ(StateOf(disk, 0), ArtGenSetJobState::Done);
        EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Running);
        EXPECT_TRUE(disk.jobs[1].variants.empty());
    }

    TEST_F(ArtGenSetSessionTest, APickIsKeptInTheManifest) {
        ASSERT_TRUE(Enqueue("pick", "a").has_value());
        Finish(*RequestOf(Poll()), "ok");
        EXPECT_EQ(session.PickVariant("pick", "a", 12345), ArtGenSetEdit::NoVariant);
        EXPECT_EQ(session.PickVariant("pick", "zzz", kSetSeed), ArtGenSetEdit::NoSet);
        EXPECT_EQ(session.PickVariant("pick", "a", kSetSeed + 1), ArtGenSetEdit::Ok);
        EXPECT_EQ(OnDisk("pick").jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
    }

    TEST_F(ArtGenSetSessionTest, EnqueueRefusesWhatItCannotKeep) {
        EXPECT_FALSE(Enqueue("ok", "a", 0).has_value());
        EXPECT_FALSE(Enqueue("ok", "a", 17).has_value());
        EXPECT_FALSE(Enqueue("ok", "a", 2, -1).has_value());
        EXPECT_FALSE(Enqueue("bad name", "a").has_value());
        EXPECT_FALSE(Enqueue("../up", "a").has_value());
        ASSERT_TRUE(Enqueue("taken", "a").has_value());
        const auto again = Enqueue("taken", "b");
        ASSERT_FALSE(again.has_value());
        EXPECT_NE(again.error().find("taken"), String::npos);
        EXPECT_EQ(session.GetSets().size(), 1u);

        ArtGenSetRequest empty;
        empty.preset = "wood";
        EXPECT_FALSE(session.EnqueueSet(empty).has_value());
        ArtGenSetRequest noPreset;
        noPreset.subjects = ParseArtGenSubjects("a").value();
        EXPECT_FALSE(session.EnqueueSet(noPreset).has_value());

        ArtGenSession noRoot;
        ArtGenSetRequest request;
        request.preset = "wood";
        request.subjects = ParseArtGenSubjects("a").value();
        EXPECT_FALSE(noRoot.EnqueueSet(request).has_value());
    }

    TEST_F(ArtGenSetSessionTest, ABrokenPresetByNameIsRefusedAndADraftIsChecked) {
        Json broken = Json::object();
        broken["name"] = "broken";
        broken["error"] = "no workflow";
        Json params = ReadyPoll();
        params["presets"].push_back(broken);
        Call("artgen_poll", params);
        ArtGenSetRequest request;
        request.preset = "broken";
        request.subjects = ParseArtGenSubjects("a").value();
        const auto refused = session.EnqueueSet(request);
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("broken"), String::npos);

        request.preset = "form";
        request.presetData = Json::object();
        const auto notPreset = session.EnqueueSet(request);
        ASSERT_FALSE(notPreset.has_value());
        EXPECT_NE(notPreset.error().find("not a preset"), String::npos);
    }

    TEST_F(ArtGenSetSessionTest, ADraftTravelsInlineWithEveryJob) {
        ArtGenSetRequest request;
        request.preset = "form";
        request.presetData = ParseJson(R"({"preset":1,"title":"form","workflow":"txt2img","prompt":"{subject}"})").value();
        request.subjects = ParseArtGenSubjects("a").value();
        request.name = "drafted";
        request.seed = 3;
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        const Json first = Poll();
        const Json* sent = RequestOf(first);
        ASSERT_NE(sent, nullptr);
        EXPECT_EQ(FindJsonMember(*sent, "preset")->type(), Json::value_t::object);
        EXPECT_EQ(GetJsonString(*sent, "presetName"), Optional<String>("form"));
        EXPECT_EQ(OnDisk("drafted").presetData, request.presetData);
    }

    TEST_F(ArtGenSetSessionTest, EnqueueTakesSubjectsAsLinesAsAListAndAsObjects) {
        Json lines = Json::object();
        lines["preset"] = "wood";
        lines["subjects"] = "# pieces\nrook: a rook\n\nWhite Knight!";
        lines["set"] = "from_lines";
        lines["seed"] = 5;
        lines["count"] = 3;
        const Json first = Result(Call("artgen_enqueue", lines));
        EXPECT_EQ(GetJsonString(first, "set"), Optional<String>("from_lines"));
        EXPECT_EQ(GetJsonInt(first, "seed"), Optional<int64_t>(5));
        ASSERT_NE(FindJsonMember(first, "jobs"), nullptr);
        EXPECT_EQ(*FindJsonMember(first, "jobs"), (Json::array({"rook", "white_knight"})));
        EXPECT_EQ(GetJsonBool(first, "serviceConnected"), Optional<bool>(false));
        EXPECT_EQ(OnDisk("from_lines").count, 3);

        Json list = Json::object();
        list["preset"] = "wood";
        list["subjects"] = Json::array({"bishop: a bishop", "a queen"});
        list["set"] = "from_list";
        const Json second = Result(Call("artgen_enqueue", list));
        ASSERT_NE(FindJsonMember(second, "jobs"), nullptr);
        EXPECT_EQ(*FindJsonMember(second, "jobs"), (Json::array({"bishop", "a_queen"})));
        EXPECT_EQ(OnDisk("from_list").count, kArtGenSetDefaultCount);

        Json objects = Json::object();
        objects["preset"] = "wood";
        objects["subjects"] = Json::array({Json{{"name", "pawn"}, {"subject", "a small pawn"}}, Json{{"subject", "A Tall King"}}});
        objects["set"] = "from_objects";
        objects["seed"] = nullptr;
        const Json third = Result(Call("artgen_enqueue", objects));
        ASSERT_NE(FindJsonMember(third, "jobs"), nullptr);
        EXPECT_EQ(*FindJsonMember(third, "jobs"), (Json::array({"pawn", "a_tall_king"})));
        const ArtGenSet disk = OnDisk("from_objects");
        EXPECT_EQ(disk.jobs[0].subject, "a small pawn");
        EXPECT_EQ(GetJsonInt(third, "seed"), Optional<int64_t>(disk.seed)) << "no seed: one is drawn and told";

        Json draft = Json::object();
        draft["preset"] = ParseJson(R"({"preset":1,"title":"form","workflow":"txt2img","prompt":"{subject}"})").value();
        draft["presetName"] = "form";
        draft["subjects"] = "a";
        draft["set"] = "from_draft";
        EXPECT_EQ(GetJsonString(Result(Call("artgen_enqueue", draft)), "set"), Optional<String>("from_draft"));
        EXPECT_EQ(OnDisk("from_draft").preset, "form");
    }

    TEST_F(ArtGenSetSessionTest, EnqueueRefusesBadParamsAsInvalidParams) {
        const auto code = [this](const Json& params) {
            const String found = ErrorCode(Call("artgen_enqueue", params));
            return found.empty() ? String("ok") : found;
        };
        Json good = Json::object();
        good["preset"] = "wood";
        good["subjects"] = "a\nb";

        Json noPreset = good;
        noPreset.erase("preset");
        EXPECT_EQ(code(noPreset), "invalid_params");
        Json numberPreset = good;
        numberPreset["preset"] = 5;
        EXPECT_EQ(code(numberPreset), "invalid_params");
        Json emptyPreset = good;
        emptyPreset["preset"] = "";
        EXPECT_EQ(code(emptyPreset), "invalid_params");
        Json badDraft = good;
        badDraft["preset"] = Json::object();
        EXPECT_EQ(code(badDraft), "invalid_params") << "an object that is not a preset";

        Json noSubjects = good;
        noSubjects.erase("subjects");
        EXPECT_EQ(code(noSubjects), "invalid_params");
        const Json badSubjects = Json::array({Json::array(), Json(""), Json("# only a comment"), Json(5), Json::array({5}), Json::array({Json::object()}), Json::array({"a: x", "a: y"}),
                                              Json::array({Json{{"name", "bad name"}, {"subject", "x"}}}), Json::array({Json{{"name", 3}, {"subject", "x"}}}), Json::array({"!!!"})});
        for (const Json& subjects : badSubjects) {
            Json bad = good;
            bad["subjects"] = subjects;
            EXPECT_EQ(code(bad), "invalid_params") << DumpJson(subjects).c_str();
        }

        const Json badCounts = Json::array({0, 17, -1, "4", 1.5, int64_t{1} << 40});
        for (const Json& count : badCounts) {
            Json bad = good;
            bad["count"] = count;
            EXPECT_EQ(code(bad), "invalid_params") << DumpJson(count).c_str();
        }
        Json negativeSeed = good;
        negativeSeed["seed"] = -3;
        EXPECT_EQ(code(negativeSeed), "invalid_params");
        Json textSeed = good;
        textSeed["seed"] = "7";
        EXPECT_EQ(code(textSeed), "invalid_params");
        const Json badNames = Json::array({"bad name", "../up", "", 3, ToStdString(String(65, 'a'))});
        for (const Json& name : badNames) {
            Json bad = good;
            bad["set"] = name;
            EXPECT_EQ(code(bad), "invalid_params") << DumpJson(name).c_str();
        }

        Json tooMany = good;
        String lines;
        for (int32_t i = 0; i <= kMaxSetJobs; ++i) {
            lines += Format("job{}: x\n", i);
        }
        tooMany["subjects"] = ToStdString(lines);
        EXPECT_EQ(code(tooMany), "invalid_params");

        EXPECT_TRUE(session.GetSets().empty()) << "a refusal keeps nothing";
        good["set"] = "taken";
        EXPECT_EQ(code(good), "ok");
        EXPECT_EQ(code(good), "invalid_params") << "the folder exists";
    }

    TEST_F(ArtGenSetSessionTest, ArtgenSetAnswersTheManifestAndItsState) {
        ASSERT_TRUE(Enqueue("shown", "a\nb").has_value());
        Json params = Json::object();
        params["set"] = "shown";
        {
            const Json answer = Result(Call("artgen_set", params));
            EXPECT_EQ(GetJsonString(answer, "state"), Optional<String>("Active"));
            Json withoutState = answer;
            withoutState.erase("state");
            EXPECT_EQ(withoutState, ArtGenSetToJson(OnDisk("shown")));
        }
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "boom");
        const Json answer = Result(Call("artgen_set", params));
        EXPECT_EQ(GetJsonString(answer, "state"), Optional<String>("Complete"));
        Json withoutState = answer;
        withoutState.erase("state");
        EXPECT_EQ(withoutState, ArtGenSetToJson(OnDisk("shown")));
        const Json* jobs = FindJsonMember(answer, "jobs");
        ASSERT_NE(jobs, nullptr);
        ASSERT_EQ(jobs->size(), 2u);
        EXPECT_EQ(GetJsonString((*jobs)[1], "error"), Optional<String>("boom"));
        const Json* variants = FindJsonMember((*jobs)[0], "variants");
        ASSERT_NE(variants, nullptr);
        ASSERT_EQ(variants->size(), 2u);
        EXPECT_EQ(GetJsonString((*variants)[0], "file"), Optional<String>("a/a_40.png"));

        EXPECT_EQ(ErrorCode(Call("artgen_set", Json::object())), "invalid_params");
        params["set"] = "nothing";
        EXPECT_EQ(ErrorCode(Call("artgen_set", params)), "artgen_no_set");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenPickAndRetryAnswerWhatTheyDid) {
        ASSERT_TRUE(Enqueue("edit", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "boom");
        session.CancelJob("edit", "c");

        Json pick = Json::object();
        pick["set"] = "edit";
        pick["job"] = "a";
        pick["seed"] = kSetSeed + 1;
        const Json picked = Result(Call("artgen_pick", pick));
        EXPECT_EQ(GetJsonInt(picked, "pick"), Optional<int64_t>(kSetSeed + 1));
        EXPECT_EQ(OnDisk("edit").jobs[0].pick, Optional<int64_t>(kSetSeed + 1));

        pick["seed"] = 999;
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "artgen_no_variant");
        pick["job"] = "zzz";
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "artgen_no_set");
        pick["set"] = "zzz";
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "artgen_no_set");
        pick["seed"] = "1";
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "invalid_params");
        EXPECT_EQ(ErrorCode(Call("artgen_pick", Json::object())), "invalid_params");

        Json retry = Json::object();
        retry["set"] = "edit";
        retry["job"] = "b";
        EXPECT_EQ(GetJsonInt(Result(Call("artgen_retry", retry)), "retried"), Optional<int64_t>(1));
        EXPECT_EQ(StateOf(OnDisk("edit"), 1), ArtGenSetJobState::Pending);
        EXPECT_EQ(StateOf(OnDisk("edit"), 2), ArtGenSetJobState::Cancelled);
        retry.erase("job");
        EXPECT_EQ(GetJsonInt(Result(Call("artgen_retry", retry)), "retried"), Optional<int64_t>(1)) << "b is already waiting";
        EXPECT_EQ(GetJsonInt(Result(Call("artgen_retry", retry)), "retried"), Optional<int64_t>(0));

        retry["job"] = "zzz";
        EXPECT_EQ(ErrorCode(Call("artgen_retry", retry)), "artgen_no_set");
        retry["job"] = 4;
        EXPECT_EQ(ErrorCode(Call("artgen_retry", retry)), "invalid_params");
        retry["set"] = "zzz";
        retry.erase("job");
        EXPECT_EQ(ErrorCode(Call("artgen_retry", retry)), "artgen_no_set");
        EXPECT_EQ(ErrorCode(Call("artgen_retry", Json::object())), "invalid_params");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenCancelNamesAJobOrASet) {
        ASSERT_TRUE(Enqueue("one", "a\nb\nc").has_value());
        ASSERT_TRUE(Enqueue("two", "a\nx").has_value());
        const Json first = Poll();
        ASSERT_NE(RequestOf(first), nullptr);

        // Both sets have an "a" in work (one running, one waiting): the name alone is ambiguous
        Json ambiguous = Json::object();
        ambiguous["job"] = "a";
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", ambiguous)), "invalid_params");

        Json alone = Json::object();
        alone["job"] = "c";
        const Json cancelled = Result(Call("artgen_cancel", alone));
        EXPECT_EQ(GetJsonString(cancelled, "set"), Optional<String>("one"));
        EXPECT_EQ(GetJsonString(cancelled, "state"), Optional<String>("Cancelled"));
        EXPECT_EQ(StateOf(OnDisk("one"), 2), ArtGenSetJobState::Cancelled);
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", alone)), "artgen_no_set") << "no longer in work";

        Json running = Json::object();
        running["job"] = "a";
        running["set"] = "one";
        EXPECT_EQ(GetJsonString(Result(Call("artgen_cancel", running)), "state"), Optional<String>("Running")) << "the service ends it";
        EXPECT_EQ(GetJsonInt(Poll(), "cancel"), GetJsonInt(*RequestOf(first), "id"));
        Finish(*RequestOf(first), "cancelled");
        EXPECT_EQ(StateOf(OnDisk("one"), 0), ArtGenSetJobState::Cancelled);

        running["set"] = "nothing";
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", running)), "artgen_no_set");
        running["set"] = "two";
        running["job"] = "nothing";
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", running)), "artgen_no_set");

        Json whole = Json::object();
        whole["set"] = "one";
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", whole)), "invalid_params") << "a set needs all";
        whole["all"] = false;
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", whole)), "invalid_params");
        whole["all"] = "yes";
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", whole)), "invalid_params");
        whole["all"] = true;
        whole["job"] = "b";
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", whole)), "invalid_params") << "a job and all";

        // The end of "a" left "b" of the first set as the next job; cancelling the other set leaves it running
        const Json next = Poll();
        ASSERT_NE(RequestOf(next), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(next), "outDir"), Optional<String>("one/b"));
        Json all = Json::object();
        all["set"] = "two";
        all["all"] = true;
        EXPECT_EQ(GetJsonString(Result(Call("artgen_cancel", all)), "state"), Optional<String>("Complete"));
        EXPECT_EQ(CountJobs(OnDisk("two"), ArtGenSetJobState::Cancelled), 2);
        all["set"] = "nothing";
        EXPECT_EQ(ErrorCode(Call("artgen_cancel", all)), "artgen_no_set");

        // Without params only the run in work is meant, as before
        EXPECT_EQ(ErrorCode(Call("artgen_cancel")), "");
        Finish(*RequestOf(next), "cancelled");
        EXPECT_EQ(ErrorCode(Call("artgen_cancel")), "artgen_no_run");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenStateSummarisesTheSets) {
        ASSERT_TRUE(Enqueue("older", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "boom");
        session.CancelJob("older", "c");
        ASSERT_EQ(session.PickVariant("older", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_TRUE(Enqueue("active", "x\ny").has_value());
        ASSERT_NE(RequestOf(Poll()), nullptr);

        const Json state = Result(Call("artgen_state"));
        const Json* sets = FindJsonMember(state, "sets");
        ASSERT_NE(sets, nullptr);
        ASSERT_EQ(sets->size(), 2u);
        const Json& older = (*sets)[0];
        EXPECT_EQ(GetJsonString(older, "name"), Optional<String>("older"));
        EXPECT_EQ(GetJsonString(older, "preset"), Optional<String>("wood"));
        EXPECT_EQ(GetJsonString(older, "kind"), Optional<String>("image"));
        EXPECT_EQ(GetJsonString(older, "state"), Optional<String>("Complete"));
        EXPECT_EQ(GetJsonInt(older, "count"), Optional<int64_t>(2));
        EXPECT_EQ(GetJsonInt(older, "seed"), Optional<int64_t>(kSetSeed));
        EXPECT_EQ(GetJsonInt(older, "picked"), Optional<int64_t>(1));
        EXPECT_EQ(GetJsonInt(older, "accepted"), Optional<int64_t>(0));
        const Json* olderJobs = FindJsonMember(older, "jobs");
        ASSERT_NE(olderJobs, nullptr);
        EXPECT_EQ(GetJsonInt(*olderJobs, "done"), Optional<int64_t>(1));
        EXPECT_EQ(GetJsonInt(*olderJobs, "failed"), Optional<int64_t>(1));
        EXPECT_EQ(GetJsonInt(*olderJobs, "cancelled"), Optional<int64_t>(1));
        EXPECT_EQ(GetJsonInt(*olderJobs, "pending"), Optional<int64_t>(0));
        const Json& active = (*sets)[1];
        EXPECT_EQ(GetJsonString(active, "state"), Optional<String>("Active"));
        const Json* activeJobs = FindJsonMember(active, "jobs");
        ASSERT_NE(activeJobs, nullptr);
        EXPECT_EQ(GetJsonInt(*activeJobs, "running"), Optional<int64_t>(1));
        EXPECT_EQ(GetJsonInt(*activeJobs, "pending"), Optional<int64_t>(1));
    }

    TEST_F(ArtGenSetSessionTest, ASetupActionIsRefusedWhileASetIsActive) {
        ASSERT_TRUE(Enqueue("hold", "a").has_value());
        Json setup = Json::object();
        setup["action"] = "Plan";
        EXPECT_EQ(ErrorCode(Call("artgen_setup", setup)), "artgen_busy");
    }

}  // namespace BECore::Tests
