#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <filesystem>
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

}  // namespace BECore::Tests
