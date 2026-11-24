#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/File/File.h>
#include <Common/ControlTestSupport.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kFrameGuard = 100;
        constexpr int64_t kSetSeed = 40;

        /// The manifest `WriteArtGenSet` writes, as a DOM to compare with the answers the oracle `Call` parses
        Json ManifestJson(const ArtGenSet& set) {
            String text;
            JsonWriter writer(text);
            WriteArtGenSet(writer, set);
            return Require(ParseJson(text));
        }

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

        /// The calls of the batch hooks, the acceptors and the packer in the order they came: "begin", "accept <name>", "pack <group>", "end"
        Vector<String> gEvents;
        bool gEndFails = false;               // the fake end of the batch answers that the library was not saved
        MoveOnlyFunction<void()> gDuringEnd;  // what a test looks at while the fake end runs: the library is saved there, the rows are not marked yet

        void FakeBegin() {
            gEvents.push_back("begin");
        }

        std::expected<void, ControlError> FakeEnd() {
            gEvents.push_back("end");
            if (gDuringEnd) {
                gDuringEnd();
            }
            if (gEndFails) {
                return std::unexpected(ControlError{String("artgen_accept_failed"), String("TextureLibrary.xml could not be saved")});
            }
            return {};
        }

        std::expected<ArtGenAcceptAnswer, ControlError> Answer(const ArtGenVariant& variant, StringView name, bool image, bool withNode, bool defaultSettings) {
            if (name == gFailOn) {
                return std::unexpected(ControlError{String("artgen_accept_failed"), String("the disk is full")});
            }
            gAccepted.push_back(AcceptedRow{variant.seed, variant.path, variant.provenancePath, String(name), image, withNode, defaultSettings});
            gEvents.push_back(Format("accept {}", name));
            ArtGenAcceptAnswer reply;
            (image ? reply.sprite : reply.clip) = String(name);
            return reply;
        }

        /// The member names of a JSON object, sorted: the parsed DOM sorts them, and the generated writer writes them in the same order by design
        Vector<String> KeysOf(const Json& object) {
            Vector<String> keys;
            for (const auto& [key, value] : object.items()) {
                keys.push_back(String(key.c_str()));
            }
            return keys;
        }

        struct KeptRow {
            String kind;
            String name;
            int64_t seed = -1;
            String date;
        };
        Vector<KeptRow> gKept;
        String gKeepFailOn;  // a row of this name is refused by the fake keeper

        std::expected<void, ControlError> FakeKeeper(StringView kind, StringView name, int64_t seed, StringView date) {
            if (name == gKeepFailOn) {
                return std::unexpected(ControlError{String("artgen_accept_failed"), String("the provenance is read-only")});
            }
            gKept.push_back(KeptRow{String(kind), String(name), seed, String(date)});
            return {};
        }

        /// The plan of each call of the fake image acceptor that succeeded, in order, beside gAccepted
        Vector<ArtGenImagePlan> gPlans;

        std::expected<ArtGenAcceptAnswer, ControlError> FakeAcceptor(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan, SceneNode* node) {
            auto answer = Answer(variant, sprite, true, node != nullptr, true);
            if (answer) {
                gPlans.push_back(plan);
            }
            return answer;
        }

        /// The group of each call of the fake packer, and how many rows the fake acceptor had written when it was called
        struct PackCall {
            String group;
            size_t acceptedBefore = 0;
        };
        Vector<PackCall> gPacks;
        String gPackFail;  // the fake packer fails with this text when it is not empty

        std::expected<ArtGenAtlasAnswer, ControlError> FakePacker(StringView group) {
            gPacks.push_back(PackCall{String(group), gAccepted.size()});
            gEvents.push_back(Format("pack {}", group));
            if (!gPackFail.empty()) {
                return std::unexpected(ControlError{String("artgen_accept_failed"), gPackFail});
            }
            ArtGenAtlasAnswer atlas;
            atlas.name = String(group);
            atlas.members = gAccepted.size();
            return atlas;
        }

        struct MeasureCall {
            Vector<String> files;
            ArtGenImageSettings settings;
        };
        Vector<MeasureCall> gMeasures;
        String gMeasureFail;  // the fake measurer fails with this text when it is not empty
        constexpr ArtGenImageFrame kFakeFrame{10, 20, 300, 400, 150, 200};

        std::expected<ArtGenImageFrame, String> FakeMeasurer(const Vector<String>& files, const ArtGenImageSettings& settings) {
            gMeasures.push_back(MeasureCall{files, settings});
            if (!gMeasureFail.empty()) {
                return std::unexpected(gMeasureFail);
            }
            return kFakeFrame;
        }

        std::expected<ArtGenAcceptAnswer, ControlError> FakeSoundAcceptor(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* node) {
            return Answer(variant, target.name, false, node != nullptr, settings == ArtGenSoundSettings{} && target.clip.empty());
        }

    }  // namespace

    class ArtGenSetSessionTest : public ::testing::Test {
    protected:
        ArtGenSetSessionTest() : session([this] { return now; }) {}

        void SetUp() override {
            _root = TestTempDirectory("artgen_set_session");
            MakeEmptyDirectory(_root);
            session.SetGameRoot(_root.View());
            session.RegisterControl(service);
            session.SetAcceptor(&FakeAcceptor);
            session.SetSoundAcceptor(&FakeSoundAcceptor);
            session.SetKeeper(&FakeKeeper);
            session.SetFrameMeasurer(&FakeMeasurer);
            session.SetAtlasPacker(&FakePacker);
            gPacks.clear();
            gPackFail.clear();
            gAccepted.clear();
            gPlans.clear();
            gMeasures.clear();
            gMeasureFail.clear();
            gFailOn.clear();
            gEvents.clear();
            gEndFails = false;
            gDuringEnd = nullptr;
            gKept.clear();
            gKeepFailOn.clear();
        }

        void TearDown() override {
            RemoveDirectory(_root);
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
            request.subjects = Require(ParseArtGenSubjects(lines));
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
            const uint32_t id = static_cast<uint32_t>(Require(GetJsonInt(request, "id")));
            const int64_t seed = Require(GetJsonInt(request, "seed"));
            const String outDir = Require(GetJsonString(request, "outDir"));
            const String name = Require(GetJsonString(request, "name"));
            const StringView extension = kind == "audio" ? "flac" : "png";
            if (status == "ok") {
                const int64_t count = Require(GetJsonInt(request, "count"));
                for (int64_t i = 0; i < count; ++i) {
                    Json image = Report(id);
                    image["seed"] = seed + i;
                    image["kind"] = ToStdString(kind);
                    image["path"] = ToStdString(Format("{}/artgen/out/{}/{}_{}.{}", _root.View(), outDir, name, seed + i, extension));
                    image["provenance"] = ToStdString(Format("{}/artgen/out/{}/{}_{}.artgen.json", _root.View(), outDir, name, seed + i));
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

        /// What the service does when it makes @p images images for a request that asked for fewer (extra images of a regenerating run): seeds from the request's, then the end
        void FinishWithImages(const Json& request, int64_t images) {
            const uint32_t id = static_cast<uint32_t>(GetJsonInt(request, "id").value());
            const int64_t seed = GetJsonInt(request, "seed").value();
            const String outDir = GetJsonString(request, "outDir").value();
            const String name = GetJsonString(request, "name").value();
            for (int64_t i = 0; i < images; ++i) {
                Json image = Report(id);
                image["seed"] = seed + i;
                image["path"] = ToStdString(Format("{}/artgen/out/{}/{}_{}.png", _root.View(), outDir, name, seed + i));
                image["provenance"] = ToStdString(Format("{}/artgen/out/{}/{}_{}.artgen.json", _root.View(), outDir, name, seed + i));
                Call("artgen_image", image);
            }
            Json done = Report(id);
            done["status"] = "ok";
            Call("artgen_done", done);
        }

        /// Runs every job of the set to its end (the service made all the images) and picks the second variant of each
        void RunAndPickAll(StringView set, StringView kind = "image") {
            while (const Json* request = RequestOf(pollReply = Poll())) {
                Finish(*request, "ok", {}, kind);
            }
            for (const ArtGenSetJob& job : Require(session.FindSet(set)).jobs) {
                ASSERT_EQ(session.PickVariant(set, job.name, kSetSeed + 1), ArtGenSetEdit::Ok);
            }
        }

        /// Makes the files the manifest names, as the service would have written them (the reports alone leave none)
        void MakeFiles(StringView set) {
            for (const ArtGenSetJob& job : Require(session.FindSet(set)).jobs) {
                for (const ArtGenSetVariant& variant : job.variants) {
                    for (const String& file : {variant.file, variant.provenance}) {
                        const Path path = AbsoluteInSet(_root, set, file);
                        MakeDirectory(path.Parent());
                        ASSERT_TRUE(WriteFileAtomically(path, "x").has_value());
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

        /// Marks a row as kept in the manifest on disk, without cutting its variants as Keep does (what a manifest from before Keep holds), and reads it back
        void KeepOnDisk(StringView set, StringView job) {
            ArtGenSet disk = OnDisk(set);
            ArtGenSetJob* row = FindJob(disk, job);
            ASSERT_NE(row, nullptr);
            ASSERT_TRUE(row->accepted.has_value());
            row->accepted->kept = true;
            ASSERT_TRUE(WriteManifest(_root, disk).has_value());
            ReloadFromDisk();
        }

        /// Two finished sets that both make 'a' (the first also 'b'), both accepted, the second after the first: the second holds the game's 'a'
        void AcceptTwoSetsOfOneName() {
            ASSERT_TRUE(Enqueue("first", "a\nb").has_value());
            ASSERT_TRUE(Enqueue("second", "a").has_value());
            ASSERT_NO_FATAL_FAILURE(RunAndPickAll("first"));
            ASSERT_EQ(session.PickVariant("second", "a", kSetSeed + 1), ArtGenSetEdit::Ok);
            ASSERT_NO_FATAL_FAILURE(MakeFiles("first"));
            ASSERT_NO_FATAL_FAILURE(MakeFiles("second"));
            ASSERT_EQ(ErrorCode(AcceptAllCall("first")), "");
            ASSERT_EQ(ErrorCode(AcceptAllCall("second")), "");
            gAccepted.clear();
            gKept.clear();
        }

        Json AcceptAllCall(StringView set) {
            Json params = Json::object();
            params["set"] = ToStdString(set);
            return Call("artgen_accept_set", params);
        }

        /// `artgen_more` of @p set with the members of @p extra (count, seed, jobs)
        Json MoreCall(StringView set, Json extra = Json::object()) {
            extra["set"] = ToStdString(set);
            return Call("artgen_more", extra);
        }

        /// `artgen_keep` of @p set, or of one job of it
        Json KeepCall(StringView set, StringView job = {}) {
            Json params = Json::object();
            params["set"] = ToStdString(set);
            if (!job.empty()) {
                params["job"] = ToStdString(job);
            }
            return Call("artgen_keep", params);
        }

        /// A finished set accepted into the game: every row picked on its second variant (seed kSetSeed + 1) and live, files made
        void FinishAndAccept(StringView set, StringView lines, int32_t count = 2) {
            ASSERT_TRUE(Enqueue(set, lines, count).has_value());
            ASSERT_NO_FATAL_FAILURE(RunAndPickAll(set));
            ASSERT_NO_FATAL_FAILURE(MakeFiles(set));
            ASSERT_EQ(ErrorCode(AcceptAllCall(set)), "");
            gAccepted.clear();
            gPlans.clear();
            gMeasures.clear();
        }

        /// The manifest as it is on disk now
        ArtGenSet OnDisk(StringView set) const {
            const auto text = ReadTextFile(ManifestPath(_root, set));
            const JsonDoc doc{StringView(Require(text))};
            return Require(ReadArtGenSet(doc));
        }

        static ArtGenSetJobState StateOf(const ArtGenSet& set, size_t job) {
            return set.jobs[job].state;
        }

        Json pollReply;
        double now = 100.0;
        ControlService service;
        ArtGenSession session;
        Path _root;

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
        EXPECT_EQ(CountJobs(Require(session.FindSet("pieces")), ArtGenSetJobState::Done), 3);
        EXPECT_EQ(RequestOf(Poll()), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, TheSetDrawsASharedSeedWhenNoneIsGiven) {
        ASSERT_TRUE(Enqueue("drawn", "a\nb", 1, Optional<int64_t>()).has_value());
        const ArtGenSet& set = Require(session.FindSet("drawn"));
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
        EXPECT_FALSE(IsActive(Require(session.FindSet("all"))));
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
        EXPECT_FALSE(IsActive(Require(session.FindSet("retry"))));
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
        EXPECT_EQ(StateOf(Require(session.FindSet("wait")), 0), ArtGenSetJobState::Pending);

        const Json first = Poll();
        ASSERT_NE(RequestOf(first), nullptr);
        // The service is silent past the timeout when the job ends: the next one waits for a poll
        now += ArtGenSession::kServiceTimeoutSeconds + 1.0;
        Finish(*RequestOf(first), "ok");
        EXPECT_EQ(StateOf(Require(session.FindSet("wait")), 1), ArtGenSetJobState::Pending);
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
        EXPECT_EQ(StateOf(Require(session.FindSet("bye")), 1), ArtGenSetJobState::Pending);
        const Json again = Poll();
        ASSERT_NE(RequestOf(again), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(again), "name"), Optional<String>("b"));
    }

    TEST_F(ArtGenSetSessionTest, ARestartShowsTheInterruptedJobsCancelled) {
        ASSERT_TRUE(Enqueue("crash", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        ASSERT_NE(RequestOf(Poll()), nullptr);

        ArtGenSession restarted([this] { return now; });
        restarted.SetGameRoot(_root.View());
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
        session.SetGameRoot(_root.View());
        EXPECT_EQ(StateOf(Require(session.FindSet("keep")), 0), ArtGenSetJobState::Running);
    }

    TEST_F(ArtGenSetSessionTest, AReportOfAnOlderRunIsStale) {
        ASSERT_TRUE(Enqueue("stale", "a\nb").has_value());
        const Json first = Poll();
        const uint32_t firstId = static_cast<uint32_t>(Require(GetJsonInt(*RequestOf(first), "id")));
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
        noPreset.subjects = Require(ParseArtGenSubjects("a"));
        EXPECT_FALSE(session.EnqueueSet(noPreset).has_value());

        ArtGenSession noRoot;
        ArtGenSetRequest request;
        request.preset = "wood";
        request.subjects = Require(ParseArtGenSubjects("a"));
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
        request.subjects = Require(ParseArtGenSubjects("a"));
        const auto refused = session.EnqueueSet(request);
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("broken"), String::npos);

        request.preset = "form";
        request.presetData = "{}";
        const auto notPreset = session.EnqueueSet(request);
        ASSERT_FALSE(notPreset.has_value());
        EXPECT_NE(notPreset.error().find("not a preset"), String::npos);
    }

    TEST_F(ArtGenSetSessionTest, ADraftTravelsInlineWithEveryJob) {
        ArtGenSetRequest request;
        request.preset = "form";
        request.presetData = R"({"preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"})";
        request.subjects = Require(ParseArtGenSubjects("a"));
        request.name = "drafted";
        request.seed = 3;
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        const Json first = Poll();
        const Json* sent = RequestOf(first);
        ASSERT_NE(sent, nullptr);
        EXPECT_EQ(Require(FindJsonMember(*sent, "preset")).type(), Json::value_t::object);
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
        EXPECT_EQ(Require(FindJsonMember(first, "jobs")), (Json::array({"rook", "white_knight"})));
        EXPECT_EQ(GetJsonBool(first, "serviceConnected"), Optional<bool>(false));
        EXPECT_EQ(KeysOf(first), (Vector<String>{"jobs", "seed", "serviceConnected", "set"}));
        EXPECT_EQ(OnDisk("from_lines").count, 3);

        Json list = Json::object();
        list["preset"] = "wood";
        list["subjects"] = Json::array({"bishop: a bishop", "a queen"});
        list["set"] = "from_list";
        const Json second = Result(Call("artgen_enqueue", list));
        ASSERT_NE(FindJsonMember(second, "jobs"), nullptr);
        EXPECT_EQ(Require(FindJsonMember(second, "jobs")), (Json::array({"bishop", "a_queen"})));
        EXPECT_EQ(OnDisk("from_list").count, kArtGenSetDefaultCount);

        Json objects = Json::object();
        objects["preset"] = "wood";
        objects["subjects"] = Json::array({Json{{"name", "pawn"}, {"subject", "a small pawn"}}, Json{{"subject", "A Tall King"}}});
        objects["set"] = "from_objects";
        objects["seed"] = nullptr;
        const Json third = Result(Call("artgen_enqueue", objects));
        ASSERT_NE(FindJsonMember(third, "jobs"), nullptr);
        EXPECT_EQ(Require(FindJsonMember(third, "jobs")), (Json::array({"pawn", "a_tall_king"})));
        const ArtGenSet disk = OnDisk("from_objects");
        EXPECT_EQ(disk.jobs[0].subject, "a small pawn");
        EXPECT_EQ(GetJsonInt(third, "seed"), Optional<int64_t>(disk.seed)) << "no seed: one is drawn and told";

        Json draft = Json::object();
        draft["preset"] = Require(ParseJson(R"({"preset":1,"title":"form","workflow":"txt2img","prompt":"{subject}"})"));
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
            EXPECT_EQ(withoutState, ManifestJson(OnDisk("shown")));
        }
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "boom");
        const Json answer = Result(Call("artgen_set", params));
        EXPECT_EQ(GetJsonString(answer, "state"), Optional<String>("Complete"));
        Json withoutState = answer;
        withoutState.erase("state");
        EXPECT_EQ(withoutState, ManifestJson(OnDisk("shown")));
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
        EXPECT_EQ(KeysOf(Result(Call("artgen_retry", retry))), (Vector<String>{"retried", "set"}));
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
        EXPECT_EQ(cancelled, (Json{{"job", "c"}, {"set", "one"}, {"state", "Cancelled"}}));
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
        EXPECT_EQ(Result(Call("artgen_cancel", all)), (Json{{"set", "two"}, {"state", "Complete"}}));
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
            EXPECT_EQ(gAccepted[row].path, Format("{}/artgen/out/pieces/{}/{}_{}.png", _root.View(), names[row], names[row], seeds[row]));
            EXPECT_EQ(gAccepted[row].provenance, Format("{}/artgen/out/pieces/{}/{}_{}.artgen.json", _root.View(), names[row], names[row], seeds[row]));
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
        EXPECT_EQ(GetJsonInt((Require(FindJsonMember(state, "sets")))[0], "accepted"), Optional<int64_t>(3));
    }

    TEST_F(ArtGenSetSessionTest, ASetOfAGameWhoseRootHasANonAsciiNameKeepsRelativeFilesAndAcceptsAll) {
        const Path parent = _root;
        _root = parent / "caf\xC3\xA9 game";
        session.SetGameRoot(_root.View());
        ASSERT_TRUE(Enqueue("pieces", "rook: a rook\nknight: a knight").has_value());
        ASSERT_TRUE(PathExists(_root / "artgen" / "out" / "pieces" / "manifest.json"));

        RunAndPickAll("pieces");
        {
            const ArtGenSet disk = OnDisk("pieces");
            ASSERT_EQ(disk.jobs[0].variants.size(), 2u);
            EXPECT_EQ(disk.jobs[0].variants[1].file, "rook/rook_41.png") << "relative to the set folder, whatever the root's name";
            EXPECT_EQ(disk.jobs[0].variants[1].provenance, "rook/rook_41.artgen.json");
        }

        ASSERT_NO_FATAL_FAILURE(MakeFiles("pieces"));
        Json params = Json::object();
        params["set"] = "pieces";
        const Json answer = Result(Call("artgen_accept_set", params));
        const Json* accepted = FindJsonMember(answer, "accepted");
        ASSERT_NE(accepted, nullptr) << DumpJson(answer).c_str();
        EXPECT_EQ(accepted->size(), 2u);
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].path, Format("{}/artgen/out/pieces/rook/rook_41.png", _root.View()));

        ReloadFromDisk();
        EXPECT_NE(session.FindSet("pieces"), nullptr);
        RemoveDirectory(parent);
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
        const String message = GetJsonString(Require(FindJsonMember(refused, "error")), "message").value_or(String());
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
        RemoveFileIfPresent(AbsoluteInSet(_root, "files", "c/c_41.png"));
        Json refused = Call("artgen_accept_set", params);
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = GetJsonString(Require(FindJsonMember(refused, "error")), "message").value_or(String());
        EXPECT_NE(message.find("'c'"), String::npos) << message.c_str();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_FALSE(OnDisk("files").jobs[0].accepted.has_value());

        // The provenance of a row is gone
        ASSERT_TRUE(WriteFileAtomically(AbsoluteInSet(_root, "files", "c/c_41.png"), "x").has_value());
        RemoveFileIfPresent(AbsoluteInSet(_root, "files", "a/a_41.artgen.json"));
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "artgen_accept_failed");
        EXPECT_TRUE(gAccepted.empty());

        // No acceptor here
        ASSERT_TRUE(WriteFileAtomically(AbsoluteInSet(_root, "files", "a/a_41.artgen.json"), "x").has_value());
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
        const String message = GetJsonString(Require(FindJsonMember(refused, "error")), "message").value_or(String());
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
        EXPECT_EQ(Require(FindJsonMember(again, "accepted")).size(), 5u);
        ASSERT_EQ(gAccepted.size(), 7u) << "the first two replace themselves";
        EXPECT_EQ(gAccepted[2].name, "a");
        EXPECT_EQ(gAccepted[6].name, "e");
        for (const ArtGenSetJob& job : OnDisk("five").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfImagesRunsItsAcceptorsInsideOneBatchAndMarksTheRowsAfterIt) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(Enqueue("batch", "a\nb\nc").has_value());
        RunAndPickAll("batch");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("batch"));
        size_t markedWhileEnding = 99;
        gDuringEnd = [this, &markedWhileEnding] {
            markedWhileEnding = 0;
            for (const ArtGenSetJob& job : OnDisk("batch").jobs) {
                markedWhileEnding += job.accepted.has_value() ? 1 : 0;
            }
        };

        const Json answer = Result(AcceptAllCall("batch"));

        const Vector<String> expected{"begin", "accept a", "accept b", "accept c", "end"};
        EXPECT_EQ(gEvents, expected);
        EXPECT_EQ(markedWhileEnding, 0u) << "the library is saved before any row is marked";
        ASSERT_NE(FindJsonMember(answer, "accepted"), nullptr);
        const Json& rows = *FindJsonMember(answer, "accepted");
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(GetJsonString(rows[2], "as"), Optional<String>("c"));
        EXPECT_EQ(GetJsonString(rows[2], "sprite"), Optional<String>("c"));
        EXPECT_EQ(GetJsonInt(rows[2], "seed"), Optional<int64_t>(kSetSeed + 1));
        const ArtGenSet disk = OnDisk("batch");
        for (const ArtGenSetJob& job : disk.jobs) {
            ASSERT_TRUE(job.accepted.has_value());
            EXPECT_EQ(job.pick, Optional<int64_t>(kSetSeed + 1));
            EXPECT_FALSE(job.accepted->kept);
        }
        EXPECT_TRUE(disk.processing.has_value());
    }

    TEST_F(ArtGenSetSessionTest, ABatchedAcceptAllCountsTheAlternativesOfARowThatHasNoPickYet) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(Enqueue("nopick", "a\nb").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_EQ(session.PickVariant("nopick", "b", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("nopick"));

        const Json answer = Result(AcceptAllCall("nopick"));

        ASSERT_NE(FindJsonMember(answer, "accepted"), nullptr);
        const Json& rows = *FindJsonMember(answer, "accepted");
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(GetJsonInt(rows[0], "seed"), Optional<int64_t>(kSetSeed)) << "the first variant is the candidate of a row without a pick";
        EXPECT_EQ(GetJsonInt(rows[0], "alternatives"), Optional<int64_t>(1));
        EXPECT_EQ(GetJsonInt(rows[1], "alternatives"), Optional<int64_t>(1));
    }

    TEST_F(ArtGenSetSessionTest, AFailureHalfwayInABatchEndsItMarksTheRowsDoneAndTheNextCallFinishes) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(Enqueue("five", "a\nb\nc\nd\ne").has_value());
        RunAndPickAll("five");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("five"));

        gFailOn = "c";
        const Json refused = AcceptAllCall("five");

        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = ErrorMessage(refused);
        EXPECT_NE(message.find("row 3 of 5"), String::npos) << message.c_str();
        EXPECT_NE(message.find("accepted before it: a, b"), String::npos) << message.c_str();
        const Vector<String> expected{"begin", "accept a", "accept b", "end"};
        EXPECT_EQ(gEvents, expected) << "the batch ends once, on the failure path too";
        const ArtGenSet disk = OnDisk("five");
        EXPECT_TRUE(disk.jobs[0].accepted.has_value());
        EXPECT_TRUE(disk.jobs[1].accepted.has_value());
        EXPECT_FALSE(disk.jobs[2].accepted.has_value());

        gFailOn.clear();
        gEvents.clear();
        EXPECT_EQ(ErrorCode(AcceptAllCall("five")), "");
        EXPECT_EQ(gEvents.size(), 7u) << "begin, five accepts, end";
        for (const ArtGenSetJob& job : OnDisk("five").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, AFailedLibrarySaveMarksNoRowAndNamesTheFilesWritten) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(Enqueue("lost", "a\nb\nc").has_value());
        RunAndPickAll("lost");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("lost"));
        const uint64_t versionBefore = session.Version();

        gEndFails = true;
        const Json refused = AcceptAllCall("lost");

        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = ErrorMessage(refused);
        EXPECT_NE(message.find("files written: a, b, c"), String::npos) << message.c_str();
        EXPECT_NE(message.find("library was not saved"), String::npos) << message.c_str();
        EXPECT_NE(message.find("call again"), String::npos) << message.c_str();
        for (const ArtGenSetJob& job : OnDisk("lost").jobs) {
            EXPECT_FALSE(job.accepted.has_value());
        }
        EXPECT_FALSE(OnDisk("lost").processing.has_value());
        EXPECT_EQ(session.Version(), versionBefore) << "nothing of the set changed";

        gEndFails = false;
        EXPECT_EQ(ErrorCode(AcceptAllCall("lost")), "");
        for (const ArtGenSetJob& job : OnDisk("lost").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, ARowFailureAndAFailedLibrarySaveAreBothInTheError) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(Enqueue("both", "a\nb\nc").has_value());
        RunAndPickAll("both");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("both"));

        gFailOn = "b";
        gEndFails = true;
        const String message = ErrorMessage(AcceptAllCall("both"));

        EXPECT_NE(message.find("'b'"), String::npos) << message.c_str();
        EXPECT_NE(message.find("the disk is full"), String::npos) << message.c_str();
        EXPECT_NE(message.find("library was not saved"), String::npos) << message.c_str();
        EXPECT_FALSE(OnDisk("both").jobs[0].accepted.has_value()) << "the row before the failure is not marked either";
    }

    TEST_F(ArtGenSetSessionTest, ASetWithNothingToWriteAndASoundSetCallNoBatchHook) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(Enqueue("done", "a").has_value());
        RunAndPickAll("done");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("done"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("done")), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("done", "a"));
        gEvents.clear();
        EXPECT_EQ(ErrorCode(AcceptAllCall("done")), "");
        EXPECT_TRUE(gEvents.empty()) << "every row is kept: nothing is written, no batch is opened";

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
        ASSERT_EQ(session.PickVariant("heard", "tap", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("heard"));
        gEvents.clear();
        EXPECT_EQ(ErrorCode(AcceptAllCall("heard")), "");
        const Vector<String> expected{"accept knock", "accept tap"};
        EXPECT_EQ(gEvents, expected) << "a sound touches no library";
        EXPECT_TRUE(OnDisk("heard").jobs[1].accepted.has_value());
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
        EXPECT_EQ(GetJsonInt((Require(FindJsonMember(answer, "accepted")))[0], "seed"), Optional<int64_t>(kSetSeed));
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfASoundSetStillNeedsAPickOnEveryRow) {
        Poll();
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = Require(ParseArtGenSubjects("knock: a wooden knock\ntap: a soft tap"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "heard";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll("audio"));
        ASSERT_EQ(session.PickVariant("heard", "knock", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("heard"));

        const Json refused = AcceptAllCall("heard");
        EXPECT_EQ(ErrorCode(refused), "artgen_set_incomplete");
        const String message = GetJsonString(Require(FindJsonMember(refused, "error")), "message").value_or(String());
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
        const Json& rows = Require(FindJsonMember(answer, "accepted"));
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(GetJsonString(rows[1], "job"), Optional<String>("b"));
        EXPECT_EQ(GetJsonBool(rows[1], "kept"), Optional<bool>(true));
        EXPECT_EQ(FindJsonMember(rows[1], "as"), nullptr) << "nothing was written for it";
        EXPECT_EQ(KeysOf(rows[1]), (Vector<String>{"job", "kept"}));
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
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(reply, "switched")), "sprite"), Optional<String>("a"));

        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(gAccepted[0].name, "a") << "the same name: the file is replaced";
        EXPECT_EQ(gAccepted[0].seed, kSetSeed);
        EXPECT_EQ(gAccepted[0].path, Format("{}/artgen/out/live/a/a_{}.png", _root.View(), kSetSeed));
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
        EXPECT_NE(write.error.find(Format("the row still names seed {}", kSetSeed + 1)), String::npos) << write.error.c_str();
        const Json refused = Call("artgen_pick", pick);
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        EXPECT_EQ(ManifestJson(OnDisk("fail")), ManifestJson(before));
        EXPECT_EQ(Require(session.FindSet("fail")).jobs[0].pick, Optional<int64_t>(kSetSeed + 1));

        gFailOn.clear();
        RemoveFileIfPresent(AbsoluteInSet(_root, "fail", "a/a_40.png"));
        EXPECT_EQ(session.PickVariant("fail", "a", kSetSeed, &write), ArtGenSetEdit::WriteFailed);
        EXPECT_NE(write.error.find("is gone"), String::npos) << write.error.c_str();
        EXPECT_TRUE(gAccepted.empty()) << "the files are checked before the acceptor is asked";

        session.SetAcceptor(nullptr);
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "artgen_accept_failed");
        EXPECT_EQ(Require(session.FindSet("fail")).jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
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
        request.subjects = Require(ParseArtGenSubjects("knock: a wooden knock"));
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

    TEST_F(ArtGenSetSessionTest, KeepFixesALiveRowAndDropsItsAlternatives) {
        ASSERT_TRUE(Enqueue("keep", "a\nb").has_value());
        RunAndPickAll("keep");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("keep"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("keep")), "");

        Json params = Json::object();
        params["set"] = "keep";
        params["job"] = "a";
        const Json answer = Result(Call("artgen_keep", params));
        const Json* kept = FindJsonMember(answer, "kept");
        ASSERT_NE(kept, nullptr) << DumpJson(answer).c_str();
        ASSERT_EQ(kept->size(), 1u);
        EXPECT_EQ((*kept)[0], "a");
        EXPECT_EQ(KeysOf(answer), (Vector<String>{"kept", "set"})) << "no `replaced` when no row was left alone";

        ASSERT_EQ(gKept.size(), 1u);
        EXPECT_EQ(gKept[0].kind, "image");
        EXPECT_EQ(gKept[0].name, "a");
        EXPECT_EQ(gKept[0].seed, kSetSeed + 1);
        EXPECT_EQ(gKept[0].date, OnDisk("keep").jobs[0].accepted->date);
        const ArtGenSet disk = OnDisk("keep");
        EXPECT_TRUE(disk.jobs[0].accepted->kept);
        ASSERT_EQ(disk.jobs[0].variants.size(), 1u);
        EXPECT_EQ(disk.jobs[0].variants[0].seed, kSetSeed + 1);
        EXPECT_TRUE(IsLive(disk.jobs[1])) << "the other row is untouched";
        EXPECT_EQ(disk.jobs[1].variants.size(), 2u);

        Json pick = Json::object();
        pick["set"] = "keep";
        pick["job"] = "a";
        pick["seed"] = kSetSeed;
        gAccepted.clear();
        EXPECT_EQ(ErrorCode(Call("artgen_pick", pick)), "artgen_no_variant") << "the alternatives are gone";
        EXPECT_TRUE(gAccepted.empty());
    }

    TEST_F(ArtGenSetSessionTest, KeepOfARowThatIsNotLiveIsInvalidParams) {
        ASSERT_TRUE(Enqueue("notlive", "a\nb").has_value());
        RunAndPickAll("notlive");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("notlive"));
        Json params = Json::object();
        params["set"] = "notlive";
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "invalid_params") << "nothing is accepted yet";
        params["job"] = "a";
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "invalid_params");

        ASSERT_EQ(ErrorCode(AcceptAllCall("notlive")), "");
        ASSERT_EQ(ErrorCode(Call("artgen_keep", params)), "");
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "invalid_params") << "already kept";
        params.erase("job");
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "") << "b is still live";
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "invalid_params") << "nothing live is left";
        EXPECT_EQ(gKept.size(), 2u);
    }

    TEST_F(ArtGenSetSessionTest, KeepOfAWholeSetKeepsEveryLiveRow) {
        ASSERT_TRUE(Enqueue("all", "a\nb\nc").has_value());
        RunAndPickAll("all");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("all"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("all")), "");

        Json params = Json::object();
        params["set"] = "all";
        const Json answer = Result(Call("artgen_keep", params));
        ASSERT_NE(FindJsonMember(answer, "kept"), nullptr);
        EXPECT_EQ(Require(FindJsonMember(answer, "kept")).size(), 3u);
        EXPECT_EQ(gKept.size(), 3u);
        const ArtGenSet disk = OnDisk("all");
        EXPECT_EQ(CountKept(disk), 3);
        EXPECT_EQ(CountLive(disk), 0);
    }

    TEST_F(ArtGenSetSessionTest, KeepNamesWhatIsUnknownAndNeedsAKeeper) {
        ASSERT_TRUE(Enqueue("unk", "a").has_value());
        RunAndPickAll("unk");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("unk"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("unk")), "");

        Json params = Json::object();
        params["set"] = "nothing";
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "artgen_no_set");
        params["set"] = "unk";
        params["job"] = "zzz";
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "artgen_no_set");
        params["job"] = 3;
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "invalid_params");
        EXPECT_EQ(ErrorCode(Call("artgen_keep", Json::object())), "invalid_params");

        params.erase("job");
        session.SetKeeper(nullptr);
        EXPECT_EQ(ErrorCode(Call("artgen_keep", params)), "artgen_accept_failed");
        EXPECT_TRUE(IsLive(OnDisk("unk").jobs[0]));
    }

    TEST_F(ArtGenSetSessionTest, AFailingKeeperChangesNothingForItsRowAndTheRowsBeforeStayKept) {
        ASSERT_TRUE(Enqueue("kfail", "a\nb\nc").has_value());
        RunAndPickAll("kfail");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("kfail"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("kfail")), "");

        Json params = Json::object();
        params["set"] = "kfail";
        gKeepFailOn = "b";
        const Json refused = Call("artgen_keep", params);
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = GetJsonString(Require(FindJsonMember(refused, "error")), "message").value_or(String());
        EXPECT_NE(message.find("'b'"), String::npos) << message.c_str();
        EXPECT_NE(message.find("read-only"), String::npos) << message.c_str();
        EXPECT_NE(message.find("kept before it: a"), String::npos) << message.c_str();

        const ArtGenSet disk = OnDisk("kfail");
        EXPECT_TRUE(disk.jobs[0].accepted->kept);
        EXPECT_TRUE(IsLive(disk.jobs[1]));
        EXPECT_EQ(disk.jobs[1].variants.size(), 2u) << "the failed row keeps its alternatives";
        EXPECT_TRUE(IsLive(disk.jobs[2]));

        gKeepFailOn.clear();
        const Json again = Result(Call("artgen_keep", params));
        EXPECT_EQ(Require(FindJsonMember(again, "kept")).size(), 2u);
        EXPECT_EQ(CountKept(OnDisk("kfail")), 3);
    }

    TEST_F(ArtGenSetSessionTest, AKeptSoundRowAsksTheKeeperForAClip) {
        Poll();
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = Require(ParseArtGenSubjects("knock: a wooden knock"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "kclips";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("kclips", "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("kclips"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("kclips")), "");
        EXPECT_TRUE(session.KeepSet("kclips").has_value());
        ASSERT_EQ(gKept.size(), 1u);
        EXPECT_EQ(gKept[0].kind, "audio");
        EXPECT_EQ(gKept[0].name, "knock");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenStateCountsLiveAndKeptRows) {
        ASSERT_TRUE(Enqueue("counts", "a\nb\nc").has_value());
        RunAndPickAll("counts");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("counts"));
        const auto setState = [this] { return (Require(FindJsonMember(Result(Call("artgen_state")), "sets")))[0]; };
        EXPECT_EQ(GetJsonInt(setState(), "live"), Optional<int64_t>(0));
        EXPECT_EQ(GetJsonInt(setState(), "kept"), Optional<int64_t>(0));

        ASSERT_EQ(ErrorCode(AcceptAllCall("counts")), "");
        EXPECT_EQ(GetJsonInt(setState(), "live"), Optional<int64_t>(3));
        EXPECT_EQ(GetJsonInt(setState(), "kept"), Optional<int64_t>(0));

        ASSERT_TRUE(session.KeepSet("counts", "b").has_value());
        EXPECT_EQ(GetJsonInt(setState(), "live"), Optional<int64_t>(2));
        EXPECT_EQ(GetJsonInt(setState(), "kept"), Optional<int64_t>(1));
        EXPECT_EQ(GetJsonInt(setState(), "accepted"), Optional<int64_t>(3));
    }

    TEST_F(ArtGenSetSessionTest, AConnectedServiceWhoseGeneratorIsNotReadyLeavesTheJobsPending) {
        ASSERT_TRUE(Enqueue("down", "a\nb").has_value());
        Json notRunning = ReadyPoll();
        notRunning["state"] = "not_running";
        notRunning["reason"] = "ComfyUI is not started";
        const Json first = Result(Call("artgen_poll", notRunning));
        EXPECT_EQ(RequestOf(first), nullptr);
        EXPECT_EQ(CountJobs(Require(session.FindSet("down")), ArtGenSetJobState::Pending), 2);
        EXPECT_EQ(CountJobs(Require(session.FindSet("down")), ArtGenSetJobState::Running), 0);
        EXPECT_FALSE(session.IsGeneratorReady());
        EXPECT_TRUE(session.IsServiceConnected());

        const Json ready = Poll();
        ASSERT_NE(RequestOf(ready), nullptr) << "the first poll with a ready generator starts a job";
        EXPECT_EQ(CountJobs(Require(session.FindSet("down")), ArtGenSetJobState::Running), 1);
        EXPECT_TRUE(session.IsGeneratorReady());
    }

    TEST_F(ArtGenSetSessionTest, ASwitchOfARowAnotherSetAcceptedLaterIsRefusedAndTheNewerSetStillSwitches) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());
        const ArtGenSet before = OnDisk("first");

        ArtGenSwitch write;
        EXPECT_EQ(session.PickVariant("first", "a", kSetSeed, &write), ArtGenSetEdit::WriteFailed);
        EXPECT_NE(write.error.find("'second'"), String::npos) << write.error.c_str();
        Json pick = Json::object();
        pick["set"] = "first";
        pick["job"] = "a";
        pick["seed"] = kSetSeed;
        const Json refused = Call("artgen_pick", pick);
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        EXPECT_NE(ErrorMessage(refused).find("'second'"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_TRUE(gAccepted.empty()) << "the game's file was not written";
        EXPECT_EQ(ManifestJson(OnDisk("first")), ManifestJson(before));

        EXPECT_EQ(session.PickVariant("first", "b", kSetSeed), ArtGenSetEdit::Ok) << "a row nobody replaced switches as before";
        gAccepted.clear();
        EXPECT_EQ(session.PickVariant("second", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(gAccepted[0].name, "a");
        EXPECT_EQ(OnDisk("second").jobs[0].pick, Optional<int64_t>(kSetSeed));
    }

    TEST_F(ArtGenSetSessionTest, TheLaterDateOwnsTheNameWhateverTheOrderOfTheSets) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());
        ArtGenSet disk = OnDisk("first");
        disk.jobs[0].accepted->date = "2999-01-01T00:00:00Z";
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();

        EXPECT_EQ(session.PickVariant("second", "a", kSetSeed), ArtGenSetEdit::WriteFailed);
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_EQ(session.PickVariant("first", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gAccepted.size(), 1u);
    }

    TEST_F(ArtGenSetSessionTest, AnAcceptOfANameAnotherSetHoldsWithALaterDateTakesTheNextSecondAndOwnsTheName) {
        ASSERT_TRUE(Enqueue("first", "a\nb").has_value());
        ASSERT_TRUE(Enqueue("second", "a").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("first"));
        ASSERT_EQ(session.PickVariant("second", "a", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("first"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("second"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("second")), "");
        ArtGenSet disk = OnDisk("second");
        disk.jobs[0].accepted->date = "2999-01-01T00:00:00Z";
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();

        ASSERT_EQ(ErrorCode(AcceptAllCall("first")), "");

        const ArtGenSet& first = *session.FindSet("first");
        const ArtGenSet& second = *session.FindSet("second");
        EXPECT_EQ(first.jobs[0].accepted->date, "2999-01-01T00:00:01Z");
        EXPECT_EQ(OnDisk("first").jobs[0].accepted->date, "2999-01-01T00:00:01Z");
        EXPECT_EQ(session.FindNewerOwner(second, second.jobs[0]), &first);
        EXPECT_EQ(session.FindNewerOwner(first, first.jobs[0]), nullptr);
        EXPECT_LT(first.jobs[1].accepted->date, String("2999")) << "a name no other set holds keeps the stamp of now";
    }

    TEST_F(ArtGenSetSessionTest, ASwitchOfTheOwnerTakesADateAfterTheOtherSetsDateAndKeepsTheName) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());
        for (const char* name : {"first", "second"}) {
            ArtGenSet disk = OnDisk(name);
            disk.jobs[0].accepted->date = "2999-01-01T00:00:00Z";
            ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        }
        ReloadFromDisk();
        const ArtGenSet& first = *session.FindSet("first");
        const ArtGenSet& second = *session.FindSet("second");
        ASSERT_EQ(session.FindNewerOwner(first, first.jobs[0]), &second) << "manifests that hold equal dates: the later set in the list owns the name";
        ASSERT_EQ(session.FindNewerOwner(second, second.jobs[0]), nullptr);
        EXPECT_EQ(session.PickVariant("first", "a", kSetSeed), ArtGenSetEdit::WriteFailed);

        ASSERT_EQ(session.PickVariant("second", "a", kSetSeed), ArtGenSetEdit::Ok);

        EXPECT_EQ(second.jobs[0].accepted->date, "2999-01-01T00:00:01Z");
        EXPECT_EQ(session.FindNewerOwner(first, first.jobs[0]), &second);
        EXPECT_EQ(session.FindNewerOwner(second, second.jobs[0]), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, TheNextSingleRunDoesNotCarryTheDraftsOfASetsJob) {
        ASSERT_TRUE(Enqueue("drafts", "a").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_FALSE(session.GetRun().variants.empty()) << "the job's drafts are the run's variants when it ends";

        ASSERT_TRUE(session.Request("wood", "b", 1, 5, true).has_value());

        EXPECT_TRUE(session.GetRun().variants.empty());
        EXPECT_EQ(session.GetRun().keptVariants, 0);
        EXPECT_EQ(session.FindSet("drafts")->jobs[0].variants.size(), 2u) << "the row keeps its own";
    }

    TEST_F(ArtGenSetSessionTest, ASoundAndAnImageOfTheSameNameDoNotReplaceEachOther) {
        Poll();
        ASSERT_TRUE(Enqueue("pictures", "knock").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll("image"));
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = Require(ParseArtGenSubjects("knock"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "clips";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll("audio"));
        ASSERT_EQ(session.PickVariant("pictures", "knock", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_EQ(session.PickVariant("clips", "knock", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("pictures"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("clips"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("pictures")), "");
        ASSERT_EQ(ErrorCode(AcceptAllCall("clips")), "");
        gAccepted.clear();

        EXPECT_EQ(session.PickVariant("pictures", "knock", kSetSeed), ArtGenSetEdit::Ok) << "assets/artgen/knock.png is not knock.wav";
        EXPECT_EQ(gAccepted.size(), 1u);
    }

    TEST_F(ArtGenSetSessionTest, KeepOfARowAnotherSetAcceptedLaterIsRefusedAndTheNewerSetKeeps) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());

        Json params = Json::object();
        params["set"] = "first";
        params["job"] = "a";
        const Json refused = Call("artgen_keep", params);
        EXPECT_EQ(ErrorCode(refused), "invalid_params");
        EXPECT_NE(ErrorMessage(refused).find("'second'"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_TRUE(gKept.empty()) << "no false kept seed reached the game's file";
        EXPECT_TRUE(IsLive(OnDisk("first").jobs[0]));

        // The whole set keeps what it still owns and says what it left
        params.erase("job");
        const Json answer = Result(Call("artgen_keep", params));
        ASSERT_NE(FindJsonMember(answer, "kept"), nullptr) << DumpJson(answer).c_str();
        ASSERT_EQ(Require(FindJsonMember(answer, "kept")).size(), 1u);
        EXPECT_EQ((Require(FindJsonMember(answer, "kept")))[0], "b");
        const Json* replaced = FindJsonMember(answer, "replaced");
        ASSERT_NE(replaced, nullptr);
        ASSERT_EQ(replaced->size(), 1u);
        EXPECT_EQ(GetJsonString((*replaced)[0], "job"), Optional<String>("a"));
        EXPECT_EQ(GetJsonString((*replaced)[0], "by"), Optional<String>("second"));
        EXPECT_EQ(KeysOf(answer), (Vector<String>{"kept", "replaced", "set"}));
        EXPECT_EQ(KeysOf((*replaced)[0]), (Vector<String>{"by", "job"}));
        ASSERT_EQ(gKept.size(), 1u);
        EXPECT_EQ(gKept[0].name, "b");

        const Json onlyReplaced = Call("artgen_keep", params);
        EXPECT_EQ(ErrorCode(onlyReplaced), "invalid_params");
        EXPECT_NE(ErrorMessage(onlyReplaced).find("second"), String::npos) << ErrorMessage(onlyReplaced).c_str();

        params["set"] = "second";
        ASSERT_EQ(ErrorCode(Call("artgen_keep", params)), "");
        ASSERT_EQ(gKept.size(), 2u);
        EXPECT_EQ(gKept[1].name, "a");
        EXPECT_EQ(gKept[1].seed, kSetSeed + 1);
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfTheOlderSetLeavesTheRowTheNewerSetHolds) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());
        const ArtGenSet before = OnDisk("first");

        const Json answer = Result(AcceptAllCall("first"));
        ASSERT_EQ(gAccepted.size(), 1u) << "b is written again, a is not";
        EXPECT_EQ(gAccepted[0].name, "b");
        const Json& rows = Require(FindJsonMember(answer, "accepted"));
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(GetJsonString(rows[0], "job"), Optional<String>("a"));
        EXPECT_EQ(GetJsonString(rows[0], "replacedBy"), Optional<String>("second"));
        EXPECT_EQ(FindJsonMember(rows[0], "as"), nullptr);
        EXPECT_EQ(KeysOf(rows[0]), (Vector<String>{"job", "replacedBy"})) << "a row left as it is has none of the acceptor's members";
        EXPECT_EQ(GetJsonString(rows[1], "as"), Optional<String>("b"));
        EXPECT_EQ(KeysOf(rows[1]), (Vector<String>{"alternatives", "as", "job", "seed", "sprite"})) << "a row written: the acceptor's members, as, seed and alternatives";
        EXPECT_EQ(OnDisk("first").jobs[0].accepted->date, before.jobs[0].accepted->date) << "the row's mark did not move";

        // A set whose row of that name was never accepted writes it: it is the newest then
        ASSERT_TRUE(Enqueue("third", "a").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("third"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("third"));
        gAccepted.clear();
        ASSERT_EQ(ErrorCode(AcceptAllCall("third")), "");
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(session.PickVariant("second", "a", kSetSeed), ArtGenSetEdit::WriteFailed) << "the third holds it now";
    }

    TEST_F(ArtGenSetSessionTest, APickOnAKeptRowIsNoVariantEvenWhenTheManifestStillListsTheOthers) {
        ASSERT_TRUE(Enqueue("old", "a").has_value());
        RunAndPickAll("old");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("old"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("old")), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("old", "a"));
        ASSERT_EQ(Require(session.FindSet("old")).jobs[0].variants.size(), 2u);
        gAccepted.clear();

        Json pick = Json::object();
        pick["set"] = "old";
        pick["job"] = "a";
        pick["seed"] = kSetSeed;
        const Json refused = Call("artgen_pick", pick);
        EXPECT_EQ(ErrorCode(refused), "artgen_no_variant");
        EXPECT_NE(ErrorMessage(refused).find("kept"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_EQ(Require(session.FindSet("old")).jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
        EXPECT_EQ(session.PickVariant("old", "a", kSetSeed + 1), ArtGenSetEdit::Ok) << "the pick it holds is no change";
        EXPECT_TRUE(gAccepted.empty());
    }

    TEST_F(ArtGenSetSessionTest, TwoDefaultNamesOfTheSameSecondAreDistinct) {
        Vector<String> names;
        for (int32_t index = 0; index < 3; ++index) {
            ArtGenSetRequest request;
            request.preset = "wood";
            request.subjects = Require(ParseArtGenSubjects("a"));
            request.seed = kSetSeed;
            const auto name = session.EnqueueSet(request);
            ASSERT_TRUE(name.has_value()) << name.error().c_str();
            EXPECT_TRUE(IsValidSpriteName(*name));
            EXPECT_EQ(std::count(names.begin(), names.end(), *name), 0) << name->c_str();
            names.push_back(*name);
        }
        EXPECT_EQ(session.GetSets().size(), 3u);

        ArtGenSetRequest longPreset;
        longPreset.preset = String(70, 'p');
        longPreset.subjects = Require(ParseArtGenSubjects("a"));
        for (int32_t index = 0; index < 12; ++index) {
            const auto name = session.EnqueueSet(longPreset);
            ASSERT_TRUE(name.has_value()) << name.error().c_str();
            EXPECT_LE(name->size(), static_cast<size_t>(kMaxSpriteNameLength));
            EXPECT_TRUE(IsValidSpriteName(*name)) << name->c_str();
        }

        ArtGenSetRequest named;
        named.preset = "wood";
        named.subjects = Require(ParseArtGenSubjects("a"));
        named.name = "mine";
        ASSERT_TRUE(session.EnqueueSet(named).has_value());
        EXPECT_FALSE(session.EnqueueSet(named).has_value()) << "a name that was given and is taken stays a refusal";
    }

    TEST_F(ArtGenSetSessionTest, ASoundSetIsAcceptedAsClipsWithDefaultSettings) {
        Poll();  // the catalog tells the kind of the preset
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = Require(ParseArtGenSubjects("knock: a wooden knock\ntap: a soft tap"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "sounds";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        EXPECT_EQ(Require(session.FindSet("sounds")).kind, "audio");
        RunAndPickAll("sounds", "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("sounds"));

        Json params = Json::object();
        params["set"] = "sounds";
        const Json answer = Result(Call("artgen_accept_set", params));
        ASSERT_NE(FindJsonMember(answer, "accepted"), nullptr);
        EXPECT_EQ(GetJsonString((Require(FindJsonMember(answer, "accepted")))[0], "clip"), Optional<String>("knock"));
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
        request.subjects = Require(ParseArtGenSubjects("knock: a wooden knock\ntap: a soft tap"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "early";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        EXPECT_EQ(Require(session.FindSet("early")).kind, "image") << "the catalog is empty: only a guess";

        RunAndPickAll("early", "audio");
        EXPECT_EQ(Require(session.FindSet("early")).kind, "audio") << "the first poll brought the catalog, and the job went out with its kind";
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
        request.subjects = Require(ParseArtGenSubjects("knock: a wooden knock"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "unlisted";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());

        RunAndPickAll("unlisted", "audio");
        EXPECT_EQ(Require(session.FindSet("unlisted")).kind, "audio");
        EXPECT_EQ(OnDisk("unlisted").kind, "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("unlisted"));

        Json params = Json::object();
        params["set"] = "unlisted";
        EXPECT_EQ(ErrorCode(Call("artgen_accept_set", params)), "");
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_FALSE(gAccepted[0].image);
    }

    // ---- an atlas group of the set (Sets/CLAUDE.md, "Accept all") ----

    namespace {

        /// A poll whose catalog gives the saved preset `wood` the content @p woodAtlas for its `atlas` key (null: no key)
        Json PollWithWoodAtlas(const Json& woodAtlas) {
            Json params = ReadyPoll();
            Json wood = params["presets"][0];
            wood["preset"] = Json{{"preset", 1}, {"workflow", "txt2img"}, {"prompt", "{subject}"}};
            if (!woodAtlas.is_null()) {
                wood["preset"]["atlas"] = woodAtlas;
            }
            Json knock = params["presets"][1];
            knock["preset"] = Json{{"preset", 1}, {"workflow", "sound"}, {"prompt", "{subject}"}, {"atlas", "pieces"}};
            params["presets"] = Json::array({wood, knock});
            return params;
        }

        Json EnqueueParams(StringView set) {
            Json params = Json::object();
            params["preset"] = "wood";
            params["subjects"] = "a\nb";
            params["set"] = ToStdString(set);
            params["seed"] = kSetSeed;
            return params;
        }

    }  // namespace

    TEST_F(ArtGenSetSessionTest, EnqueueRecordsTheAtlasGroupInTheSetItsManifestAndTheState) {
        Json params = EnqueueParams("grouped");
        params["atlas"] = "pieces";
        ASSERT_EQ(ErrorCode(Call("artgen_enqueue", params)), "");
        EXPECT_EQ(session.FindSet("grouped")->atlas, "pieces");
        EXPECT_EQ(OnDisk("grouped").atlas, "pieces");

        Json setParams = Json::object();
        setParams["set"] = "grouped";
        EXPECT_EQ(*FindJsonMember(Result(Call("artgen_set", setParams)), "atlas"), Json("pieces"));
        const Json state = Result(Call("artgen_state"));
        EXPECT_EQ(*FindJsonMember((*FindJsonMember(state, "sets"))[0], "atlas"), Json("pieces"));

        ASSERT_EQ(ErrorCode(Call("artgen_enqueue", EnqueueParams("plain"))), "");
        EXPECT_TRUE(session.FindSet("plain")->atlas.empty());
        EXPECT_EQ(FindJsonMember((*FindJsonMember(Result(Call("artgen_state")), "sets"))[1], "atlas"), nullptr);
        ReloadFromDisk();
        EXPECT_EQ(session.FindSet("grouped")->atlas, "pieces") << "a reload of the sets keeps it";
    }

    TEST_F(ArtGenSetSessionTest, EnqueueTakesTheGroupOfTheSavedPresetAndAnEmptyStringClearsIt) {
        Call("artgen_poll", PollWithWoodAtlas("icons"));
        ASSERT_EQ(ErrorCode(Call("artgen_enqueue", EnqueueParams("by_preset"))), "");
        EXPECT_EQ(session.FindSet("by_preset")->atlas, "icons");

        Json cleared = EnqueueParams("cleared");
        cleared["atlas"] = "";
        ASSERT_EQ(ErrorCode(Call("artgen_enqueue", cleared)), "");
        EXPECT_TRUE(session.FindSet("cleared")->atlas.empty());

        Json other = EnqueueParams("other");
        other["atlas"] = "tiles";
        ASSERT_EQ(ErrorCode(Call("artgen_enqueue", other)), "");
        EXPECT_EQ(session.FindSet("other")->atlas, "tiles");

        Call("artgen_poll", PollWithWoodAtlas("changed"));
        EXPECT_EQ(session.FindSet("by_preset")->atlas, "icons") << "editing the preset later does not reach the set";
        Call("artgen_poll", PollWithWoodAtlas(Json()));
        ASSERT_EQ(ErrorCode(Call("artgen_enqueue", EnqueueParams("none"))), "");
        EXPECT_TRUE(session.FindSet("none")->atlas.empty());
    }

    TEST_F(ArtGenSetSessionTest, ADraftSetTakesItsOwnGroupNeverTheSavedPresets) {
        Call("artgen_poll", PollWithWoodAtlas("saved"));
        ArtGenSetRequest request;
        request.preset = "form";
        request.presetData = R"({"atlas":"drafted","preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"})";
        request.subjects = ParseArtGenSubjects("a").value();
        request.name = "with_group";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        EXPECT_EQ(session.FindSet("with_group")->atlas, "drafted");

        request.presetData = R"({"preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"})";
        request.preset = "wood";
        request.name = "without_group";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        EXPECT_TRUE(session.FindSet("without_group")->atlas.empty());
    }

    TEST_F(ArtGenSetSessionTest, ASoundSetTakesNoGroup) {
        Call("artgen_poll", PollWithWoodAtlas(Json()));
        Json params = Json::object();
        params["preset"] = "knock";
        params["subjects"] = "click";
        params["set"] = "heard";
        EXPECT_EQ(ErrorCode(Call("artgen_enqueue", params)), "") << "a group the sound preset names is ignored";
        EXPECT_TRUE(session.FindSet("heard")->atlas.empty());

        params["set"] = "refused";
        params["atlas"] = "pieces";
        const Json refused = Call("artgen_enqueue", params);
        EXPECT_EQ(ErrorCode(refused), "invalid_params");
        EXPECT_NE(ErrorMessage(refused).find("only an image set can have an atlas"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_EQ(session.FindSet("refused"), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, EnqueueRefusesABadAtlasAndKeepsNothing) {
        for (const Json& bad : {Json("a b"), Json("../x"), Json(5), Json::array(), Json(true), Json(ToStdString(String(65, 'a')))}) {
            Json params = EnqueueParams("bad");
            params["atlas"] = bad;
            EXPECT_EQ(ErrorCode(Call("artgen_enqueue", params)), "invalid_params") << DumpJson(bad).c_str();
        }
        EXPECT_TRUE(session.GetSets().empty());

        ArtGenSetRequest request;
        request.preset = "wood";
        request.subjects = ParseArtGenSubjects("a").value();
        request.atlas = "bad name";
        EXPECT_FALSE(session.EnqueueSet(request).has_value()) << "the model checks it too";
    }

    TEST_F(ArtGenSetSessionTest, ASwitchOfALiveRowOfAGroupSetPassesTheGroupAndPacksAtOnce) {
        ArtGenSetRequest request;
        request.preset = "wood";
        request.subjects = ParseArtGenSubjects("a\nb").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "grouped";
        request.atlas = "pieces";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("grouped")), "");
        gPlans.clear();

        ASSERT_EQ(session.PickVariant("grouped", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_EQ(gPlans[0].atlas, "pieces");
        EXPECT_FALSE(gPlans[0].deferPack) << "a switch repacks at once";
    }

    TEST_F(ArtGenSetSessionTest, ASwitchOfALiveRowOfASetWithoutAGroupPassesNone) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("loose", "a\nb"));
        ASSERT_EQ(session.PickVariant("loose", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_TRUE(gPlans[0].atlas.empty());
        EXPECT_FALSE(gPlans[0].deferPack);
    }

    // ---- Accept all packs the group once (Sets/CLAUDE.md, "Accept all") ----

    namespace {

        std::expected<String, String> EnqueueInGroup(ArtGenSession& session, StringView name, StringView lines, StringView group) {
            ArtGenSetRequest request;
            request.preset = "wood";
            request.subjects = ParseArtGenSubjects(lines).value();
            request.count = 2;
            request.seed = kSetSeed;
            request.name = String(name);
            request.atlas = String(group);
            return session.EnqueueSet(std::move(request));
        }

    }  // namespace

    TEST_F(ArtGenSetSessionTest, AcceptAllOfAGroupSetDefersEveryRowAndPacksOnceAfterTheLastOne) {
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb\nc", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));

        const Json answer = Result(AcceptAllCall("grouped"));
        ASSERT_EQ(gPlans.size(), 3u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_EQ(plan.atlas, "pieces");
            EXPECT_TRUE(plan.deferPack);
        }
        ASSERT_EQ(gPacks.size(), 1u);
        EXPECT_EQ(gPacks[0].group, "pieces");
        EXPECT_EQ(gPacks[0].acceptedBefore, 3u) << "after the last row";
        const Json* atlas = FindJsonMember(answer, "atlas");
        ASSERT_NE(atlas, nullptr);
        EXPECT_EQ(GetJsonString(*atlas, "name"), Optional<String>("pieces"));
        EXPECT_EQ(FindJsonMember(answer, "accepted")->size(), 3u);
        for (const ArtGenSetJob& job : OnDisk("grouped").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfASetWithoutAGroupNeverCallsThePacker) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("loose", "a\nb"));
        const Json answer = Result(AcceptAllCall("loose"));
        EXPECT_TRUE(gPacks.empty());
        ASSERT_EQ(gPlans.size(), 2u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_TRUE(plan.atlas.empty());
            EXPECT_FALSE(plan.deferPack);
        }
        EXPECT_EQ(FindJsonMember(answer, "atlas"), nullptr) << "no atlas member when none was packed";
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfAGroupSetWithNoPackerLetsEveryRowPackByItself) {
        session.SetAtlasPacker(nullptr);
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        const Json answer = Result(AcceptAllCall("grouped"));
        ASSERT_EQ(gPlans.size(), 2u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_EQ(plan.atlas, "pieces");
            EXPECT_FALSE(plan.deferPack);
        }
        EXPECT_TRUE(gPacks.empty());
        EXPECT_EQ(FindJsonMember(answer, "atlas"), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfAGroupSetPacksNothingWhenEveryRowIsKeptOrReplaced) {
        ASSERT_TRUE(EnqueueInGroup(session, "first", "a\nb", "pieces").has_value());
        ASSERT_TRUE(EnqueueInGroup(session, "second", "a", "pieces").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("first"));
        ASSERT_EQ(session.PickVariant("second", "a", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("first"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("second"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("first")), "");
        ASSERT_EQ(ErrorCode(AcceptAllCall("second")), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("first", "b"));
        gAccepted.clear();
        gPacks.clear();

        // Row a of the first set is held by the second, row b is kept: nothing is written, so nothing is packed
        const Json answer = Result(AcceptAllCall("first"));
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_TRUE(gPacks.empty());
        EXPECT_EQ(FindJsonMember(answer, "atlas"), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, AFailedRowOfAGroupSetPacksNothingAndTheNextCallFinishesAndPacksOnce) {
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb\nc", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));

        gFailOn = "c";
        const Json refused = AcceptAllCall("grouped");
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = ErrorMessage(refused);
        EXPECT_NE(message.find("accepted before it: a, b"), String::npos) << message.c_str();
        EXPECT_NE(message.find("atlas 'pieces' is not packed"), String::npos) << message.c_str();
        EXPECT_TRUE(gPacks.empty());
        const ArtGenSet disk = OnDisk("grouped");
        EXPECT_TRUE(disk.jobs[0].accepted.has_value());
        EXPECT_TRUE(disk.jobs[1].accepted.has_value());
        EXPECT_FALSE(disk.jobs[2].accepted.has_value());

        gFailOn.clear();
        const Json again = Result(AcceptAllCall("grouped"));
        ASSERT_EQ(gPacks.size(), 1u);
        EXPECT_EQ(gPacks[0].acceptedBefore, 5u) << "the first two rows replace themselves";
        EXPECT_NE(FindJsonMember(again, "atlas"), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, AFailedFirstRowOfAGroupSetSaysNothingAboutTheAtlas) {
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        gFailOn = "a";
        const Json refused = AcceptAllCall("grouped");
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        EXPECT_EQ(ErrorMessage(refused).find("atlas"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_TRUE(gPacks.empty());
    }

    TEST_F(ArtGenSetSessionTest, AFailedPackKeepsTheRowsAcceptedAndNamesTheRepair) {
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));

        gPackFail = "the atlas file is locked";
        const Json refused = AcceptAllCall("grouped");
        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = ErrorMessage(refused);
        EXPECT_NE(message.find("the atlas file is locked"), String::npos) << message.c_str();
        EXPECT_NE(message.find("'pieces' is not packed"), String::npos) << message.c_str();
        EXPECT_NE(message.find("call artgen_accept_set again"), String::npos) << message.c_str();
        EXPECT_NE(message.find("Pack in the Sprite Library"), String::npos) << message.c_str();
        ASSERT_EQ(gPacks.size(), 1u);
        EXPECT_EQ(gAccepted.size(), 2u) << "the rows are in the game";
        for (const ArtGenSetJob& job : OnDisk("grouped").jobs) {
            EXPECT_TRUE(job.accepted.has_value()) << "the manifest on disk has them accepted";
        }

        gPackFail.clear();
        const Json again = Result(AcceptAllCall("grouped"));
        EXPECT_EQ(gPacks.size(), 2u);
        EXPECT_NE(FindJsonMember(again, "atlas"), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfAGroupSetPacksInsideTheBatchAndMarksTheRowsAfterItsSave) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb\nc", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        size_t markedWhileEnding = 99;
        gDuringEnd = [this, &markedWhileEnding] {
            markedWhileEnding = 0;
            for (const ArtGenSetJob& job : OnDisk("grouped").jobs) {
                markedWhileEnding += job.accepted.has_value() ? 1 : 0;
            }
        };

        const Json answer = Result(AcceptAllCall("grouped"));

        const Vector<String> expected{"begin", "accept a", "accept b", "accept c", "pack pieces", "end"};
        EXPECT_EQ(gEvents, expected) << "the pack is a step of the batch, so the library is written once, by its end";
        EXPECT_EQ(markedWhileEnding, 0u);
        EXPECT_NE(FindJsonMember(answer, "atlas"), nullptr);
        for (const ArtGenSetJob& job : OnDisk("grouped").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, AFailedSaveAfterThePackMarksNoRowOfAGroupSet) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        gEndFails = true;

        const Json refused = AcceptAllCall("grouped");

        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const String message = ErrorMessage(refused);
        EXPECT_NE(message.find("the library was not saved"), String::npos) << message.c_str();
        const Vector<String> expected{"begin", "accept a", "accept b", "pack pieces", "end"};
        EXPECT_EQ(gEvents, expected);
        for (const ArtGenSetJob& job : OnDisk("grouped").jobs) {
            EXPECT_FALSE(job.accepted.has_value()) << "a row is marked only after the save went through";
        }
    }

    TEST_F(ArtGenSetSessionTest, AFailedPackInsideTheBatchStillSavesAndMarksTheRowsDone) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        gPackFail = "the atlas file is locked";

        const Json refused = AcceptAllCall("grouped");

        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        EXPECT_NE(ErrorMessage(refused).find("'pieces' is not packed"), String::npos) << ErrorMessage(refused).c_str();
        const Vector<String> expected{"begin", "accept a", "accept b", "pack pieces", "end"};
        EXPECT_EQ(gEvents, expected) << "the loose members are saved by the batch's end";
        for (const ArtGenSetJob& job : OnDisk("grouped").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, AFailedRowOfAGroupSetInsideTheBatchPacksNothingAndEndsTheBatch) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb\nc", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        gFailOn = "c";

        const Json refused = AcceptAllCall("grouped");

        EXPECT_EQ(ErrorCode(refused), "artgen_accept_failed");
        const Vector<String> expected{"begin", "accept a", "accept b", "end"};
        EXPECT_EQ(gEvents, expected);
        const ArtGenSet disk = OnDisk("grouped");
        EXPECT_TRUE(disk.jobs[0].accepted.has_value());
        EXPECT_TRUE(disk.jobs[1].accepted.has_value());
        EXPECT_FALSE(disk.jobs[2].accepted.has_value());
    }

    TEST_F(ArtGenSetSessionTest, ASoundSetNeverCallsThePacker) {
        Poll();  // the catalog tells the kind of the preset
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = ParseArtGenSubjects("a\nb").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "heard";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("heard", "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("heard"));
        const Json answer = Result(AcceptAllCall("heard"));
        EXPECT_TRUE(gPacks.empty());
        EXPECT_EQ(FindJsonMember(answer, "atlas"), nullptr);
    }

    // ---- one frame for the whole set (Sets/CLAUDE.md, "Accept all") ----

    TEST_F(ArtGenSetSessionTest, TheFirstAcceptAllMeasuresOnceOverEveryVariantAndStoresTheFrame) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        EXPECT_FALSE(Require(session.FindSet("framed")).processing.has_value());

        ASSERT_EQ(ErrorCode(AcceptAllCall("framed")), "");

        ASSERT_EQ(gMeasures.size(), 1u);
        ASSERT_EQ(gMeasures[0].files.size(), 4u) << "the drafts that were not picked count as much as the picks";
        for (const char* const job : {"a", "b"}) {
            for (const int64_t seed : {kSetSeed, kSetSeed + 1}) {
                const String file = Format("{}/artgen/out/framed/{}/{}_{}.png", _root.View(), job, job, seed);
                EXPECT_NE(std::find(gMeasures[0].files.begin(), gMeasures[0].files.end(), file), gMeasures[0].files.end()) << file.c_str();
            }
        }
        EXPECT_EQ(gMeasures[0].settings, ArtGenImageSettings{}) << "a preset with no accept key means the defaults";

        ASSERT_EQ(gPlans.size(), 2u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_EQ(plan.settings, ArtGenImageSettings{});
            EXPECT_EQ(plan.frame, Optional<ArtGenImageFrame>(kFakeFrame));
        }
        const Optional<ArtGenSetProcessing> expected = ArtGenSetProcessing{ArtGenImageSettings{}, kFakeFrame};
        EXPECT_EQ(Require(session.FindSet("framed")).processing, expected);
        EXPECT_EQ(OnDisk("framed").processing, expected);

        Json params = Json::object();
        params["set"] = "framed";
        const Json shown = Result(Call("artgen_set", params));
        const Json* processing = FindJsonMember(shown, "processing");
        ASSERT_NE(processing, nullptr);
        EXPECT_EQ(GetJsonInt(Require(FindJsonMember(*processing, "frame")), "outWidth"), Optional<int64_t>(150));
    }

    TEST_F(ArtGenSetSessionTest, TheFrameOfAnAcceptAllUsesTheSettingsOfTheSetsDraft) {
        ArtGenSetRequest request;
        request.preset = "form";
        request.presetData = R"({"accept":{"maxSize":128,"padding":7,"trim":true},"preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"})";
        request.subjects = Require(ParseArtGenSubjects("a"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "drafted";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("drafted");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("drafted"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("drafted")), "");

        ASSERT_EQ(gMeasures.size(), 1u);
        EXPECT_EQ(gMeasures[0].settings, (ArtGenImageSettings{true, 7, 128}));
        EXPECT_EQ(Require(session.FindSet("drafted")).processing->settings, (ArtGenImageSettings{true, 7, 128}));
    }

    TEST_F(ArtGenSetSessionTest, ASecondAcceptAllAndASwitchKeepTheFrameAndMeasureNothing) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("framed")), "");
        const Optional<ArtGenSetProcessing> stored = Require(session.FindSet("framed")).processing;
        ASSERT_TRUE(stored.has_value());
        gMeasures.clear();
        gAccepted.clear();
        gPlans.clear();

        ASSERT_EQ(ErrorCode(AcceptAllCall("framed")), "");
        EXPECT_TRUE(gMeasures.empty());
        ASSERT_EQ(gPlans.size(), 2u);
        EXPECT_EQ(gPlans[0].frame, Optional<ArtGenImageFrame>(stored->frame));
        EXPECT_EQ(gPlans[1].frame, Optional<ArtGenImageFrame>(stored->frame));

        gAccepted.clear();
        gPlans.clear();
        ASSERT_EQ(session.PickVariant("framed", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gAccepted.size(), 1u) << "a switch calls the acceptor for its row only";
        EXPECT_EQ(gAccepted[0].name, "a");
        EXPECT_EQ(gAccepted[0].seed, kSetSeed);
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_EQ(gPlans[0].settings, stored->settings);
        EXPECT_EQ(gPlans[0].frame, Optional<ArtGenImageFrame>(stored->frame));
        EXPECT_TRUE(gMeasures.empty());
        EXPECT_EQ(Require(session.FindSet("framed")).processing, stored) << "the frame never moves";
    }

    TEST_F(ArtGenSetSessionTest, AnEditOfThePresetAfterTheFirstAcceptAllDoesNotReachTheSet) {
        ASSERT_TRUE(Enqueue("framed", "a").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("framed")), "");

        Json preset = Json::object();
        preset["name"] = "wood";
        preset["title"] = "wood";
        preset["preset"] = Require(ParseJson(R"({"accept":{"trim":false,"maxSize":0}})"));
        Json poll = ReadyPoll();
        poll["presets"] = Json::array({preset});
        Call("artgen_poll", poll);

        gPlans.clear();
        ASSERT_EQ(session.PickVariant("framed", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_FALSE(gPlans[0].settings.IsIdentity());
        EXPECT_TRUE(gPlans[0].frame.has_value());
    }

    TEST_F(ArtGenSetSessionTest, TheFrameOutlivesKeepAndAReloadOfTheManifest) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("framed")), "");

        Json params = Json::object();
        params["set"] = "framed";
        params["job"] = "a";
        ASSERT_EQ(ErrorCode(Call("artgen_keep", params)), "");
        const Optional<ArtGenSetProcessing> expected = ArtGenSetProcessing{ArtGenImageSettings{}, kFakeFrame};
        EXPECT_EQ(Require(session.FindSet("framed")).processing, expected) << "Keep cuts a row's variants, the frame is the set's";
        EXPECT_EQ(OnDisk("framed").processing, expected);

        ReloadFromDisk();
        ASSERT_NE(session.FindSet("framed"), nullptr);
        EXPECT_EQ(Require(session.FindSet("framed")).processing, expected);

        // Row b is still live: its switch after the reload goes through the same frame, and nothing is measured again
        gPlans.clear();
        gMeasures.clear();
        ASSERT_EQ(session.PickVariant("framed", "b", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_EQ(gPlans[0].frame, Optional<ArtGenImageFrame>(kFakeFrame));
        EXPECT_TRUE(gMeasures.empty());
    }

    TEST_F(ArtGenSetSessionTest, ALegacySetKeepsWritingUnprocessedFilesAndStoresNoFrame) {
        ASSERT_TRUE(Enqueue("old", "a\nb").has_value());
        RunAndPickAll("old");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("old"));
        // A set accepted before frames existed: a row accepted, no processing in the manifest
        ArtGenSet disk = OnDisk("old");
        disk.jobs[0].accepted = ArtGenSetAccepted{"a", "2026-11-17T00:00:00Z", false};
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();
        ASSERT_TRUE(IsLegacyAccepted(Require(session.FindSet("old"))));

        ASSERT_EQ(ErrorCode(AcceptAllCall("old")), "");
        EXPECT_TRUE(gMeasures.empty());
        ASSERT_EQ(gPlans.size(), 2u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_TRUE(plan.settings.IsIdentity());
            EXPECT_FALSE(plan.frame.has_value());
        }
        EXPECT_FALSE(Require(session.FindSet("old")).processing.has_value());

        gPlans.clear();
        ASSERT_EQ(session.PickVariant("old", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_TRUE(gPlans[0].settings.IsIdentity());
        EXPECT_FALSE(gPlans[0].frame.has_value());
    }

    TEST_F(ArtGenSetSessionTest, SettingsThatChangeNothingNeedNoFrame) {
        ArtGenSetRequest request;
        request.preset = "form";
        request.presetData = R"({"accept":{"maxSize":0,"trim":false},"preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"})";
        request.subjects = Require(ParseArtGenSubjects("a"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "plain";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("plain");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("plain"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("plain")), "");

        EXPECT_TRUE(gMeasures.empty());
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_TRUE(gPlans[0].settings.IsIdentity());
        EXPECT_FALSE(Require(session.FindSet("plain")).processing.has_value());
    }

    TEST_F(ArtGenSetSessionTest, AFrameThatCannotBeMeasuredIsARefusalThatChangesNothing) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        gMeasureFail = "nothing drawn in any of the pictures";

        const Json response = AcceptAllCall("framed");
        EXPECT_EQ(ErrorCode(response), "artgen_accept_failed");
        EXPECT_NE(ErrorMessage(response).find("nothing drawn in any"), String::npos) << ErrorMessage(response).c_str();
        EXPECT_TRUE(gAccepted.empty());
        const ArtGenSet disk = OnDisk("framed");
        EXPECT_FALSE(disk.processing.has_value());
        EXPECT_FALSE(disk.jobs[0].accepted.has_value());

        gMeasureFail.clear();
        EXPECT_EQ(ErrorCode(AcceptAllCall("framed")), "") << "the next call measures again";
        EXPECT_TRUE(Require(session.FindSet("framed")).processing.has_value());
    }

    TEST_F(ArtGenSetSessionTest, AFailedFirstRowStoresNoFrame) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        gFailOn = "a";

        EXPECT_EQ(ErrorCode(AcceptAllCall("framed")), "artgen_accept_failed");
        EXPECT_FALSE(Require(session.FindSet("framed")).processing.has_value());
        EXPECT_FALSE(OnDisk("framed").processing.has_value());
        EXPECT_FALSE(IsLegacyAccepted(Require(session.FindSet("framed")))) << "nothing was written, so the set is not legacy";

        gFailOn.clear();
        ASSERT_EQ(ErrorCode(AcceptAllCall("framed")), "");
        EXPECT_TRUE(Require(session.FindSet("framed")).processing.has_value());
        EXPECT_EQ(gMeasures.size(), 2u);
    }

    // ---- frameOf: a replacement set takes the frame of the set it replaces a row of ----

    /// A finished set `old` with a frame of its own, as its first Accept all would have left it (no row accepted, so it is not a legacy set)
    static const ArtGenSetProcessing kOldProcessing{ArtGenImageSettings{true, 3, 200}, ArtGenImageFrame{1, 2, 30, 40, 15, 20}};

    TEST_F(ArtGenSetSessionTest, AReplacementSetTakesTheFrameOfTheSetItReplacesARowOf) {
        ASSERT_TRUE(Enqueue("old", "a\nb").has_value());
        RunAndPickAll("old");
        ArtGenSet disk = OnDisk("old");
        disk.processing = kOldProcessing;
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();

        ArtGenSetRequest request;
        request.preset = "wood";
        request.subjects = Require(ParseArtGenSubjects("a: a bigger knight"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "fix";
        request.frameOf = "old";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        EXPECT_EQ(OnDisk("fix").processing, Optional<ArtGenSetProcessing>(kOldProcessing)) << "the frame is in the manifest before any job runs";

        RunAndPickAll("fix");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("fix"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("fix")), "");
        EXPECT_TRUE(gMeasures.empty()) << "a frame that was given is not measured again";
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_EQ(gPlans[0].frame, Optional<ArtGenImageFrame>(kOldProcessing.frame));
        EXPECT_EQ(gPlans[0].settings, kOldProcessing.settings);
        EXPECT_EQ(Require(session.FindSet("fix")).processing, Optional<ArtGenSetProcessing>(kOldProcessing));
    }

    TEST_F(ArtGenSetSessionTest, FrameOfNeedsAnImageSetThatHasAFrame) {
        Poll();
        ASSERT_TRUE(Enqueue("noframe", "a").has_value());
        ArtGenSetRequest sound;
        sound.preset = "knock";
        sound.subjects = Require(ParseArtGenSubjects("click"));
        sound.name = "sounds";
        ASSERT_TRUE(session.EnqueueSet(sound).has_value());
        ASSERT_TRUE(Require(session.FindSet("sounds")).kind == kKindAudio);

        ArtGenSetRequest good;
        good.preset = "wood";
        good.subjects = Require(ParseArtGenSubjects("a"));
        good.name = "fix";
        const size_t setsBefore = session.GetSets().size();
        const auto refused = [&](StringView frameOf, StringView preset = "wood") {
            ArtGenSetRequest request = good;
            request.frameOf = String(frameOf);
            request.preset = String(preset);
            const auto result = session.EnqueueSet(request);
            EXPECT_FALSE(result.has_value()) << String(frameOf).c_str();
            return result ? String() : result.error();
        };
        EXPECT_NE(refused("missing").find("no set named"), String::npos);
        EXPECT_NE(refused("noframe").find("no frame yet"), String::npos);
        EXPECT_NE(refused("sounds").find("only an image set"), String::npos);

        ArtGenSet disk = OnDisk("noframe");
        disk.processing = kOldProcessing;
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();
        Poll();
        EXPECT_NE(refused("noframe", "knock").find("only an image set"), String::npos) << "a sound set cannot take a frame";

        EXPECT_EQ(session.GetSets().size(), setsBefore) << "a refusal queues nothing";
        EXPECT_FALSE(PathExists(_root / "artgen" / "out" / "fix"));
    }

    TEST_F(ArtGenSetSessionTest, ArtgenEnqueueReadsFrameOfAndRefusesWhatItCannotTake) {
        ASSERT_TRUE(Enqueue("old", "a").has_value());
        ArtGenSet disk = OnDisk("old");
        disk.processing = kOldProcessing;
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();

        Json params = Json::object();
        params["preset"] = "wood";
        params["subjects"] = "a";
        params["set"] = "fix";
        for (const Json& bad : {Json(3), Json(""), Json("nowhere")}) {
            params["frameOf"] = bad;
            EXPECT_EQ(ErrorCode(Call("artgen_enqueue", params)), "invalid_params") << DumpJson(bad).c_str();
        }
        EXPECT_EQ(session.FindSet("fix"), nullptr);

        params["frameOf"] = "old";
        EXPECT_EQ(ErrorCode(Call("artgen_enqueue", params)), "");
        ASSERT_NE(session.FindSet("fix"), nullptr);
        EXPECT_EQ(Require(session.FindSet("fix")).processing, Optional<ArtGenSetProcessing>(kOldProcessing));
    }

    TEST_F(ArtGenSetSessionTest, ASoundSetNeedsNoFrame) {
        session.SetFrameMeasurer(nullptr);
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = Require(ParseArtGenSubjects("click"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "sounds";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("sounds", "audio");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("sounds"));
        EXPECT_EQ(ErrorCode(AcceptAllCall("sounds")), "");
        EXPECT_TRUE(gMeasures.empty());
        EXPECT_FALSE(Require(session.FindSet("sounds")).processing.has_value());
    }

    namespace {

        constexpr StringView kStyleSource = "assets/artgen/white_knight.png";

        Json StyleReferenceJson() {
            return Json{{"mode", "style"}, {"image", ToStdString(kStyleSource)}, {"weight", 0.7}};
        }

    }  // namespace

    TEST_F(ArtGenSetSessionTest, AReferenceOfAnEnqueuedSetGoesOutWithEveryJobAndIsKeptInTheSet) {
        Json params = Json::object();
        params["preset"] = "wood";
        params["subjects"] = "a\nb";
        params["set"] = "styled";
        params["seed"] = kSetSeed;
        params["reference"] = StyleReferenceJson();
        ASSERT_EQ(ErrorCode(Call("artgen_enqueue", params)), "");

        const Json first = Poll();
        const Json* sent = RequestOf(first);
        ASSERT_NE(sent, nullptr);
        EXPECT_EQ(*FindJsonMember(*sent, "reference"), StyleReferenceJson());
        EXPECT_EQ(*FindJsonMember(*FindJsonMember(Result(Call("artgen_state")), "run"), "reference"), StyleReferenceJson()) << "the run in work shows it";
        Finish(*sent, "ok");
        const Json second = Poll();
        ASSERT_NE(RequestOf(second), nullptr);
        EXPECT_EQ(*FindJsonMember(*RequestOf(second), "reference"), StyleReferenceJson()) << "every job, not only the first";
        Finish(*RequestOf(second), "ok");

        const Json state = Result(Call("artgen_state"));
        EXPECT_EQ(*FindJsonMember((*FindJsonMember(state, "sets"))[0], "reference"), StyleReferenceJson());
        Json setParams = Json::object();
        setParams["set"] = "styled";
        EXPECT_EQ(*FindJsonMember(Result(Call("artgen_set", setParams)), "reference"), StyleReferenceJson());
        ASSERT_TRUE(OnDisk("styled").reference.has_value());
        EXPECT_EQ(OnDisk("styled").reference->image, String(kStyleSource));

        ReloadFromDisk();
        ASSERT_TRUE(session.FindSet("styled")->reference.has_value()) << "a reload of the sets keeps it";
        EXPECT_EQ(session.FindSet("styled")->reference->mode, "style");
        EXPECT_EQ(session.FindSet("styled")->reference->weight, Optional<double>(0.7));
    }

    TEST_F(ArtGenSetSessionTest, RetryAndMoreVariantsRunWithTheReferenceOfTheSet) {
        ArtGenSetRequest request;
        request.preset = "wood";
        request.subjects = ParseArtGenSubjects("a\nb").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "styled";
        request.reference = ArtGenReference{"style", String(kStyleSource), {}};
        ASSERT_TRUE(session.EnqueueSet(std::move(request)).has_value());

        const Json first = Poll();
        Finish(*RequestOf(first), "failed", "the generator fell over");
        const Json second = Poll();
        Finish(*RequestOf(second), "ok");
        ASSERT_EQ(session.RetryJobs("styled", {}), ArtGenSetEdit::Ok);
        const Json retried = Poll();
        const Json* retriedRequest = RequestOf(retried);
        ASSERT_NE(retriedRequest, nullptr);
        EXPECT_EQ(GetJsonString(*retriedRequest, "name"), Optional<String>("a"));
        const Json* retriedReference = FindJsonMember(*retriedRequest, "reference");
        ASSERT_NE(retriedReference, nullptr) << "a Retry runs with the set's reference";
        EXPECT_EQ(GetJsonString(*retriedReference, "image"), Optional<String>(String(kStyleSource)));
        EXPECT_EQ(FindJsonMember(*retriedReference, "weight"), nullptr) << "no weight: the mode's default, as the set was made";
        Finish(*retriedRequest, "ok");

        ASSERT_EQ(ErrorCode(MoreCall("styled")), "");
        const Json more = Poll();
        const Json* moreRequest = RequestOf(more);
        ASSERT_NE(moreRequest, nullptr);
        EXPECT_NE(GetJsonInt(*moreRequest, "seed"), Optional<int64_t>(kSetSeed)) << "this is the more-run";
        EXPECT_EQ(GetJsonString(*FindJsonMember(*moreRequest, "reference"), "mode"), Optional<String>("style"));
    }

    TEST_F(ArtGenSetSessionTest, ASetWithNoReferenceSendsNoneAndTheSlotIsNotTakenForOne) {
        session.SetReferenceSlot(ArtGenReference{"init", String(kStyleSource), {}});
        ASSERT_TRUE(Enqueue("plain", "a").has_value());
        EXPECT_FALSE(session.FindSet("plain")->reference.has_value()) << "only the panel passes the slot";
        const Json first = Poll();
        ASSERT_NE(RequestOf(first), nullptr);
        EXPECT_EQ(FindJsonMember(*RequestOf(first), "reference"), nullptr);
        EXPECT_EQ(FindJsonMember((*FindJsonMember(Result(Call("artgen_state")), "sets"))[0], "reference"), nullptr);
        const Json manifest = ParseJson(ReadTextFile(ManifestPath(_root, "plain")).value()).value();
        EXPECT_TRUE(FindJsonMember(manifest, "reference")->is_null());
    }

    TEST_F(ArtGenSetSessionTest, ABadReferenceOfEnqueueIsInvalidParamsThatNamesTheKey) {
        Json good = Json::object();
        good["preset"] = "wood";
        good["subjects"] = "a";
        const Json bad = Json::array({Json("yes"), Json{{"image", "a.png"}}, Json{{"mode", "style"}}, Json{{"mode", "style"}, {"image", "a.png"}, {"weight", "x"}}});
        for (const Json& reference : bad) {
            Json params = good;
            params["reference"] = reference;
            const Json response = Call("artgen_enqueue", params);
            EXPECT_EQ(ErrorCode(response), "invalid_params") << DumpJson(reference).c_str();
            EXPECT_TRUE(ErrorMessage(response).find("'reference") != String::npos) << ErrorMessage(response);
        }
        EXPECT_TRUE(session.GetSets().empty()) << "a refusal keeps nothing";
        good["reference"] = nullptr;
        EXPECT_EQ(ErrorCode(Call("artgen_enqueue", good)), "") << "a null reference is none";
    }

    TEST_F(ArtGenSetSessionTest, MoreQueuesEveryDoneRowOnTheGivenSeedAndTheRunAppendsToTheVariants) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        constexpr int64_t kMoreSeed = 5000;
        Json extra = Json::object();
        extra["count"] = 2;
        extra["seed"] = kMoreSeed;
        const Json answer = Result(MoreCall("m", extra));
        EXPECT_EQ(GetJsonString(answer, "set"), Optional<String>("m"));
        EXPECT_EQ(GetJsonInt(answer, "seed"), Optional<int64_t>(kMoreSeed));
        EXPECT_EQ(GetJsonInt(answer, "count"), Optional<int64_t>(2));
        const Json* names = FindJsonMember(answer, "jobs");
        ASSERT_NE(names, nullptr);
        ASSERT_EQ(names->size(), 2u);
        EXPECT_EQ((*names)[0], "a");
        EXPECT_EQ((*names)[1], "b");
        ASSERT_NE(FindJsonMember(answer, "skipped"), nullptr);
        EXPECT_TRUE(Require(FindJsonMember(answer, "skipped")).empty());

        EXPECT_TRUE(session.IsBusy());
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Pending);
            ASSERT_TRUE(job.more.has_value());
            EXPECT_EQ(job.more->seed, kMoreSeed);
            EXPECT_EQ(job.more->count, 2);
            EXPECT_EQ(job.variants.size(), 2u) << "nothing is dropped while the row waits";
        }
        const Json state = Result(Call("artgen_state"));
        EXPECT_EQ(GetJsonString((Require(FindJsonMember(state, "sets")))[0], "state"), Optional<String>("Active"));
        Json setParams = Json::object();
        setParams["set"] = "m";
        const Json shown = Result(Call("artgen_set", setParams));
        const Json& shownMore = (Require(FindJsonMember(shown, "jobs")))[0]["more"];
        EXPECT_EQ(GetJsonInt(shownMore, "seed"), Optional<int64_t>(kMoreSeed));
        EXPECT_EQ(GetJsonInt(shownMore, "count"), Optional<int64_t>(2));

        const Json first = Poll();
        const Json* request = RequestOf(first);
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(GetJsonInt(*request, "seed"), Optional<int64_t>(kMoreSeed));
        EXPECT_EQ(GetJsonInt(*request, "count"), Optional<int64_t>(2));
        EXPECT_EQ(GetJsonString(*request, "outDir"), Optional<String>("m/a"));
        EXPECT_EQ(GetJsonString(*request, "name"), Optional<String>("a"));
        Finish(*request, "ok");
        EXPECT_EQ(Require(session.FindSet("m")).jobs[0].state, ArtGenSetJobState::Done);
        EXPECT_FALSE(Require(session.FindSet("m")).jobs[0].more.has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());

        EXPECT_FALSE(session.IsBusy());
        const ArtGenSet disk = OnDisk("m");
        for (const ArtGenSetJob& job : disk.jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_FALSE(job.more.has_value());
            EXPECT_TRUE(job.error.empty());
            ASSERT_EQ(job.variants.size(), 4u);
            EXPECT_EQ(job.variants[0].seed, kSetSeed);
            EXPECT_EQ(job.variants[1].seed, kSetSeed + 1);
            EXPECT_EQ(job.variants[2].seed, kMoreSeed);
            EXPECT_EQ(job.variants[3].seed, kMoreSeed + 1);
            EXPECT_EQ(job.pick, Optional<int64_t>(kSetSeed + 1)) << "the pick and the game stay";
            ASSERT_TRUE(job.accepted.has_value());
            EXPECT_FALSE(job.accepted->kept);
        }
        EXPECT_TRUE(gAccepted.empty()) << "more variants write nothing into the game";
        EXPECT_EQ(GetJsonString((Require(FindJsonMember(Result(Call("artgen_state")), "sets")))[0], "state"), Optional<String>("Complete"));
    }

    TEST_F(ArtGenSetSessionTest, MoreDrawsOneFreshSeedForTheRowsOfTheCall) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb\nc"));
        const Json answer = Result(MoreCall("m"));
        EXPECT_EQ(GetJsonInt(answer, "count"), Optional<int64_t>(1)) << "the default is one variant per row";
        const int64_t seed = Require(GetJsonInt(answer, "seed"));
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            ASSERT_TRUE(job.more.has_value());
            EXPECT_EQ(job.more->seed, seed) << "one seed keeps the look of the rows together";
            EXPECT_EQ(job.more->count, 1);
            EXPECT_TRUE(ArtGenSeedRangeFree(job, seed, 1));
        }
        ASSERT_NO_FATAL_FAILURE(RunAll());
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            ASSERT_EQ(job.variants.size(), 3u);
            EXPECT_EQ(job.variants[2].seed, seed);
        }
    }

    TEST_F(ArtGenSetSessionTest, ASeedThatMeetsAVariantIsRefusedAndNothingChanges) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        const uint64_t before = session.Version();
        Json extra = Json::object();
        extra["count"] = 3;
        extra["seed"] = kSetSeed - 2;  // 38, 39, 40: meets the first variant
        const Json refused = MoreCall("m", extra);
        EXPECT_EQ(ErrorCode(refused), "invalid_params");
        EXPECT_NE(ErrorMessage(refused).find("'a'"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(session.Version(), before);
        for (const ArtGenSetJob& job : OnDisk("m").jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_FALSE(job.more.has_value());
        }

        extra["seed"] = -1;
        EXPECT_EQ(ErrorCode(MoreCall("m", extra)), "invalid_params");
        EXPECT_FALSE(session.IsBusy());
        extra["seed"] = kSetSeed - 3;  // 37, 38, 39: ends right before the first variant
        EXPECT_EQ(ErrorCode(MoreCall("m", extra)), "");
    }

    TEST_F(ArtGenSetSessionTest, AKeptRowIsLeftOutAndAnAllKeptSetIsNothingToRun) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(ErrorCode(KeepCall("m", "a")), "");
        const Json answer = Result(MoreCall("m"));
        const Json* names = FindJsonMember(answer, "jobs");
        ASSERT_NE(names, nullptr);
        ASSERT_EQ(names->size(), 1u);
        EXPECT_EQ((*names)[0], "b");
        const Json* skipped = FindJsonMember(answer, "skipped");
        ASSERT_NE(skipped, nullptr);
        ASSERT_EQ(skipped->size(), 1u);
        EXPECT_EQ(GetJsonString((*skipped)[0], "job"), Optional<String>("a"));
        EXPECT_EQ(GetJsonString((*skipped)[0], "reason"), Optional<String>("kept"));
        EXPECT_EQ(KeysOf(answer), (Vector<String>{"count", "jobs", "seed", "set", "skipped"}));
        EXPECT_EQ(KeysOf((*skipped)[0]), (Vector<String>{"job", "reason"}));
        const ArtGenSetJob* kept = FindJob(Require(session.FindSet("m")), "a");
        EXPECT_EQ(kept->state, ArtGenSetJobState::Done);
        EXPECT_FALSE(kept->more.has_value());
        EXPECT_EQ(kept->variants.size(), 1u) << "Keep trimmed it and More leaves it alone";
        ASSERT_NO_FATAL_FAILURE(RunAll());
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "a")).variants.size(), 1u);
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).variants.size(), 3u);

        ASSERT_EQ(ErrorCode(KeepCall("m")), "");
        const Json refused = MoreCall("m");
        EXPECT_EQ(ErrorCode(refused), "invalid_params");
        EXPECT_NE(ErrorMessage(refused).find("a (kept)"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_NE(ErrorMessage(refused).find("b (kept)"), String::npos) << ErrorMessage(refused).c_str();
        EXPECT_FALSE(session.IsBusy());
    }

    TEST_F(ArtGenSetSessionTest, KeepWaitsForARunningMoreRunAndNoKeptRowGrows) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(ErrorCode(MoreCall("m")), "");
        const uint64_t before = session.Version();

        EXPECT_EQ(ErrorCode(KeepCall("m")), "artgen_busy");
        EXPECT_EQ(ErrorCode(KeepCall("m", "a")), "artgen_busy");
        EXPECT_EQ(session.Version(), before);
        for (const ArtGenSetJob& job : OnDisk("m").jobs) {
            EXPECT_FALSE(job.accepted->kept);
        }

        ASSERT_NO_FATAL_FAILURE(RunAll());
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.variants.size(), 3u);
            EXPECT_FALSE(job.accepted->kept);
        }
        ASSERT_EQ(ErrorCode(KeepCall("m")), "");
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.variants.size(), 1u);
        }
    }

    TEST_F(ArtGenSetSessionTest, FailedAndCancelledRowsAreLeftOutForRetry) {
        ASSERT_TRUE(Enqueue("m", "a\nb\nc").has_value());
        Finish(*RequestOf(Poll()), "ok");
        Finish(*RequestOf(Poll()), "failed", "boom");
        session.CancelJob("m", "c");
        const Json answer = Result(MoreCall("m"));
        ASSERT_EQ(Require(FindJsonMember(answer, "jobs")).size(), 1u);
        const Json& skipped = Require(FindJsonMember(answer, "skipped"));
        ASSERT_EQ(skipped.size(), 2u);
        EXPECT_EQ(GetJsonString(skipped[0], "reason"), Optional<String>("failed"));
        EXPECT_EQ(GetJsonString(skipped[1], "reason"), Optional<String>("cancelled"));
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).state, ArtGenSetJobState::Failed);
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).error, "boom");
    }

    TEST_F(ArtGenSetSessionTest, MoreNarrowsToTheNamedRowsAndNamesTheUnknownOne) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb\nc"));
        Json extra = Json::object();
        extra["jobs"] = Json::array({"c"});
        extra["seed"] = 7000;
        const Json answer = Result(MoreCall("m", extra));
        ASSERT_EQ(Require(FindJsonMember(answer, "jobs")).size(), 1u);
        EXPECT_EQ((Require(FindJsonMember(answer, "jobs")))[0], "c");
        EXPECT_TRUE(Require(FindJsonMember(answer, "skipped")).empty()) << "a row that was not named is not a skipped one";
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "a")).state, ArtGenSetJobState::Done);
        ASSERT_NO_FATAL_FAILURE(RunAll());

        extra["jobs"] = Json::array({"c", "nope"});
        const Json unknown = MoreCall("m", extra);
        EXPECT_EQ(ErrorCode(unknown), "artgen_no_set");
        EXPECT_NE(ErrorMessage(unknown).find("nope"), String::npos);
        extra["jobs"] = Json::array();
        EXPECT_EQ(ErrorCode(MoreCall("m", extra)), "invalid_params");
        extra["jobs"] = "c";
        EXPECT_EQ(ErrorCode(MoreCall("m", extra)), "invalid_params");
        extra["jobs"] = Json::array({3});
        EXPECT_EQ(ErrorCode(MoreCall("m", extra)), "invalid_params");
        EXPECT_FALSE(session.IsBusy());
    }

    TEST_F(ArtGenSetSessionTest, MoreRefusesABadCountAnUnknownSetAndAnActiveSet) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        for (const int64_t count : {int64_t{0}, int64_t{17}, int64_t{-1}, int64_t{1} << 40}) {
            Json extra = Json::object();
            extra["count"] = count;
            EXPECT_EQ(ErrorCode(MoreCall("m", extra)), "invalid_params") << count;
        }
        Json text = Json::object();
        text["count"] = "two";
        EXPECT_EQ(ErrorCode(MoreCall("m", text)), "invalid_params");
        Json fractional = Json::object();
        fractional["count"] = 1.5;
        EXPECT_EQ(ErrorCode(MoreCall("m", fractional)), "invalid_params");
        Json badSeed = Json::object();
        badSeed["seed"] = "x";
        EXPECT_EQ(ErrorCode(MoreCall("m", badSeed)), "invalid_params");
        EXPECT_EQ(ErrorCode(MoreCall("nope")), "artgen_no_set");
        EXPECT_EQ(ErrorCode(Call("artgen_more", Json::object())), "invalid_params");
        EXPECT_FALSE(session.IsBusy());

        ASSERT_EQ(ErrorCode(MoreCall("m")), "");
        Json two = Json::object();
        two["count"] = 2;
        EXPECT_EQ(ErrorCode(MoreCall("m", two)), "artgen_busy");
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.more->count, 1) << "a refusal changes nothing";
        }
    }

    TEST_F(ArtGenSetSessionTest, ASingleRunInWorkDoesNotRefuseMoreAndTheRowsWait) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a"));
        ASSERT_TRUE(session.Request("wood", "a lone sprite", 1, 9, false).has_value());
        EXPECT_EQ(ErrorCode(MoreCall("m")), "");
        EXPECT_EQ(Require(session.FindSet("m")).jobs[0].state, ArtGenSetJobState::Pending);
    }

    TEST_F(ArtGenSetSessionTest, ACancelledMoreRunLeavesTheRowDoneWithItsVariantsAndPick) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb\nc"));
        ASSERT_EQ(ErrorCode(MoreCall("m")), "");
        const Json first = Poll();
        ASSERT_NE(RequestOf(first), nullptr);
        EXPECT_EQ(session.CancelSet("m"), ArtGenSetEdit::Ok);
        // The pending rows end at once; the running one when the service says so
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).state, ArtGenSetJobState::Done);
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).error, "cancelled");
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "a")).state, ArtGenSetJobState::Running);
        EXPECT_EQ(GetJsonInt(Poll(), "cancel"), GetJsonInt(*RequestOf(first), "id"));
        Finish(*RequestOf(first), "cancelled");

        EXPECT_FALSE(session.IsBusy());
        for (const ArtGenSetJob& job : OnDisk("m").jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_EQ(job.error, "cancelled") << "the note of the run that did not happen";
            EXPECT_FALSE(job.more.has_value());
            EXPECT_EQ(job.variants.size(), 2u);
            EXPECT_EQ(job.pick, Optional<int64_t>(kSetSeed + 1));
            EXPECT_TRUE(job.accepted.has_value());
        }
        // Retry takes only Failed and Cancelled rows, so it cannot wipe what these hold
        Json retry = Json::object();
        retry["set"] = "m";
        EXPECT_EQ(GetJsonInt(Result(Call("artgen_retry", retry)), "retried"), Optional<int64_t>(0));
        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(Require(session.FindSet("m")).jobs[0].variants.size(), 2u);
    }

    TEST_F(ArtGenSetSessionTest, ACancelOfOneRunningMoreJobLeavesTheOthersToRun) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(ErrorCode(MoreCall("m")), "");
        const Json first = Poll();
        ASSERT_NE(RequestOf(first), nullptr);
        EXPECT_EQ(session.CancelJob("m", "a"), ArtGenSetEdit::Ok);
        Poll();
        Finish(*RequestOf(first), "cancelled");
        const ArtGenSetJob* a = FindJob(Require(session.FindSet("m")), "a");
        EXPECT_EQ(a->state, ArtGenSetJobState::Done);
        EXPECT_EQ(a->variants.size(), 2u);
        EXPECT_EQ(a->pick, Optional<int64_t>(kSetSeed + 1));
        const Json next = Poll();
        ASSERT_NE(RequestOf(next), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(next), "name"), Optional<String>("b"));
    }

    TEST_F(ArtGenSetSessionTest, AFailedMoreRunKeepsTheImagesItMadeAndTheRest) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a"));
        Json extra = Json::object();
        extra["count"] = 3;
        extra["seed"] = 6000;
        ASSERT_EQ(ErrorCode(MoreCall("m", extra)), "");
        const Json* request = RequestOf(pollReply = Poll());
        ASSERT_NE(request, nullptr);
        const uint32_t id = static_cast<uint32_t>(Require(GetJsonInt(*request, "id")));
        Json image = Report(id);
        image["seed"] = 6000;
        image["path"] = ToStdString(Format("{}/artgen/out/m/a/a_6000.png", _root.View()));
        Call("artgen_image", image);
        Json done = Report(id);
        done["status"] = "failed";
        done["error"] = "out of memory";
        Call("artgen_done", done);

        const ArtGenSetJob& job = Require(session.FindSet("m")).jobs[0];
        EXPECT_EQ(job.state, ArtGenSetJobState::Done) << "a Failed row would be Retry's to wipe";
        EXPECT_EQ(job.error, "out of memory");
        EXPECT_FALSE(job.more.has_value());
        ASSERT_EQ(job.variants.size(), 3u) << "the image made before the failure stays";
        EXPECT_EQ(job.variants[2].seed, 6000);
        EXPECT_EQ(job.pick, Optional<int64_t>(kSetSeed + 1));
        EXPECT_TRUE(job.accepted.has_value());
        EXPECT_EQ(session.RetryJobs("m"), ArtGenSetEdit::Ok);
        EXPECT_EQ(Require(session.FindSet("m")).jobs[0].variants.size(), 3u);
        EXPECT_FALSE(session.IsBusy());
    }

    TEST_F(ArtGenSetSessionTest, AServiceStopLeavesTheRunningMoreRowDoneAndTheRestWait) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(ErrorCode(MoreCall("m")), "");
        ASSERT_NE(RequestOf(Poll()), nullptr);
        session.RequestServiceStop();
        EXPECT_EQ(GetJsonBool(Poll(), "shutdown"), Optional<bool>(true));
        const ArtGenSet disk = OnDisk("m");
        EXPECT_EQ(disk.jobs[0].state, ArtGenSetJobState::Done);
        EXPECT_EQ(disk.jobs[0].error, "the service was stopped");
        EXPECT_EQ(disk.jobs[0].variants.size(), 2u);
        EXPECT_EQ(disk.jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
        EXPECT_EQ(disk.jobs[1].state, ArtGenSetJobState::Pending);
        EXPECT_TRUE(disk.jobs[1].more.has_value()) << "it goes when a service polls again";
        const Json again = Poll();
        ASSERT_NE(RequestOf(again), nullptr);
        EXPECT_EQ(GetJsonString(*RequestOf(again), "name"), Optional<String>("b"));
    }

    TEST_F(ArtGenSetSessionTest, ARestartInTheMiddleOfAMoreRunShowsDoneRowsWithTheNote) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(ErrorCode(MoreCall("m")), "");
        ASSERT_NE(RequestOf(Poll()), nullptr);

        ArtGenSession restarted([this] { return now; });
        restarted.SetGameRoot(_root.View());
        const ArtGenSet* set = restarted.FindSet("m");
        ASSERT_NE(set, nullptr);
        EXPECT_FALSE(restarted.IsBusy());
        for (const ArtGenSetJob& job : set->jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_EQ(job.error, "the editor was closed");
            EXPECT_FALSE(job.more.has_value());
            EXPECT_EQ(job.variants.size(), 2u);
            EXPECT_EQ(job.pick, Optional<int64_t>(kSetSeed + 1));
        }
    }

    TEST_F(ArtGenSetSessionTest, ALiveRowSwitchesToANewVariantAfterMore) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a"));
        Json extra = Json::object();
        extra["seed"] = 8000;
        ASSERT_EQ(ErrorCode(MoreCall("m", extra)), "");
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_NO_FATAL_FAILURE(MakeFiles("m"));
        ASSERT_EQ(session.PickVariant("m", "a", 8000), ArtGenSetEdit::Ok);
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(gAccepted[0].seed, 8000);
        EXPECT_EQ(gAccepted[0].name, "a");
        EXPECT_EQ(OnDisk("m").jobs[0].pick, Optional<int64_t>(8000));
    }

    TEST_F(ArtGenSetSessionTest, TheFirstAcceptAllAfterMoreMeasuresTheNewVariantsToo) {
        ASSERT_TRUE(Enqueue("m", "a\nb").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("m"));
        ASSERT_EQ(ErrorCode(MoreCall("m")), "");
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_NO_FATAL_FAILURE(MakeFiles("m"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("m")), "");
        ASSERT_EQ(gMeasures.size(), 1u);
        EXPECT_EQ(gMeasures[0].files.size(), 6u) << "two rows of two old and one new variant";
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].seed, kSetSeed + 1) << "the pick, not a new variant";
        EXPECT_EQ(gAccepted[1].seed, kSetSeed + 1);
    }

    TEST_F(ArtGenSetSessionTest, AFullRowIsLeftOutWhileTheOthersGrow) {
        ASSERT_TRUE(Enqueue("m", "a\nb", kArtGenSetMaxCount).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        Json grow = Json::object();
        grow["count"] = kArtGenSetMaxCount;
        grow["jobs"] = Json::array({"a"});
        for (int round = 0; round < 3; ++round) {
            ASSERT_EQ(ErrorCode(MoreCall("m", grow)), "") << round;
            ASSERT_NO_FATAL_FAILURE(RunAll());
        }
        EXPECT_EQ(static_cast<int32_t>(Require(FindJob(Require(session.FindSet("m")), "a")).variants.size()), kArtGenJobMaxVariants);

        const Json answer = Result(MoreCall("m"));
        ASSERT_EQ(Require(FindJsonMember(answer, "jobs")).size(), 1u);
        EXPECT_EQ((Require(FindJsonMember(answer, "jobs")))[0], "b");
        const Json& skipped = Require(FindJsonMember(answer, "skipped"));
        ASSERT_EQ(skipped.size(), 1u);
        EXPECT_EQ(GetJsonString(skipped[0], "job"), Optional<String>("a"));
        EXPECT_EQ(GetJsonString(skipped[0], "reason"), Optional<String>("full"));
    }

    // The service regenerates what failed its checks: a job asked for `count` images may come back with more, on the seeds after its own

    TEST_F(ArtGenSetSessionTest, AJobThatGetsMoreImagesThanItsCountEndsDoneWithAllOfThem) {
        ASSERT_TRUE(Enqueue("m", "a", 2).has_value());
        const Json* request = RequestOf(pollReply = Poll());
        ASSERT_NE(request, nullptr);
        ASSERT_EQ(GetJsonInt(*request, "count"), Optional<int64_t>(2));
        FinishWithImages(*request, 4);

        EXPECT_FALSE(session.IsBusy());
        for (const ArtGenSet& set : {*session.FindSet("m"), OnDisk("m")}) {
            const ArtGenSetJob& job = set.jobs[0];
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_TRUE(job.error.empty());
            ASSERT_EQ(job.variants.size(), 4u);
            for (size_t i = 0; i < job.variants.size(); ++i) {
                EXPECT_EQ(job.variants[i].seed, kSetSeed + static_cast<int64_t>(i));
                EXPECT_EQ(job.variants[i].file, Format("a/a_{}.png", kSetSeed + static_cast<int64_t>(i)));
            }
        }
    }

    TEST_F(ArtGenSetSessionTest, MoreAfterExtraImagesKeepsOffTheirSeeds) {
        ASSERT_TRUE(Enqueue("m", "a", 2).has_value());
        FinishWithImages(*RequestOf(pollReply = Poll()), 4);  // seeds 40..43, the last two are extras
        const ArtGenSetJob& job = session.FindSet("m")->jobs[0];
        ASSERT_EQ(job.variants.size(), 4u);

        // An explicit seed whose range meets an extra is refused and changes nothing
        const Pair<int64_t, int64_t> meeting[] = {{42, 1}, {43, 2}, {41, 3}, {39, 5}};
        for (const auto& [seed, count] : meeting) {
            Json extra = Json::object();
            extra["seed"] = seed;
            extra["count"] = count;
            EXPECT_EQ(ErrorCode(MoreCall("m", extra)), "invalid_params") << seed << " x " << count;
            EXPECT_FALSE(session.IsBusy());
            EXPECT_FALSE(job.more.has_value());
            EXPECT_EQ(job.variants.size(), 4u);
        }

        // One that does not is queued
        Json clear = Json::object();
        clear["seed"] = 44;
        clear["count"] = 2;
        ASSERT_EQ(ErrorCode(MoreCall("m", clear)), "");
        EXPECT_TRUE(job.more.has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_EQ(job.variants.size(), 6u);
        EXPECT_EQ(job.variants[4].seed, 44);
        EXPECT_EQ(job.variants[5].seed, 45);

        // A drawn seed meets no variant the row holds, the extras among them
        for (int round = 0; round < 5; ++round) {
            Json two = Json::object();
            two["count"] = 2;
            const Json answer = Result(MoreCall("m", two));
            const Optional<int64_t> seed = GetJsonInt(answer, "seed");
            ASSERT_TRUE(seed.has_value()) << round;
            EXPECT_TRUE(ArtGenSeedRangeFree(job, *seed, 2)) << "round " << round << " drew " << *seed;
            ASSERT_NO_FATAL_FAILURE(RunAll());
        }
        EXPECT_EQ(job.variants.size(), 16u);
    }

    TEST_F(ArtGenSetSessionTest, ASoundSetGetsMoreVariantsToo) {
        Poll();  // the catalog tells the kind of the preset
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = Require(ParseArtGenSubjects("knock: a wooden knock"));
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "sounds";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("sounds", "audio"));
        ASSERT_EQ(ErrorCode(MoreCall("sounds")), "");
        ASSERT_NO_FATAL_FAILURE(RunAll("audio"));
        const ArtGenSetJob& job = Require(session.FindSet("sounds")).jobs[0];
        ASSERT_EQ(job.variants.size(), 3u);
        EXPECT_EQ(job.variants[2].kind, "audio");
        EXPECT_NE(job.variants[2].file.find(".flac"), String::npos);
        EXPECT_EQ(job.pick, Optional<int64_t>(kSetSeed + 1));
    }

    TEST_F(ArtGenSetSessionTest, ADroppedVariantOfAFramedSetIsWrittenThroughTheSetsFrameAndAtlas) {
        ArtGenSetRequest request;
        request.preset = "wood";
        request.subjects = ParseArtGenSubjects("a\nb").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "framed";
        request.atlas = String("pieces");
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("framed"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        ASSERT_EQ(ErrorCode(AcceptAllCall("framed")), "");
        gAccepted.clear();
        gPlans.clear();
        const ArtGenSet before = OnDisk("framed");
        ASSERT_TRUE(before.processing.has_value());

        const auto answer = session.AcceptSetVariant("framed", "a", kSetSeed, "queen");
        ASSERT_TRUE(answer.has_value()) << answer.error().message.c_str();
        EXPECT_EQ(answer->sprite, Optional<String>("queen"));
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(gAccepted[0].name, "queen");
        EXPECT_EQ(gAccepted[0].seed, kSetSeed) << "the variant dropped, not the row's pick";
        EXPECT_EQ(gAccepted[0].path, Format("{}/artgen/out/framed/a/a_{}.png", _root.View(), kSetSeed));
        EXPECT_FALSE(gAccepted[0].withNode);
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_EQ(gPlans[0].frame, Optional<ArtGenImageFrame>(kFakeFrame)) << "a pawn stays smaller than a king";
        EXPECT_EQ(gPlans[0].settings, before.processing->settings);
        EXPECT_EQ(gPlans[0].atlas, "pieces");
        EXPECT_FALSE(gPlans[0].deferPack);

        EXPECT_EQ(ManifestJson(OnDisk("framed")), ManifestJson(before)) << "no pick, no accepted, nothing in the manifest";
        EXPECT_EQ(session.FindSet("framed")->jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
        EXPECT_EQ(session.FindSet("framed")->jobs[0].accepted->as, "a");
    }

    TEST_F(ArtGenSetSessionTest, ADroppedVariantOfASetNotAcceptedYetUsesThePresetsSettingsAndAFrameOfItsOwn) {
        ArtGenSetRequest request;
        request.preset = "form";
        request.presetData = R"({"accept":{"maxSize":128,"padding":7,"trim":true},"preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"})";
        request.subjects = ParseArtGenSubjects("a").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "fresh";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_NO_FATAL_FAILURE(MakeFiles("fresh"));

        const auto answer = session.AcceptSetVariant("fresh", "a", kSetSeed + 1, "rook");
        ASSERT_TRUE(answer.has_value()) << answer.error().message.c_str();
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_EQ(gPlans[0].settings, (ArtGenImageSettings{true, 7, 128}));
        EXPECT_FALSE(gPlans[0].frame.has_value());
        EXPECT_TRUE(gPlans[0].atlas.empty());
        EXPECT_TRUE(gMeasures.empty()) << "a frame is measured by the first Accept all only";
        EXPECT_FALSE(session.FindSet("fresh")->processing.has_value());
        EXPECT_FALSE(session.FindSet("fresh")->jobs[0].accepted.has_value());
        EXPECT_FALSE(session.FindSet("fresh")->jobs[0].pick.has_value());
    }

    TEST_F(ArtGenSetSessionTest, ADroppedVariantOfALegacySetKeepsTheIdentityPlanEvenWhenThePresetTrims) {
        ArtGenSetRequest request;
        request.preset = "form";
        request.presetData = R"({"accept":{"maxSize":128,"padding":7,"trim":true},"preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"})";
        request.subjects = ParseArtGenSubjects("a\nb").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "old";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        RunAndPickAll("old");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("old"));
        ArtGenSet disk = OnDisk("old");
        disk.jobs[0].accepted = ArtGenSetAccepted{"a", "2026-11-17T00:00:00Z", false};
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();
        ASSERT_TRUE(IsLegacyAccepted(*session.FindSet("old")));
        gPlans.clear();

        const auto answer = session.AcceptSetVariant("old", "b", kSetSeed + 1, "rook");

        ASSERT_TRUE(answer.has_value()) << answer.error().message.c_str();
        ASSERT_EQ(gPlans.size(), 1u);
        EXPECT_TRUE(gPlans[0].settings.IsIdentity()) << "the rows of a legacy set are full frames: a trimmed one would differ in scale";
        EXPECT_FALSE(gPlans[0].frame.has_value());
        EXPECT_TRUE(gMeasures.empty());
    }

    TEST_F(ArtGenSetSessionTest, ADroppedVariantNamesWhatIsWrongAndWritesNothing) {
        ASSERT_TRUE(Enqueue("bad", "a").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_NO_FATAL_FAILURE(MakeFiles("bad"));

        const auto code = [&](StringView set, StringView job, int64_t seed, StringView sprite) {
            const auto answer = session.AcceptSetVariant(set, job, seed, sprite);
            return answer ? String() : answer.error().code;
        };
        EXPECT_EQ(code("nope", "a", kSetSeed, "x"), "artgen_no_set");
        EXPECT_EQ(code("bad", "nope", kSetSeed, "x"), "artgen_no_set");
        EXPECT_EQ(code("bad", "a", 999, "x"), "artgen_no_variant");
        EXPECT_EQ(code("bad", "a", kSetSeed, "not a name"), "artgen_accept_failed");
        EXPECT_EQ(code("bad", "a", kSetSeed, ""), "artgen_accept_failed");
        EXPECT_TRUE(gAccepted.empty());

        RemoveFileIfPresent(AbsoluteInSet(_root, "bad", "a/a_40.png"));
        const auto gone = session.AcceptSetVariant("bad", "a", kSetSeed, "x");
        ASSERT_FALSE(gone.has_value());
        EXPECT_EQ(gone.error().code, "artgen_accept_failed");
        EXPECT_NE(gone.error().message.find("is gone"), String::npos) << gone.error().message.c_str();
        EXPECT_TRUE(gAccepted.empty()) << "the files are checked before the acceptor is asked";

        gFailOn = "x";
        EXPECT_EQ(code("bad", "a", kSetSeed + 1, "x"), "artgen_accept_failed") << "the acceptor's refusal is passed on";
        gFailOn.clear();

        session.SetAcceptor(nullptr);
        EXPECT_EQ(code("bad", "a", kSetSeed + 1, "x"), "artgen_accept_failed");
        EXPECT_TRUE(gAccepted.empty());
    }

    TEST_F(ArtGenSetSessionTest, ADroppedVariantOfASoundSetIsRefused) {
        Poll();  // the catalog tells the kind of the preset
        ArtGenSetRequest request;
        request.preset = "knock";
        request.subjects = ParseArtGenSubjects("knock: a wooden knock").value();
        request.count = 2;
        request.seed = kSetSeed;
        request.name = "sounds";
        ASSERT_TRUE(session.EnqueueSet(request).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll("audio"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("sounds"));

        const auto answer = session.AcceptSetVariant("sounds", "knock", kSetSeed, "x");
        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().code, "artgen_accept_failed");
        EXPECT_NE(answer.error().message.find("not a sprite"), String::npos) << answer.error().message.c_str();
        EXPECT_TRUE(gAccepted.empty());
    }

}  // namespace BECore::Tests
