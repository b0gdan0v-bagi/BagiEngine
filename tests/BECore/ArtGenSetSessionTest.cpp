#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
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
            Json knock = Json::object();
            knock["name"] = "knock";
            knock["title"] = "knock";
            knock["kind"] = "audio";
            Json params = Json::object();
            params["state"] = "ready";
            params["generator"] = "fake";
            params["presets"] = Json::array({preset, knock});
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

        /// What the fake acceptors were called with, in order (a function pointer cannot capture, so the test reads it here)
        struct AcceptedRow {
            int64_t seed = -1;
            String path;
            String provenance;
            String name;
            bool image = true;
            bool withNode = false;
            bool defaultSettings = true;
        };
        Vector<AcceptedRow> gAccepted;
        String gFailOn;  // a row of this name is refused by the fake acceptors

        std::expected<Json, ControlError> Answer(const ArtGenVariant& variant, StringView name, bool image, bool withNode, bool defaultSettings) {
            if (name == gFailOn) {
                return std::unexpected(ControlError{String("artgen_accept_failed"), String("the disk is full")});
            }
            gAccepted.push_back(AcceptedRow{variant.seed, variant.path, variant.provenancePath, String(name), image, withNode, defaultSettings});
            Json reply = Json::object();
            reply[image ? "sprite" : "clip"] = ToStdString(name);
            return reply;
        }

        std::expected<Json, ControlError> FakeAcceptor(const ArtGenVariant& variant, StringView sprite, SceneNode* node) {
            return Answer(variant, sprite, true, node != nullptr, true);
        }

        std::expected<Json, ControlError> FakeSoundAcceptor(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* node) {
            return Answer(variant, target.name, false, node != nullptr, settings == ArtGenSoundSettings{} && target.clip.empty());
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
            session.SetAcceptor(&FakeAcceptor);
            session.SetSoundAcceptor(&FakeSoundAcceptor);
            gAccepted.clear();
            gFailOn.clear();
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
        void Finish(const Json& request, StringView status, StringView error = {}, StringView kind = "image") {
            const uint32_t id = static_cast<uint32_t>(GetJsonInt(request, "id").value());
            const int64_t seed = GetJsonInt(request, "seed").value();
            const String outDir = GetJsonString(request, "outDir").value();
            const String name = GetJsonString(request, "name").value();
            const StringView extension = kind == "audio" ? "flac" : "png";
            if (status == "ok") {
                const int64_t count = GetJsonInt(request, "count").value();
                for (int64_t i = 0; i < count; ++i) {
                    Json image = Report(id);
                    image["seed"] = seed + i;
                    image["kind"] = ToStdString(kind);
                    image["path"] = ToStdString(Format("{}/artgen/out/{}/{}_{}.{}", PathText(_root), outDir, name, seed + i, extension));
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

        /// Runs every job of the set to its end (the service made all the images) and picks the second variant of each
        void RunAndPickAll(StringView set, StringView kind = "image") {
            while (const Json* request = RequestOf(pollReply = Poll())) {
                Finish(*request, "ok", {}, kind);
            }
            for (const ArtGenSetJob& job : session.FindSet(set)->jobs) {
                ASSERT_EQ(session.PickVariant(set, job.name, kSetSeed + 1), ArtGenSetEdit::Ok);
            }
        }

        /// Makes the files the manifest names, as the service would have written them (the reports alone leave none)
        void MakeFiles(StringView set) {
            for (const ArtGenSetJob& job : session.FindSet(set)->jobs) {
                for (const ArtGenSetVariant& variant : job.variants) {
                    for (const String& file : {variant.file, variant.provenance}) {
                        const std::filesystem::path path = AbsoluteInSet(_root, set, file);
                        std::filesystem::create_directories(path.parent_path());
                        ASSERT_TRUE(WriteTextFileAtomically(path, "x").has_value());
                    }
                }
            }
        }

        /// Runs every job of the set to its end and leaves every row unpicked
        void RunAll(StringView kind = "image") {
            while (const Json* request = RequestOf(pollReply = Poll())) {
                Finish(*request, "ok", {}, kind);
            }
        }

        /// What a restart does to the sets: they are read from the disk again
        void ReloadFromDisk() {
            const String root = session.GetGameRoot();
            session.SetGameRoot("");
            session.SetGameRoot(root);
        }

        /// Marks a row as kept in the manifest on disk and reads it back, as the owner's Keep will (the session has no Keep of its own yet)
        void KeepOnDisk(StringView set, StringView job) {
            ArtGenSet disk = OnDisk(set);
            ArtGenSetJob* row = FindJob(disk, job);
            ASSERT_NE(row, nullptr);
            ASSERT_TRUE(row->accepted.has_value());
            row->accepted->kept = true;
            ASSERT_TRUE(WriteManifest(_root, disk).has_value());
            ReloadFromDisk();
        }

        Json AcceptAllCall(StringView set) {
            Json params = Json::object();
            params["set"] = ToStdString(set);
            return Call("artgen_accept_set", params);
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

        Json pollReply;
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

    TEST_F(ArtGenSetSessionTest, AcceptAllTakesThePickedVariantOfEveryJobUnderTheJobName) {
        ASSERT_TRUE(Enqueue("pieces", "rook: a rook\nknight: a knight\nbishop: a bishop").has_value());
        RunAndPickAll("pieces");
        ASSERT_EQ(session.PickVariant("pieces", "knight", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("pieces"));

        Json params = Json::object();
        params["set"] = "pieces";
        const Json answer = Result(Call("artgen_accept_set", params));
        EXPECT_EQ(GetJsonString(answer, "set"), Optional<String>("pieces"));
        const Json* accepted = FindJsonMember(answer, "accepted");
        ASSERT_NE(accepted, nullptr);
        ASSERT_EQ(accepted->size(), 3u);
        EXPECT_EQ(GetJsonString((*accepted)[1], "job"), Optional<String>("knight"));
        EXPECT_EQ(GetJsonString((*accepted)[1], "as"), Optional<String>("knight"));
        EXPECT_EQ(GetJsonString((*accepted)[1], "sprite"), Optional<String>("knight")) << "the acceptor's answer is part of the row";
        EXPECT_EQ(GetJsonInt((*accepted)[1], "seed"), Optional<int64_t>(kSetSeed));
        EXPECT_EQ(GetJsonInt((*accepted)[1], "alternatives"), Optional<int64_t>(1));

        ASSERT_EQ(gAccepted.size(), 3u);
        const char* const names[] = {"rook", "knight", "bishop"};
        const int64_t seeds[] = {kSetSeed + 1, kSetSeed, kSetSeed + 1};
        for (size_t row = 0; row < 3; ++row) {
            EXPECT_EQ(gAccepted[row].name, names[row]);
            EXPECT_EQ(gAccepted[row].seed, seeds[row]);
            EXPECT_EQ(gAccepted[row].path, Format("{}/artgen/out/pieces/{}/{}_{}.png", PathText(_root), names[row], names[row], seeds[row]));
            EXPECT_EQ(gAccepted[row].provenance, Format("{}/artgen/out/pieces/{}/{}_{}.artgen.json", PathText(_root), names[row], names[row], seeds[row]));
            EXPECT_TRUE(gAccepted[row].image);
            EXPECT_FALSE(gAccepted[row].withNode);
        }

        const ArtGenSet disk = OnDisk("pieces");
        for (size_t row = 0; row < 3; ++row) {
            ASSERT_TRUE(disk.jobs[row].accepted.has_value());
            EXPECT_EQ(disk.jobs[row].accepted->as, names[row]);
            EXPECT_FALSE(disk.jobs[row].accepted->date.empty());
            EXPECT_FALSE(disk.jobs[row].accepted->kept) << "a written row is live";
        }
        const Json state = Result(Call("artgen_state"));
        EXPECT_EQ(GetJsonInt((*FindJsonMember(state, "sets"))[0], "accepted"), Optional<int64_t>(3));
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllRefusesASetThatIsNotReadyAndChangesNothing) {
        ASSERT_TRUE(Enqueue("gaps", "a\nb\nc\nd").has_value());
        Json params = Json::object();
        params["set"] = "gaps";
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "artgen_busy") << "the set is still active";

        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "boom");
        session.CancelJob("gaps", "d");
        ASSERT_EQ(session.PickVariant("gaps", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("gaps"));

        const Json refused = Call("artgen_accept_set", params);
        EXPECT_EQ(ErrorCode(refused), "artgen_set_incomplete");
        const String message = GetJsonString(*FindJsonMember(refused, "error"), "message").value_or(String());
        EXPECT_EQ(message.find("not picked"), String::npos) << "an image row with no pick takes its first variant: " << message.c_str();
        EXPECT_NE(message.find("failed: c"), String::npos) << message.c_str();
        EXPECT_NE(message.find("cancelled: d"), String::npos) << message.c_str();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_EQ(CountJobs(OnDisk("gaps"), ArtGenSetJobState::Done), 2);
        EXPECT_FALSE(OnDisk("gaps").jobs[0].accepted.has_value());

        params["set"] = "nothing";
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "artgen_no_set");
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", Json::object())), "invalid_params");
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllChecksEveryRowsFilesBeforeTheFirstAccept) {
        ASSERT_TRUE(Enqueue("files", "a\nb\nc").has_value());
        RunAndPickAll("files");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("files"));
        Json params = Json::object();
        params["set"] = "files";

        // The picked file of the last row is gone: the first two are not accepted either
        std::filesystem::remove(AbsoluteInSet(_root, "files", "c/c_41.png"));
        Json refused = Call("artgen_accept_set", params);
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = GetJsonString(*FindJsonMember(refused, "error"), "message").value_or(String());
        EXPECT_NE(message.find("'c'"), String::npos) << message.c_str();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_FALSE(OnDisk("files").jobs[0].accepted.has_value());

        // The provenance of a row is gone
        ASSERT_TRUE(WriteTextFileAtomically(AbsoluteInSet(_root, "files", "c/c_41.png"), "x").has_value());
        std::filesystem::remove(AbsoluteInSet(_root, "files", "a/a_41.artgen.json"));
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "artgen_accept_failed");
        EXPECT_TRUE(gAccepted.empty());

        // No acceptor here
        ASSERT_TRUE(WriteTextFileAtomically(AbsoluteInSet(_root, "files", "a/a_41.artgen.json"), "x").has_value());
        session.SetAcceptor(nullptr);
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "artgen_accept_failed");
        session.SetAcceptor(&FakeAcceptor);

        // Everything in place: the same call goes through
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "") << "everything is in place now";
        EXPECT_EQ(gAccepted.size(), 3u);
    }

    TEST_F(ArtGenSetSessionTest, AFailureHalfwayNamesTheRowAndTheNextCallFinishesTheSet) {
        ASSERT_TRUE(Enqueue("five", "a\nb\nc\nd\ne").has_value());
        RunAndPickAll("five");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("five"));
        Json params = Json::object();
        params["set"] = "five";

        gFailOn = "c";
        const Json refused = Call("artgen_accept_set", params);
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = GetJsonString(*FindJsonMember(refused, "error"), "message").value_or(String());
        EXPECT_NE(message.find("'c'"), String::npos) << message.c_str();
        EXPECT_NE(message.find("row 3 of 5"), String::npos) << message.c_str();
        EXPECT_NE(message.find("the disk is full"), String::npos) << message.c_str();
        EXPECT_NE(message.find("accepted before it: a, b"), String::npos) << message.c_str();
        ASSERT_EQ(gAccepted.size(), 2u);
        {
            const ArtGenSet disk = OnDisk("five");
            EXPECT_TRUE(disk.jobs[0].accepted.has_value());
            EXPECT_TRUE(disk.jobs[1].accepted.has_value());
            EXPECT_FALSE(disk.jobs[2].accepted.has_value());
            EXPECT_FALSE(disk.jobs[3].accepted.has_value());
        }

        gFailOn.clear();
        const Json again = Result(Call("artgen_accept_set", params));
        ASSERT_NE(FindJsonMember(again, "accepted"), nullptr);
        EXPECT_EQ(FindJsonMember(again, "accepted")->size(), 5u);
        ASSERT_EQ(gAccepted.size(), 7u) << "the first two replace themselves";
        EXPECT_EQ(gAccepted[2].name, "a");
        EXPECT_EQ(gAccepted[6].name, "e");
        for (const ArtGenSetJob& job : OnDisk("five").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfRowsWithNoPickTakesTheFirstVariantAndRecordsItAsThePick) {
        ASSERT_TRUE(Enqueue("firsts", "a\nb").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_EQ(session.PickVariant("firsts", "b", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("firsts"));

        const Json answer = Result(AcceptAllCall("firsts"));
        ASSERT_NE(FindJsonMember(answer, "accepted"), nullptr) << "the answer: " << DumpJson(answer).c_str();
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].seed, kSetSeed) << "a has no pick: its first variant";
        EXPECT_EQ(gAccepted[1].seed, kSetSeed + 1) << "b keeps its pick";
        const ArtGenSet disk = OnDisk("firsts");
        EXPECT_EQ(disk.jobs[0].pick, Optional<int64_t>(kSetSeed));
        EXPECT_EQ(disk.jobs[1].pick, Optional<int64_t>(kSetSeed + 1));
        EXPECT_TRUE(IsLive(disk.jobs[0]));
        EXPECT_EQ(GetJsonInt((*FindJsonMember(answer, "accepted"))[0], "seed"), Optional<int64_t>(kSetSeed));
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfASoundSetStillNeedsAPickOnEveryRow) {
        Poll();
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = ParseArtGenSubjects("knock: a wooden knock\ntap: a soft tap").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "heard";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll("audio"));
        ASSERT_EQ(session.PickVariant("heard", "knock", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("heard"));

        const Json refused = AcceptAllCall("heard");
        EXPECT_EQ(ErrorCode(refused), "artgen_set_incomplete");
        const String message = GetJsonString(*FindJsonMember(refused, "error"), "message").value_or(String());
        EXPECT_NE(message.find("not picked: tap"), String::npos) << message.c_str();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_FALSE(OnDisk("heard").jobs[1].pick.has_value()) << "nothing was picked for the person";
        EXPECT_FALSE(OnDisk("heard").jobs[0].accepted.has_value());
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllSkipsAKeptRowAndWritesTheOthers) {
        ASSERT_TRUE(Enqueue("mixed", "a\nb\nc").has_value());
        RunAndPickAll("mixed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("mixed"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("mixed")), "");
        ASSERT_EQ(gAccepted.size(), 3u);
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("mixed", "b"));

        gAccepted.clear();
        const Json answer = Result(AcceptAllCall("mixed"));
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].name, "a");
        EXPECT_EQ(gAccepted[1].name, "c");
        const Json& rows = *FindJsonMember(answer, "accepted");
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(GetJsonString(rows[1], "job"), Optional<String>("b"));
        EXPECT_EQ(GetJsonBool(rows[1], "kept"), Optional<bool>(true));
        EXPECT_EQ(FindJsonMember(rows[1], "as"), nullptr) << "nothing was written for it";
        EXPECT_TRUE(OnDisk("mixed").jobs[1].accepted->kept);
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfASetWithEveryRowKeptNeedsNoAcceptor) {
        ASSERT_TRUE(Enqueue("done", "a").has_value());
        RunAndPickAll("done");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("done"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("done")), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("done", "a"));
        session.SetAcceptor(nullptr);
        gAccepted.clear();
        EXPECT_EQ(ErrorCode(AcceptAllCall("done")), "");
        EXPECT_TRUE(gAccepted.empty());
    }

    TEST_F(ArtGenSetSessionTest, APickOfALiveRowSwitchesTheGameToThatVariant) {
        ASSERT_TRUE(Enqueue("live", "a\nb").has_value());
        RunAndPickAll("live");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("live"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("live")), "");
        gAccepted.clear();

        Json pick = Json::object();
        pick["set"] = "live";
        pick["job"] = "a";
        pick["seed"] = kSetSeed;
        const Json reply = Result(Call("artgen_pick", pick));
        EXPECT_EQ(GetJsonInt(reply, "pick"), Optional<int64_t>(kSetSeed));
        ASSERT_NE(FindJsonMember(reply, "switched"), nullptr) << DumpJson(reply).c_str();
        EXPECT_EQ(GetJsonString(*FindJsonMember(reply, "switched"), "sprite"), Optional<String>("a"));

        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(gAccepted[0].name, "a") << "the same name: the file is replaced";
        EXPECT_EQ(gAccepted[0].seed, kSetSeed);
        EXPECT_EQ(gAccepted[0].path, Format("{}/artgen/out/live/a/a_{}.png", PathText(_root), kSetSeed));
        const ArtGenSet disk = OnDisk("live");
        EXPECT_EQ(disk.jobs[0].pick, Optional<int64_t>(kSetSeed));
        EXPECT_EQ(disk.jobs[0].accepted->as, "a");
        EXPECT_FALSE(disk.jobs[0].accepted->kept);
        EXPECT_EQ(disk.jobs[1].pick, Optional<int64_t>(kSetSeed + 1)) << "the other row is untouched";

        gAccepted.clear();
        EXPECT_EQ(session.PickVariant("live", "a", kSetSeed), ArtGenSetEdit::Ok);
        EXPECT_TRUE(gAccepted.empty()) << "the pick the game already holds is not written again";
        EXPECT_EQ(FindJsonMember(Result(Call("artgen_pick", pick)), "switched"), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, APickOfARowThatIsNotAcceptedWritesNothing) {
        ASSERT_TRUE(Enqueue("plain", "a").has_value());
        RunAndPickAll("plain");
        gAccepted.clear();
        EXPECT_EQ(session.PickVariant("plain", "a", kSetSeed), ArtGenSetEdit::Ok);
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_EQ(OnDisk("plain").jobs[0].pick, Optional<int64_t>(kSetSeed));
    }

    TEST_F(ArtGenSetSessionTest, ASwitchToASeedTheJobLacksIsNoVariantAndWritesNothing) {
        ASSERT_TRUE(Enqueue("lack", "a").has_value());
        RunAndPickAll("lack");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("lack"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("lack")), "");
        gAccepted.clear();

        Json pick = Json::object();
        pick["set"] = "lack";
        pick["job"] = "a";
        pick["seed"] = 999;
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "artgen_no_variant");
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_EQ(OnDisk("lack").jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
    }

    TEST_F(ArtGenSetSessionTest, AFailedSwitchLeavesThePickAndTheDateAsTheyWere) {
        ASSERT_TRUE(Enqueue("fail", "a").has_value());
        RunAndPickAll("fail");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("fail"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("fail")), "");
        const ArtGenSet before = OnDisk("fail");
        gAccepted.clear();

        Json pick = Json::object();
        pick["set"] = "fail";
        pick["job"] = "a";
        pick["seed"] = kSetSeed;

        gFailOn = "a";
        ArtGenSwitch write;
        EXPECT_EQ(session.PickVariant("fail", "a", kSetSeed, &write), ArtGenSetEdit::WriteFailed);
        EXPECT_NE(write.error.find("the disk is full"), String::npos) << write.error.c_str();
        const Json refused = Call("artgen_pick", pick);
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        EXPECT_EQ(ArtGenSetToJson(OnDisk("fail")), ArtGenSetToJson(before));
        EXPECT_EQ(session.FindSet("fail")->jobs[0].pick, Optional<int64_t>(kSetSeed + 1));

        gFailOn.clear();
        std::filesystem::remove(AbsoluteInSet(_root, "fail", "a/a_40.png"));
        EXPECT_EQ(session.PickVariant("fail", "a", kSetSeed, &write), ArtGenSetEdit::WriteFailed);
        EXPECT_NE(write.error.find("is gone"), String::npos) << write.error.c_str();
        EXPECT_TRUE(gAccepted.empty()) << "the files are checked before the acceptor is asked";

        session.SetAcceptor(nullptr);
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "artgen_accept_failed");
        EXPECT_EQ(session.FindSet("fail")->jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
    }

    TEST_F(ArtGenSetSessionTest, ALiveRowStillSwitchesAfterARestart) {
        ASSERT_TRUE(Enqueue("again", "a").has_value());
        RunAndPickAll("again");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("again"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("again")), "");
        ReloadFromDisk();
        gAccepted.clear();

        EXPECT_EQ(session.PickVariant("again", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(gAccepted[0].name, "a");
        EXPECT_EQ(OnDisk("again").jobs[0].pick, Optional<int64_t>(kSetSeed));
    }

    TEST_F(ArtGenSetSessionTest, ASoundRowSwitchesThroughTheSoundAcceptor) {
        Poll();
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = ParseArtGenSubjects("knock: a wooden knock").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "clips";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("clips", "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("clips"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("clips")), "");
        gAccepted.clear();

        EXPECT_EQ(session.PickVariant("clips", "knock", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_FALSE(gAccepted[0].image);
        EXPECT_EQ(gAccepted[0].name, "knock");
        EXPECT_EQ(gAccepted[0].seed, kSetSeed);
    }

    TEST_F(ArtGenSetSessionTest, ASoundSetIsAcceptedAsClipsWithDefaultSettings) {
        Poll();  // the catalog tells the kind of the preset
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = ParseArtGenSubjects("knock: a wooden knock\ntap: a soft tap").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "sounds";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        EXPECT_EQ(session.FindSet("sounds")->kind, "audio");
        RunAndPickAll("sounds", "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("sounds"));

        Json params = Json::object();
        params["set"] = "sounds";
        const Json answer = Result(Call("artgen_accept_set", params));
        ASSERT_NE(FindJsonMember(answer, "accepted"), nullptr);
        EXPECT_EQ(GetJsonString((*FindJsonMember(answer, "accepted"))[0], "clip"), Optional<String>("knock"));
        ASSERT_EQ(gAccepted.size(), 2u);
        for (const AcceptedRow& row : gAccepted) {
            EXPECT_FALSE(row.image);
            EXPECT_TRUE(row.defaultSettings) << "the clip is assets/artgen/<name>.wav, processed with the defaults";
            EXPECT_NE(row.path.find(".flac"), String::npos);
        }
        EXPECT_EQ(gAccepted[1].name, "tap");
        EXPECT_TRUE(OnDisk("sounds").jobs[1].accepted.has_value());
    }

    TEST_F(ArtGenSetSessionTest, ASoundSetQueuedBeforeTheFirstPollIsAcceptedAsClips) {
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = ParseArtGenSubjects("knock: a wooden knock\ntap: a soft tap").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "early";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        EXPECT_EQ(session.FindSet("early")->kind, "image") << "the catalog is empty: only a guess";

        RunAndPickAll("early", "audio");
        EXPECT_EQ(session.FindSet("early")->kind, "audio") << "the first poll brought the catalog, and the job went out with its kind";
        EXPECT_EQ(OnDisk("early").kind, "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("early"));

        Json params = Json::object();
        params["set"] = "early";
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "");
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_FALSE(gAccepted[0].image);
    }

    TEST_F(ArtGenSetSessionTest, TheKindOfASetTheCatalogDoesNotKnowComesFromItsVariants) {
        ArtGenSetRequest request;
        request.preset = "not_in_the_catalog";
        request.subjects = ParseArtGenSubjects("knock: a wooden knock").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "unlisted";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());

        RunAndPickAll("unlisted", "audio");
        EXPECT_EQ(session.FindSet("unlisted")->kind, "audio");
        EXPECT_EQ(OnDisk("unlisted").kind, "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("unlisted"));

        Json params = Json::object();
        params["set"] = "unlisted";
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "");
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_FALSE(gAccepted[0].image);
    }

}  // namespace BECore::Tests
