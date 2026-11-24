#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/File/File.h>
#include <Common/ControlTestSupport.h>
#include <Common/JsonDoc.h>
#include <Common/JsonEdit.h>
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

        /// The manifest `WriteArtGenSet` writes: the text of an answer's set member and of a file on disk, compared as bytes
        String ManifestText(const ArtGenSet& set) {
            String text;
            JsonWriter writer(text);
            WriteArtGenSet(writer, set);
            return text;
        }

        /// The JSON text of the string @p text, quoted and escaped: a member of a request that a test builds with `JsonWith`
        String Str(StringView text) {
            String written;
            JsonWriter writer(written);
            writer.Value(text);
            return written;
        }

        constexpr StringView kReadyPoll = R"({"generator":"fake","presets":[{"name":"wood","title":"wood"},{"kind":"audio","name":"knock","title":"knock"}],"state":"ready"})";

        /// A report of the service about the run @p id with the members @p members (`"seed":1,"path":"x.png"`)
        String Report(uint32_t id, StringView members = {}) {
            return members.empty() ? Format(R"({{"id":{}}})", id) : Format(R"({{"id":{},{}}})", id, members);
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

        /// The member names of a JSON object in the order they are written: the generated writer writes them in byte order by design
        Vector<String> KeysOf(JsonView object) {
            Vector<String> keys;
            for (const JsonMember member : object.Members()) {
                keys.push_back(String(member.key.Text().value_or(StringView())));
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

        /// One control call through the service, as the bridge makes it; the answer, empty when none came
        ControlReply Call(StringView method, StringView params = "{}") {
            ControlReply response;
            bool answered = false;
            service.Post(MakeRequestText(++_nextId, method, params), [&](const String& reply) {
                response = ControlReply(StringView(reply.c_str(), reply.size()));
                answered = true;
            });
            for (int frame = 0; !answered && frame < kFrameGuard; ++frame) {
                service.Drain();
                service.EndFrame();
            }
            return response;
        }

        ControlReply Call(StringView method, const char* params) {
            return Call(method, StringView(params));
        }

        ControlReply Call(StringView method, const String& params) {
            return Call(method, StringView(params.c_str(), params.size()));
        }

        /// A poll of the service: its whole answer, the `result` of it is what the service is told
        ControlReply Poll() {
            return Call("artgen_poll", kReadyPoll);
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

        /// The job a poll handed over: its request, a view that is not valid when none
        static JsonView RequestOf(const ControlReply& poll) {
            return MemberOf(poll.Result(), "request");
        }

        /// What the service does for the request it got: an image per seed, then the end
        void Finish(JsonView request, StringView status, StringView error = {}, StringView kind = "image") {
            const uint32_t id = static_cast<uint32_t>(Require(IntOf(request, "id")));
            const int64_t seed = Require(IntOf(request, "seed"));
            const String outDir = StringOf(request, "outDir");
            const String name = StringOf(request, "name");
            const StringView extension = kind == "audio" ? "flac" : "png";
            if (status == "ok") {
                const int64_t count = Require(IntOf(request, "count"));
                for (int64_t i = 0; i < count; ++i) {
                    const String path = Format("{}/artgen/out/{}/{}_{}.{}", _root.View(), outDir, name, seed + i, extension);
                    const String provenance = Format("{}/artgen/out/{}/{}_{}.artgen.json", _root.View(), outDir, name, seed + i);
                    Call("artgen_image", Report(id, Format(R"("kind":{},"path":{},"provenance":{},"seed":{})", Str(kind), Str(path), Str(provenance), seed + i)));
                }
            }
            String done = Report(id, Format(R"("status":{})", Str(status)));
            if (!error.empty()) {
                done = JsonWith(done, "/error", Str(error));
            }
            Call("artgen_done", done);
        }

        /// What the service does when it makes @p images images for a request that asked for fewer (extra images of a regenerating run): seeds from the request's, then the end
        void FinishWithImages(JsonView request, int64_t images) {
            const uint32_t id = static_cast<uint32_t>(Require(IntOf(request, "id")));
            const int64_t seed = Require(IntOf(request, "seed"));
            const String outDir = StringOf(request, "outDir");
            const String name = StringOf(request, "name");
            for (int64_t i = 0; i < images; ++i) {
                const String path = Format("{}/artgen/out/{}/{}_{}.png", _root.View(), outDir, name, seed + i);
                const String provenance = Format("{}/artgen/out/{}/{}_{}.artgen.json", _root.View(), outDir, name, seed + i);
                Call("artgen_image", Report(id, Format(R"("path":{},"provenance":{},"seed":{})", Str(path), Str(provenance), seed + i)));
            }
            Call("artgen_done", Report(id, R"("status":"ok")"));
        }

        /// Runs every job of the set to its end (the service made all the images) and picks the second variant of each
        void RunAndPickAll(StringView set, StringView kind = "image") {
            while (RequestOf(pollReply = Poll()).IsValid()) {
                Finish(RequestOf(pollReply), "ok", {}, kind);
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
            while (RequestOf(pollReply = Poll()).IsValid()) {
                Finish(RequestOf(pollReply), "ok", {}, kind);
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
            ASSERT_EQ(AcceptAllCall("first").ErrorCode(), "");
            ASSERT_EQ(AcceptAllCall("second").ErrorCode(), "");
            gAccepted.clear();
            gKept.clear();
        }

        ControlReply AcceptAllCall(StringView set) {
            return Call("artgen_accept_set", Format(R"({{"set":{}}})", Str(set)));
        }

        /// `artgen_more` of @p set with the members of @p extra (count, seed, jobs: the text of an object)
        ControlReply MoreCall(StringView set, StringView extra = "{}") {
            return Call("artgen_more", JsonWith(extra, "/set", Str(set)));
        }

        /// `artgen_keep` of @p set, or of one job of it
        ControlReply KeepCall(StringView set, StringView job = {}) {
            return Call("artgen_keep", job.empty() ? Format(R"({{"set":{}}})", Str(set)) : Format(R"({{"job":{},"set":{}}})", Str(job), Str(set)));
        }

        /// A finished set accepted into the game: every row picked on its second variant (seed kSetSeed + 1) and live, files made
        void FinishAndAccept(StringView set, StringView lines, int32_t count = 2) {
            ASSERT_TRUE(Enqueue(set, lines, count).has_value());
            ASSERT_NO_FATAL_FAILURE(RunAndPickAll(set));
            ASSERT_NO_FATAL_FAILURE(MakeFiles(set));
            ASSERT_EQ(AcceptAllCall(set).ErrorCode(), "");
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

        ControlReply pollReply;
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

        const ControlReply first = Poll();
        JsonView request = RequestOf(first);
        ASSERT_TRUE(request.IsValid());
        EXPECT_EQ(StringOf(request, "outDir"), "pieces/rook");
        EXPECT_EQ(StringOf(request, "name"), "rook");
        EXPECT_EQ(StringOf(request, "subject"), "a rook");
        EXPECT_EQ(IntOf(request, "seed"), Optional<int64_t>(kSetSeed));
        EXPECT_EQ(IntOf(request, "count"), Optional<int64_t>(2));
        EXPECT_EQ(StateOf(OnDisk("pieces"), 0), ArtGenSetJobState::Running);
        EXPECT_EQ(StateOf(OnDisk("pieces"), 1), ArtGenSetJobState::Pending);

        // One job at a time: a poll while it runs hands over nothing
        EXPECT_FALSE(RequestOf(Poll()).IsValid());

        Finish(request, "ok");
        {
            const ArtGenSet disk = OnDisk("pieces");
            EXPECT_EQ(StateOf(disk, 0), ArtGenSetJobState::Done);
            ASSERT_EQ(disk.jobs[0].variants.size(), 2u);
            EXPECT_EQ(disk.jobs[0].variants[1].seed, kSetSeed + 1);
            EXPECT_EQ(disk.jobs[0].variants[1].file, "rook/rook_41.png");
            EXPECT_EQ(disk.jobs[0].variants[1].provenance, "rook/rook_41.artgen.json");
        }

        const ControlReply second = Poll();
        request = RequestOf(second);
        ASSERT_TRUE(request.IsValid());
        EXPECT_EQ(StringOf(request, "outDir"), "pieces/knight");
        EXPECT_EQ(IntOf(request, "seed"), Optional<int64_t>(kSetSeed));
        Finish(request, "ok");

        const ControlReply third = Poll();
        request = RequestOf(third);
        ASSERT_TRUE(request.IsValid());
        EXPECT_EQ(StringOf(request, "outDir"), "pieces/bishop");
        Finish(request, "ok");

        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(CountJobs(Require(session.FindSet("pieces")), ArtGenSetJobState::Done), 3);
        EXPECT_FALSE(RequestOf(Poll()).IsValid());
    }

    TEST_F(ArtGenSetSessionTest, TheSetDrawsASharedSeedWhenNoneIsGiven) {
        ASSERT_TRUE(Enqueue("drawn", "a\nb", 1, Optional<int64_t>()).has_value());
        const ArtGenSet& set = Require(session.FindSet("drawn"));
        EXPECT_GE(set.seed, 0);
        EXPECT_EQ(OnDisk("drawn").seed, set.seed);
        const ControlReply first = Poll();
        EXPECT_EQ(IntOf(RequestOf(first), "seed"), Optional<int64_t>(set.seed));
        Finish(RequestOf(first), "ok");
        const ControlReply second = Poll();
        EXPECT_EQ(IntOf(RequestOf(second), "seed"), Optional<int64_t>(set.seed));
    }

    TEST_F(ArtGenSetSessionTest, AFailedMiddleJobDoesNotStopTheSet) {
        ASSERT_TRUE(Enqueue("fails", "a\nb\nc").has_value());
        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "failed", "out of memory");
        {
            const ArtGenSet disk = OnDisk("fails");
            EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Failed);
            EXPECT_EQ(disk.jobs[1].error, "out of memory");
        }
        const ControlReply third = Poll();
        ASSERT_TRUE(RequestOf(third).IsValid());
        EXPECT_EQ(StringOf(RequestOf(third), "name"), "c");
        Finish(RequestOf(third), "ok");
        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(StateOf(OnDisk("fails"), 2), ArtGenSetJobState::Done);
    }

    TEST_F(ArtGenSetSessionTest, ACancelledPendingJobIsSkippedAtOnce) {
        ASSERT_TRUE(Enqueue("skip", "a\nb\nc").has_value());
        EXPECT_EQ(session.CancelJob("skip", "b"), ArtGenSetEdit::Ok);
        EXPECT_EQ(StateOf(OnDisk("skip"), 1), ArtGenSetJobState::Cancelled);
        EXPECT_EQ(OnDisk("skip").jobs[1].error, "cancelled");
        Finish(RequestOf(Poll()), "ok");
        const ControlReply next = Poll();
        EXPECT_EQ(StringOf(RequestOf(next), "name"), "c");
        EXPECT_EQ(session.CancelJob("skip", "nothing"), ArtGenSetEdit::NoSet);
        EXPECT_EQ(session.CancelJob("nothing", "a"), ArtGenSetEdit::NoSet);
    }

    TEST_F(ArtGenSetSessionTest, CancellingTheRunningJobReachesThePollAndTheSetGoesOn) {
        ASSERT_TRUE(Enqueue("running", "a\nb").has_value());
        const ControlReply first = Poll();
        ASSERT_TRUE(RequestOf(first).IsValid());
        EXPECT_EQ(session.CancelJob("running", "a"), ArtGenSetEdit::Ok);
        // The job ends when the service says so, not before
        EXPECT_EQ(StateOf(OnDisk("running"), 0), ArtGenSetJobState::Running);
        const ControlReply cancel = Poll();
        EXPECT_EQ(IntOf(cancel.Result(), "cancel"), IntOf(RequestOf(first), "id"));
        Finish(RequestOf(first), "cancelled");
        EXPECT_EQ(StateOf(OnDisk("running"), 0), ArtGenSetJobState::Cancelled);
        const ControlReply next = Poll();
        ASSERT_TRUE(RequestOf(next).IsValid());
        EXPECT_EQ(StringOf(RequestOf(next), "name"), "b");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenCancelWithoutParamsCancelsOnlyTheRunningJob) {
        ASSERT_TRUE(Enqueue("plain", "a\nb").has_value());
        const ControlReply first = Poll();
        Call("artgen_cancel");
        Poll();
        Finish(RequestOf(first), "cancelled");
        const ControlReply next = Poll();
        ASSERT_TRUE(RequestOf(next).IsValid());
        EXPECT_EQ(StringOf(RequestOf(next), "name"), "b");
    }

    TEST_F(ArtGenSetSessionTest, CancellingASetStopsEverythingOfIt) {
        ASSERT_TRUE(Enqueue("all", "a\nb\nc").has_value());
        ASSERT_TRUE(Enqueue("later", "x").has_value());
        const ControlReply first = Poll();
        EXPECT_EQ(session.CancelSet("all"), ArtGenSetEdit::Ok);
        {
            const ArtGenSet disk = OnDisk("all");
            EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Cancelled);
            EXPECT_EQ(StateOf(disk, 2), ArtGenSetJobState::Cancelled);
        }
        Poll();
        Finish(RequestOf(first), "cancelled");
        EXPECT_FALSE(IsActive(Require(session.FindSet("all"))));
        // The next set in line starts
        const ControlReply next = Poll();
        ASSERT_TRUE(RequestOf(next).IsValid());
        EXPECT_EQ(StringOf(RequestOf(next), "outDir"), "later/x");
        EXPECT_EQ(session.CancelSet("nothing"), ArtGenSetEdit::NoSet);
    }

    TEST_F(ArtGenSetSessionTest, CancellingARunNobodyPolledYetNeedsNoService) {
        ASSERT_TRUE(Enqueue("queued", "a\nb\nc").has_value());
        Finish(RequestOf(Poll()), "ok");
        // The end of the first job starts the next as a queued run that the service has not seen yet
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Queued);
        EXPECT_EQ(session.GetRun().jobName, "b");
        EXPECT_EQ(session.CancelJob("queued", "b"), ArtGenSetEdit::Ok);
        EXPECT_EQ(StateOf(OnDisk("queued"), 1), ArtGenSetJobState::Cancelled);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Queued);
        EXPECT_EQ(session.GetRun().jobName, "c");
        EXPECT_EQ(StringOf(RequestOf(Poll()), "name"), "c");
    }

    TEST_F(ArtGenSetSessionTest, RetryPutsFailedAndCancelledJobsBackAndClearsTheEarlierTry) {
        ASSERT_TRUE(Enqueue("retry", "a\nb\nc").has_value());
        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "failed", "boom");
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

        const ControlReply next = Poll();
        ASSERT_TRUE(RequestOf(next).IsValid());
        EXPECT_EQ(StringOf(RequestOf(next), "name"), "b");
        EXPECT_EQ(session.RetryJobs("retry", "nothing"), ArtGenSetEdit::NoSet);
    }

    TEST_F(ArtGenSetSessionTest, ARetryOfAnOlderSetWaitsBehindTheSetsQueuedBefore) {
        ASSERT_TRUE(Enqueue("old", "a").has_value());
        Finish(RequestOf(Poll()), "failed", "boom");
        ASSERT_TRUE(Enqueue("newer", "x").has_value());
        session.RetryJobs("old");
        const ControlReply next = Poll();
        EXPECT_EQ(StringOf(RequestOf(next), "outDir"), "newer/x");
        Finish(RequestOf(next), "ok");
        EXPECT_EQ(StringOf(RequestOf(Poll()), "outDir"), "old/a");
    }

    TEST_F(ArtGenSetSessionTest, OrdinaryRunsAreRefusedWhileASetIsActive) {
        ASSERT_TRUE(Enqueue("busy", "a").has_value());
        EXPECT_TRUE(session.IsBusy());
        EXPECT_FALSE(session.Request("wood", "queen", 1, 1).has_value());
        EXPECT_FALSE(session.RequestSetup(ArtGenSetupAction::Plan).has_value());
        EXPECT_EQ(Call("artgen_generate", R"({"preset":"wood","subject":"queen"})").ErrorCode(), "artgen_busy");

        Finish(RequestOf(Poll()), "ok");
        EXPECT_FALSE(session.IsBusy());
        EXPECT_TRUE(session.Request("wood", "queen", 1, 1).has_value());
    }

    TEST_F(ArtGenSetSessionTest, JobsWaitWhileNoServicePolls) {
        ASSERT_TRUE(Enqueue("wait", "a\nb").has_value());
        session.PumpSets();
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Idle);
        EXPECT_EQ(StateOf(Require(session.FindSet("wait")), 0), ArtGenSetJobState::Pending);

        const ControlReply first = Poll();
        ASSERT_TRUE(RequestOf(first).IsValid());
        // The service is silent past the timeout when the job ends: the next one waits for a poll
        now += ArtGenSession::kServiceTimeoutSeconds + 1.0;
        Finish(RequestOf(first), "ok");
        EXPECT_EQ(StateOf(Require(session.FindSet("wait")), 1), ArtGenSetJobState::Pending);
        EXPECT_EQ(StringOf(RequestOf(Poll()), "name"), "b");
    }

    TEST_F(ArtGenSetSessionTest, DisconnectCancelsTheRunningJobAndLeavesTheRestPending) {
        ASSERT_TRUE(Enqueue("bye", "a\nb\nc").has_value());
        ASSERT_TRUE(RequestOf(Poll()).IsValid());
        session.RequestServiceStop();
        const ControlReply answer = Poll();
        EXPECT_EQ(BoolOf(answer.Result(), "shutdown"), Optional<bool>(true));
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
        const ControlReply again = Poll();
        ASSERT_TRUE(RequestOf(again).IsValid());
        EXPECT_EQ(StringOf(RequestOf(again), "name"), "b");
    }

    TEST_F(ArtGenSetSessionTest, ARestartShowsTheInterruptedJobsCancelled) {
        ASSERT_TRUE(Enqueue("crash", "a\nb\nc").has_value());
        Finish(RequestOf(Poll()), "ok");
        ASSERT_TRUE(RequestOf(Poll()).IsValid());

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
        ASSERT_TRUE(RequestOf(Poll()).IsValid());
        // The panel calls this at every switch of the run mode; a reload would turn the running job into Cancelled
        session.SetGameRoot(_root.View());
        EXPECT_EQ(StateOf(Require(session.FindSet("keep")), 0), ArtGenSetJobState::Running);
    }

    TEST_F(ArtGenSetSessionTest, AReportOfAnOlderRunIsStale) {
        ASSERT_TRUE(Enqueue("stale", "a\nb").has_value());
        const ControlReply first = Poll();
        const uint32_t firstId = static_cast<uint32_t>(Require(IntOf(RequestOf(first), "id")));
        Finish(RequestOf(first), "ok");
        const ControlReply second = Poll();
        ASSERT_TRUE(RequestOf(second).IsValid());

        EXPECT_EQ(BoolOf(Call("artgen_image", Report(firstId, R"("path":"x.png","seed":99)")).Result(), "stale"), Optional<bool>(true));
        EXPECT_EQ(BoolOf(Call("artgen_done", Report(firstId, R"("error":"late","status":"failed")")).Result(), "stale"), Optional<bool>(true));

        const ArtGenSet disk = OnDisk("stale");
        EXPECT_EQ(disk.jobs[0].variants.size(), 2u);
        EXPECT_EQ(StateOf(disk, 0), ArtGenSetJobState::Done);
        EXPECT_EQ(StateOf(disk, 1), ArtGenSetJobState::Running);
        EXPECT_TRUE(disk.jobs[1].variants.empty());
    }

    TEST_F(ArtGenSetSessionTest, APickIsKeptInTheManifest) {
        ASSERT_TRUE(Enqueue("pick", "a").has_value());
        Finish(RequestOf(Poll()), "ok");
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
        Call("artgen_poll", JsonWith(kReadyPoll, "/presets/-", R"({"error":"no workflow","name":"broken"})"));
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
        const ControlReply first = Poll();
        const JsonView sent = RequestOf(first);
        ASSERT_TRUE(sent.IsValid());
        EXPECT_TRUE(MemberOf(sent, "preset").IsObject());
        EXPECT_EQ(StringOf(sent, "presetName"), "form");
        EXPECT_EQ(OnDisk("drafted").presetData, request.presetData);
    }

    TEST_F(ArtGenSetSessionTest, EnqueueTakesSubjectsAsLinesAsAListAndAsObjects) {
        const ControlReply firstReply = Call("artgen_enqueue", R"({"count":3,"preset":"wood","seed":5,"set":"from_lines","subjects":"# pieces\nrook: a rook\n\nWhite Knight!"})");
        const JsonView first = firstReply.Result();
        EXPECT_EQ(StringOf(first, "set"), "from_lines");
        EXPECT_EQ(IntOf(first, "seed"), Optional<int64_t>(5));
        EXPECT_EQ(RawOf(first, "jobs"), R"(["rook","white_knight"])");
        EXPECT_EQ(BoolOf(first, "serviceConnected"), Optional<bool>(false));
        EXPECT_EQ(KeysOf(first), (Vector<String>{"jobs", "seed", "serviceConnected", "set"}));
        EXPECT_EQ(OnDisk("from_lines").count, 3);

        const ControlReply secondReply = Call("artgen_enqueue", R"({"preset":"wood","set":"from_list","subjects":["bishop: a bishop","a queen"]})");
        const JsonView second = secondReply.Result();
        EXPECT_EQ(RawOf(second, "jobs"), R"(["bishop","a_queen"])");
        EXPECT_EQ(OnDisk("from_list").count, kArtGenSetDefaultCount);

        const ControlReply thirdReply = Call("artgen_enqueue", R"({"preset":"wood","seed":null,"set":"from_objects","subjects":[{"name":"pawn","subject":"a small pawn"},{"subject":"A Tall King"}]})");
        const JsonView third = thirdReply.Result();
        EXPECT_EQ(RawOf(third, "jobs"), R"(["pawn","a_tall_king"])");
        const ArtGenSet disk = OnDisk("from_objects");
        EXPECT_EQ(disk.jobs[0].subject, "a small pawn");
        EXPECT_EQ(IntOf(third, "seed"), Optional<int64_t>(disk.seed)) << "no seed: one is drawn and told";

        const String draft = R"({"preset":{"preset":1,"prompt":"{subject}","title":"form","workflow":"txt2img"},"presetName":"form","set":"from_draft","subjects":"a"})";
        EXPECT_EQ(StringOf(Call("artgen_enqueue", draft).Result(), "set"), "from_draft");
        EXPECT_EQ(OnDisk("from_draft").preset, "form");
    }

    TEST_F(ArtGenSetSessionTest, EnqueueRefusesBadParamsAsInvalidParams) {
        const auto code = [this](StringView params) {
            const String found = Call("artgen_enqueue", params).ErrorCode();
            return found.empty() ? String("ok") : found;
        };
        constexpr StringView kGood = R"({"preset":"wood","subjects":"a\nb"})";

        EXPECT_EQ(code(JsonWithout(kGood, "/preset")), "invalid_params");
        EXPECT_EQ(code(JsonWith(kGood, "/preset", "5")), "invalid_params");
        EXPECT_EQ(code(JsonWith(kGood, "/preset", R"("")")), "invalid_params");
        EXPECT_EQ(code(JsonWith(kGood, "/preset", "{}")), "invalid_params") << "an object that is not a preset";
        EXPECT_EQ(code(JsonWithout(kGood, "/subjects")), "invalid_params");

        const StringView badSubjects[] = {
            R"([])", R"("")", R"("# only a comment")", R"(5)", R"([5])", R"([{}])", R"(["a: x","a: y"])", R"([{"name":"bad name","subject":"x"}])", R"([{"name":3,"subject":"x"}])", R"(["!!!"])"};
        for (const StringView subjects : badSubjects) {
            EXPECT_EQ(code(JsonWith(kGood, "/subjects", subjects)), "invalid_params") << String(subjects).c_str();
        }

        const StringView badCounts[] = {"0", "17", "-1", R"("4")", "1.5", "1099511627776"};
        for (const StringView count : badCounts) {
            EXPECT_EQ(code(JsonWith(kGood, "/count", count)), "invalid_params") << String(count).c_str();
        }
        EXPECT_EQ(code(JsonWith(kGood, "/seed", "-3")), "invalid_params");
        EXPECT_EQ(code(JsonWith(kGood, "/seed", R"("7")")), "invalid_params");
        const String longName = Str(String(65, 'a'));
        const StringView badNames[] = {R"("bad name")", R"("../up")", R"("")", "3", longName};
        for (const StringView name : badNames) {
            EXPECT_EQ(code(JsonWith(kGood, "/set", name)), "invalid_params") << String(name).c_str();
        }

        String lines;
        for (int32_t i = 0; i <= kMaxSetJobs; ++i) {
            lines += Format("job{}: x\n", i);
        }
        EXPECT_EQ(code(JsonWith(kGood, "/subjects", Str(lines))), "invalid_params");

        EXPECT_TRUE(session.GetSets().empty()) << "a refusal keeps nothing";
        const String taken = JsonWith(kGood, "/set", R"("taken")");
        EXPECT_EQ(code(taken), "ok");
        EXPECT_EQ(code(taken), "invalid_params") << "the folder exists";
    }

    TEST_F(ArtGenSetSessionTest, ArtgenSetAnswersTheManifestAndItsState) {
        ASSERT_TRUE(Enqueue("shown", "a\nb").has_value());
        constexpr StringView kShown = R"({"set":"shown"})";
        const auto expectManifest = [this](JsonView answer) {
            const JsonDoc withoutState(JsonWithout(String(answer.Raw()), "/state"));
            const JsonDoc manifest(ManifestText(OnDisk("shown")));
            EXPECT_TRUE(JsonEquals(withoutState, manifest)) << String(answer.Raw()).c_str();
        };
        {
            const ControlReply answerReply = Call("artgen_set", kShown);
            const JsonView answer = answerReply.Result();
            EXPECT_EQ(StringOf(answer, "state"), "Active");
            expectManifest(answer);
        }
        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "failed", "boom");
        const ControlReply answerReply = Call("artgen_set", kShown);
        const JsonView answer = answerReply.Result();
        EXPECT_EQ(StringOf(answer, "state"), "Complete");
        expectManifest(answer);
        const JsonView jobs = MemberOf(answer, "jobs");
        ASSERT_TRUE(jobs.IsValid());
        ASSERT_EQ(jobs.Size(), 2u);
        EXPECT_EQ(StringOf(ItemAt(jobs, 1), "error"), "boom");
        const JsonView variants = MemberOf(ItemAt(jobs, 0), "variants");
        ASSERT_TRUE(variants.IsValid());
        ASSERT_EQ(variants.Size(), 2u);
        EXPECT_EQ(StringOf(ItemAt(variants, 0), "file"), "a/a_40.png");

        EXPECT_EQ(Call("artgen_set").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("artgen_set", R"({"set":"nothing"})").ErrorCode(), "artgen_no_set");
    }

    TEST_F(ArtGenSetSessionTest, OfTwoEqualSetKeysOfACallTheFirstIsTheSet) {
        ASSERT_TRUE(Enqueue("shown", "a\nb").has_value());
        const auto first = session.HandleSet(JsonDoc(R"({"set":"shown","set":"nothing"})"));
        EXPECT_TRUE(first.has_value());
        const auto other = session.HandleSet(JsonDoc(R"({"set":"nothing","set":"shown"})"));
        ASSERT_FALSE(other.has_value());
        EXPECT_EQ(other.error().code, "artgen_no_set");
    }

    TEST_F(ArtGenSetSessionTest, AnEnqueueWhoseCountIsNotAFiniteNumberIsInvalidParams) {
        const auto refused = session.HandleEnqueue(JsonDoc(R"({"preset":"wood","subjects":"a","count":1e999})"));
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, "invalid_params");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenPickAndRetryAnswerWhatTheyDid) {
        ASSERT_TRUE(Enqueue("edit", "a\nb\nc").has_value());
        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "failed", "boom");
        session.CancelJob("edit", "c");

        String pick = Format(R"({{"set":"edit","job":"a","seed":{}}})", kSetSeed + 1);
        const ControlReply pickedReply = Call("artgen_pick", pick);
        const JsonView picked = pickedReply.Result();
        EXPECT_EQ(IntOf(picked, "pick"), Optional<int64_t>(kSetSeed + 1));
        EXPECT_EQ(OnDisk("edit").jobs[0].pick, Optional<int64_t>(kSetSeed + 1));

        pick = JsonWith(pick, "/seed", "999");
        EXPECT_EQ(Call("artgen_pick", pick).ErrorCode(), "artgen_no_variant");
        pick = JsonWith(pick, "/job", R"("zzz")");
        EXPECT_EQ(Call("artgen_pick", pick).ErrorCode(), "artgen_no_set");
        pick = JsonWith(pick, "/set", R"("zzz")");
        EXPECT_EQ(Call("artgen_pick", pick).ErrorCode(), "artgen_no_set");
        pick = JsonWith(pick, "/seed", R"("1")");
        EXPECT_EQ(Call("artgen_pick", pick).ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("artgen_pick").ErrorCode(), "invalid_params");

        String retry = R"({"set":"edit","job":"b"})";
        EXPECT_EQ(IntOf(Call("artgen_retry", retry).Result(), "retried"), Optional<int64_t>(1));
        EXPECT_EQ(KeysOf(Call("artgen_retry", retry).Result()), (Vector<String>{"retried", "set"}));
        EXPECT_EQ(StateOf(OnDisk("edit"), 1), ArtGenSetJobState::Pending);
        EXPECT_EQ(StateOf(OnDisk("edit"), 2), ArtGenSetJobState::Cancelled);
        retry = JsonWithout(retry, "/job");
        EXPECT_EQ(IntOf(Call("artgen_retry", retry).Result(), "retried"), Optional<int64_t>(1)) << "b is already waiting";
        EXPECT_EQ(IntOf(Call("artgen_retry", retry).Result(), "retried"), Optional<int64_t>(0));

        retry = JsonWith(retry, "/job", R"("zzz")");
        EXPECT_EQ(Call("artgen_retry", retry).ErrorCode(), "artgen_no_set");
        retry = JsonWith(retry, "/job", "4");
        EXPECT_EQ(Call("artgen_retry", retry).ErrorCode(), "invalid_params");
        retry = JsonWith(retry, "/set", R"("zzz")");
        retry = JsonWithout(retry, "/job");
        EXPECT_EQ(Call("artgen_retry", retry).ErrorCode(), "artgen_no_set");
        EXPECT_EQ(Call("artgen_retry").ErrorCode(), "invalid_params");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenCancelNamesAJobOrASet) {
        ASSERT_TRUE(Enqueue("one", "a\nb\nc").has_value());
        ASSERT_TRUE(Enqueue("two", "a\nx").has_value());
        const ControlReply first = Poll();
        ASSERT_TRUE(RequestOf(first).IsValid());

        // Both sets have an "a" in work (one running, one waiting): the name alone is ambiguous
        String ambiguous = R"({"job":"a"})";
        EXPECT_EQ(Call("artgen_cancel", ambiguous).ErrorCode(), "invalid_params");

        String alone = R"({"job":"c"})";
        const ControlReply cancelledReply = Call("artgen_cancel", alone);
        const JsonView cancelled = cancelledReply.Result();
        EXPECT_EQ(cancelled.Raw(), R"({"job":"c","set":"one","state":"Cancelled"})");
        EXPECT_EQ(StateOf(OnDisk("one"), 2), ArtGenSetJobState::Cancelled);
        EXPECT_EQ(Call("artgen_cancel", alone).ErrorCode(), "artgen_no_set") << "no longer in work";

        String running = R"({"job":"a","set":"one"})";
        EXPECT_EQ(StringOf(Call("artgen_cancel", running).Result(), "state"), "Running") << "the service ends it";
        EXPECT_EQ(IntOf(Poll().Result(), "cancel"), IntOf(RequestOf(first), "id"));
        Finish(RequestOf(first), "cancelled");
        EXPECT_EQ(StateOf(OnDisk("one"), 0), ArtGenSetJobState::Cancelled);

        running = JsonWith(running, "/set", R"("nothing")");
        EXPECT_EQ(Call("artgen_cancel", running).ErrorCode(), "artgen_no_set");
        running = JsonWith(running, "/set", R"("two")");
        running = JsonWith(running, "/job", R"("nothing")");
        EXPECT_EQ(Call("artgen_cancel", running).ErrorCode(), "artgen_no_set");

        String whole = R"({"set":"one"})";
        EXPECT_EQ(Call("artgen_cancel", whole).ErrorCode(), "invalid_params") << "a set needs all";
        whole = JsonWith(whole, "/all", "false");
        EXPECT_EQ(Call("artgen_cancel", whole).ErrorCode(), "invalid_params");
        whole = JsonWith(whole, "/all", R"("yes")");
        EXPECT_EQ(Call("artgen_cancel", whole).ErrorCode(), "invalid_params");
        whole = JsonWith(whole, "/all", "true");
        whole = JsonWith(whole, "/job", R"("b")");
        EXPECT_EQ(Call("artgen_cancel", whole).ErrorCode(), "invalid_params") << "a job and all";

        // The end of "a" left "b" of the first set as the next job; cancelling the other set leaves it running
        const ControlReply next = Poll();
        ASSERT_TRUE(RequestOf(next).IsValid());
        EXPECT_EQ(StringOf(RequestOf(next), "outDir"), "one/b");
        String all = R"({"set":"two","all":true})";
        EXPECT_EQ(Call("artgen_cancel", all).Result().Raw(), R"({"set":"two","state":"Complete"})");
        EXPECT_EQ(CountJobs(OnDisk("two"), ArtGenSetJobState::Cancelled), 2);
        all = JsonWith(all, "/set", R"("nothing")");
        EXPECT_EQ(Call("artgen_cancel", all).ErrorCode(), "artgen_no_set");

        // Without params only the run in work is meant, as before
        EXPECT_EQ(Call("artgen_cancel").ErrorCode(), "");
        Finish(RequestOf(next), "cancelled");
        EXPECT_EQ(Call("artgen_cancel").ErrorCode(), "artgen_no_run");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenStateSummarisesTheSets) {
        ASSERT_TRUE(Enqueue("older", "a\nb\nc").has_value());
        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "failed", "boom");
        session.CancelJob("older", "c");
        ASSERT_EQ(session.PickVariant("older", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_TRUE(Enqueue("active", "x\ny").has_value());
        ASSERT_TRUE(RequestOf(Poll()).IsValid());

        const ControlReply stateReply = Call("artgen_state");
        const JsonView state = stateReply.Result();
        const JsonView sets = MemberOf(state, "sets");
        ASSERT_TRUE(sets.IsValid());
        ASSERT_EQ(sets.Size(), 2u);
        const JsonView older = ItemAt(sets, 0);
        EXPECT_EQ(StringOf(older, "name"), "older");
        EXPECT_EQ(StringOf(older, "preset"), "wood");
        EXPECT_EQ(StringOf(older, "kind"), "image");
        EXPECT_EQ(StringOf(older, "state"), "Complete");
        EXPECT_EQ(IntOf(older, "count"), Optional<int64_t>(2));
        EXPECT_EQ(IntOf(older, "seed"), Optional<int64_t>(kSetSeed));
        EXPECT_EQ(IntOf(older, "picked"), Optional<int64_t>(1));
        EXPECT_EQ(IntOf(older, "accepted"), Optional<int64_t>(0));
        const JsonView olderJobs = MemberOf(older, "jobs");
        ASSERT_TRUE(olderJobs.IsValid());
        EXPECT_EQ(IntOf(olderJobs, "done"), Optional<int64_t>(1));
        EXPECT_EQ(IntOf(olderJobs, "failed"), Optional<int64_t>(1));
        EXPECT_EQ(IntOf(olderJobs, "cancelled"), Optional<int64_t>(1));
        EXPECT_EQ(IntOf(olderJobs, "pending"), Optional<int64_t>(0));
        const JsonView active = ItemAt(sets, 1);
        EXPECT_EQ(StringOf(active, "state"), "Active");
        const JsonView activeJobs = MemberOf(active, "jobs");
        ASSERT_TRUE(activeJobs.IsValid());
        EXPECT_EQ(IntOf(activeJobs, "running"), Optional<int64_t>(1));
        EXPECT_EQ(IntOf(activeJobs, "pending"), Optional<int64_t>(1));
    }

    TEST_F(ArtGenSetSessionTest, ASetupActionIsRefusedWhileASetIsActive) {
        ASSERT_TRUE(Enqueue("hold", "a").has_value());
        String setup = R"({"action":"Plan"})";
        EXPECT_EQ(Call("artgen_setup", setup).ErrorCode(), "artgen_busy");
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllTakesThePickedVariantOfEveryJobUnderTheJobName) {
        ASSERT_TRUE(Enqueue("pieces", "rook: a rook\nknight: a knight\nbishop: a bishop").has_value());
        RunAndPickAll("pieces");
        ASSERT_EQ(session.PickVariant("pieces", "knight", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("pieces"));

        String params = R"({"set":"pieces"})";
        const ControlReply answerReply = Call("artgen_accept_set", params);
        const JsonView answer = answerReply.Result();
        EXPECT_EQ(StringOf(answer, "set"), "pieces");
        const JsonView accepted = MemberOf(answer, "accepted");
        ASSERT_TRUE(accepted.IsValid());
        ASSERT_EQ(accepted.Size(), 3u);
        EXPECT_EQ(StringOf(ItemAt(accepted, 1), "job"), "knight");
        EXPECT_EQ(StringOf(ItemAt(accepted, 1), "as"), "knight");
        EXPECT_EQ(StringOf(ItemAt(accepted, 1), "sprite"), "knight") << "the acceptor's answer is part of the row";
        EXPECT_EQ(IntOf(ItemAt(accepted, 1), "seed"), Optional<int64_t>(kSetSeed));
        EXPECT_EQ(IntOf(ItemAt(accepted, 1), "alternatives"), Optional<int64_t>(1));

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
        const ControlReply stateReply = Call("artgen_state");
        const JsonView state = stateReply.Result();
        EXPECT_EQ(IntOf(ItemAt(MemberOf(state, "sets"), 0), "accepted"), Optional<int64_t>(3));
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
        String params = R"({"set":"pieces"})";
        const ControlReply answerReply = Call("artgen_accept_set", params);
        const JsonView answer = answerReply.Result();
        const JsonView accepted = MemberOf(answer, "accepted");
        ASSERT_TRUE(accepted.IsValid()) << String(answer.Raw()).c_str();
        EXPECT_EQ(accepted.Size(), 2u);
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].path, Format("{}/artgen/out/pieces/rook/rook_41.png", _root.View()));

        ReloadFromDisk();
        EXPECT_NE(session.FindSet("pieces"), nullptr);
        RemoveDirectory(parent);
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllRefusesASetThatIsNotReadyAndChangesNothing) {
        ASSERT_TRUE(Enqueue("gaps", "a\nb\nc\nd").has_value());
        String params = R"({"set":"gaps"})";
        EXPECT_EQ(Call("artgen_accept_set", params).ErrorCode(), "artgen_busy") << "the set is still active";

        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "failed", "boom");
        session.CancelJob("gaps", "d");
        ASSERT_EQ(session.PickVariant("gaps", "a", kSetSeed), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("gaps"));

        const ControlReply refused = Call("artgen_accept_set", params);
        EXPECT_EQ(refused.ErrorCode(), "artgen_set_incomplete");
        const String message = refused.ErrorMessage();
        EXPECT_EQ(message.find("not picked"), String::npos) << "an image row with no pick takes its first variant: " << message.c_str();
        EXPECT_NE(message.find("failed: c"), String::npos) << message.c_str();
        EXPECT_NE(message.find("cancelled: d"), String::npos) << message.c_str();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_EQ(CountJobs(OnDisk("gaps"), ArtGenSetJobState::Done), 2);
        EXPECT_FALSE(OnDisk("gaps").jobs[0].accepted.has_value());

        params = JsonWith(params, "/set", R"("nothing")");
        EXPECT_EQ(Call("artgen_accept_set", params).ErrorCode(), "artgen_no_set");
        EXPECT_EQ(Call("artgen_accept_set").ErrorCode(), "invalid_params");
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllChecksEveryRowsFilesBeforeTheFirstAccept) {
        ASSERT_TRUE(Enqueue("files", "a\nb\nc").has_value());
        RunAndPickAll("files");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("files"));
        String params = R"({"set":"files"})";

        // The picked file of the last row is gone: the first two are not accepted either
        RemoveFileIfPresent(AbsoluteInSet(_root, "files", "c/c_41.png"));
        ControlReply refused = Call("artgen_accept_set", params);
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
        EXPECT_NE(message.find("'c'"), String::npos) << message.c_str();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_FALSE(OnDisk("files").jobs[0].accepted.has_value());

        // The provenance of a row is gone
        ASSERT_TRUE(WriteFileAtomically(AbsoluteInSet(_root, "files", "c/c_41.png"), "x").has_value());
        RemoveFileIfPresent(AbsoluteInSet(_root, "files", "a/a_41.artgen.json"));
        EXPECT_EQ(Call("artgen_accept_set", params).ErrorCode(), "artgen_accept_failed");
        EXPECT_TRUE(gAccepted.empty());

        // No acceptor here
        ASSERT_TRUE(WriteFileAtomically(AbsoluteInSet(_root, "files", "a/a_41.artgen.json"), "x").has_value());
        session.SetAcceptor(nullptr);
        EXPECT_EQ(Call("artgen_accept_set", params).ErrorCode(), "artgen_accept_failed");
        session.SetAcceptor(&FakeAcceptor);

        // Everything in place: the same call goes through
        EXPECT_EQ(Call("artgen_accept_set", params).ErrorCode(), "") << "everything is in place now";
        EXPECT_EQ(gAccepted.size(), 3u);
    }

    TEST_F(ArtGenSetSessionTest, AFailureHalfwayNamesTheRowAndTheNextCallFinishesTheSet) {
        ASSERT_TRUE(Enqueue("five", "a\nb\nc\nd\ne").has_value());
        RunAndPickAll("five");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("five"));
        String params = R"({"set":"five"})";

        gFailOn = "c";
        const ControlReply refused = Call("artgen_accept_set", params);
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
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
        const ControlReply againReply = Call("artgen_accept_set", params);
        const JsonView again = againReply.Result();
        ASSERT_TRUE(MemberOf(again, "accepted").IsValid());
        EXPECT_EQ(MemberOf(again, "accepted").Size(), 5u);
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

        const ControlReply answerReply = AcceptAllCall("batch");
        const JsonView answer = answerReply.Result();

        const Vector<String> expected{"begin", "accept a", "accept b", "accept c", "end"};
        EXPECT_EQ(gEvents, expected);
        EXPECT_EQ(markedWhileEnding, 0u) << "the library is saved before any row is marked";
        ASSERT_TRUE(MemberOf(answer, "accepted").IsValid());
        const JsonView rows = MemberOf(answer, "accepted");
        ASSERT_EQ(rows.Size(), 3u);
        EXPECT_EQ(StringOf(ItemAt(rows, 2), "as"), "c");
        EXPECT_EQ(StringOf(ItemAt(rows, 2), "sprite"), "c");
        EXPECT_EQ(IntOf(ItemAt(rows, 2), "seed"), Optional<int64_t>(kSetSeed + 1));
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

        const ControlReply answerReply = AcceptAllCall("nopick");
        const JsonView answer = answerReply.Result();

        ASSERT_TRUE(MemberOf(answer, "accepted").IsValid());
        const JsonView rows = MemberOf(answer, "accepted");
        ASSERT_EQ(rows.Size(), 2u);
        EXPECT_EQ(IntOf(ItemAt(rows, 0), "seed"), Optional<int64_t>(kSetSeed)) << "the first variant is the candidate of a row without a pick";
        EXPECT_EQ(IntOf(ItemAt(rows, 0), "alternatives"), Optional<int64_t>(1));
        EXPECT_EQ(IntOf(ItemAt(rows, 1), "alternatives"), Optional<int64_t>(1));
    }

    TEST_F(ArtGenSetSessionTest, AFailureHalfwayInABatchEndsItMarksTheRowsDoneAndTheNextCallFinishes) {
        session.SetAcceptBatch(&FakeBegin, &FakeEnd);
        ASSERT_TRUE(Enqueue("five", "a\nb\nc\nd\ne").has_value());
        RunAndPickAll("five");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("five"));

        gFailOn = "c";
        const ControlReply refused = AcceptAllCall("five");

        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
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
        EXPECT_EQ(AcceptAllCall("five").ErrorCode(), "");
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
        const ControlReply refused = AcceptAllCall("lost");

        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
        EXPECT_NE(message.find("files written: a, b, c"), String::npos) << message.c_str();
        EXPECT_NE(message.find("library was not saved"), String::npos) << message.c_str();
        EXPECT_NE(message.find("call again"), String::npos) << message.c_str();
        for (const ArtGenSetJob& job : OnDisk("lost").jobs) {
            EXPECT_FALSE(job.accepted.has_value());
        }
        EXPECT_FALSE(OnDisk("lost").processing.has_value());
        EXPECT_EQ(session.Version(), versionBefore) << "nothing of the set changed";

        gEndFails = false;
        EXPECT_EQ(AcceptAllCall("lost").ErrorCode(), "");
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
        const String message = AcceptAllCall("both").ErrorMessage();

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
        ASSERT_EQ(AcceptAllCall("done").ErrorCode(), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("done", "a"));
        gEvents.clear();
        EXPECT_EQ(AcceptAllCall("done").ErrorCode(), "");
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
        EXPECT_EQ(AcceptAllCall("heard").ErrorCode(), "");
        const Vector<String> expected{"accept knock", "accept tap"};
        EXPECT_EQ(gEvents, expected) << "a sound touches no library";
        EXPECT_TRUE(OnDisk("heard").jobs[1].accepted.has_value());
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfRowsWithNoPickTakesTheFirstVariantAndRecordsItAsThePick) {
        ASSERT_TRUE(Enqueue("firsts", "a\nb").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_EQ(session.PickVariant("firsts", "b", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("firsts"));

        const ControlReply answerReply = AcceptAllCall("firsts");
        const JsonView answer = answerReply.Result();
        ASSERT_TRUE(MemberOf(answer, "accepted").IsValid()) << "the answer: " << String(answer.Raw()).c_str();
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].seed, kSetSeed) << "a has no pick: its first variant";
        EXPECT_EQ(gAccepted[1].seed, kSetSeed + 1) << "b keeps its pick";
        const ArtGenSet disk = OnDisk("firsts");
        EXPECT_EQ(disk.jobs[0].pick, Optional<int64_t>(kSetSeed));
        EXPECT_EQ(disk.jobs[1].pick, Optional<int64_t>(kSetSeed + 1));
        EXPECT_TRUE(IsLive(disk.jobs[0]));
        EXPECT_EQ(IntOf(ItemAt(MemberOf(answer, "accepted"), 0), "seed"), Optional<int64_t>(kSetSeed));
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

        const ControlReply refused = AcceptAllCall("heard");
        EXPECT_EQ(refused.ErrorCode(), "artgen_set_incomplete");
        const String message = refused.ErrorMessage();
        EXPECT_NE(message.find("not picked: tap"), String::npos) << message.c_str();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_FALSE(OnDisk("heard").jobs[1].pick.has_value()) << "nothing was picked for the person";
        EXPECT_FALSE(OnDisk("heard").jobs[0].accepted.has_value());
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllSkipsAKeptRowAndWritesTheOthers) {
        ASSERT_TRUE(Enqueue("mixed", "a\nb\nc").has_value());
        RunAndPickAll("mixed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("mixed"));
        ASSERT_EQ(AcceptAllCall("mixed").ErrorCode(), "");
        ASSERT_EQ(gAccepted.size(), 3u);
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("mixed", "b"));

        gAccepted.clear();
        const ControlReply answerReply = AcceptAllCall("mixed");
        const JsonView answer = answerReply.Result();
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].name, "a");
        EXPECT_EQ(gAccepted[1].name, "c");
        const JsonView rows = MemberOf(answer, "accepted");
        ASSERT_EQ(rows.Size(), 3u);
        EXPECT_EQ(StringOf(ItemAt(rows, 1), "job"), "b");
        EXPECT_EQ(BoolOf(ItemAt(rows, 1), "kept"), Optional<bool>(true));
        EXPECT_FALSE(MemberOf(ItemAt(rows, 1), "as").IsValid()) << "nothing was written for it";
        EXPECT_EQ(KeysOf(ItemAt(rows, 1)), (Vector<String>{"job", "kept"}));
        EXPECT_TRUE(OnDisk("mixed").jobs[1].accepted->kept);
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfASetWithEveryRowKeptNeedsNoAcceptor) {
        ASSERT_TRUE(Enqueue("done", "a").has_value());
        RunAndPickAll("done");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("done"));
        ASSERT_EQ(AcceptAllCall("done").ErrorCode(), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("done", "a"));
        session.SetAcceptor(nullptr);
        gAccepted.clear();
        EXPECT_EQ(AcceptAllCall("done").ErrorCode(), "");
        EXPECT_TRUE(gAccepted.empty());
    }

    TEST_F(ArtGenSetSessionTest, APickOfALiveRowSwitchesTheGameToThatVariant) {
        ASSERT_TRUE(Enqueue("live", "a\nb").has_value());
        RunAndPickAll("live");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("live"));
        ASSERT_EQ(AcceptAllCall("live").ErrorCode(), "");
        gAccepted.clear();

        String pick = Format(R"({{"set":"live","job":"a","seed":{}}})", kSetSeed);
        const ControlReply replyReply = Call("artgen_pick", pick);
        const JsonView reply = replyReply.Result();
        EXPECT_EQ(IntOf(reply, "pick"), Optional<int64_t>(kSetSeed));
        ASSERT_TRUE(MemberOf(reply, "switched").IsValid()) << String(reply.Raw()).c_str();
        EXPECT_EQ(StringOf(MemberOf(reply, "switched"), "sprite"), "a");

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
        EXPECT_FALSE(MemberOf(Call("artgen_pick", pick).Result(), "switched").IsValid());
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
        ASSERT_EQ(AcceptAllCall("lack").ErrorCode(), "");
        gAccepted.clear();

        String pick = R"({"set":"lack","job":"a","seed":999})";
        EXPECT_EQ(Call("artgen_pick", pick).ErrorCode(), "artgen_no_variant");
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_EQ(OnDisk("lack").jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
    }

    TEST_F(ArtGenSetSessionTest, AFailedSwitchLeavesThePickAndTheDateAsTheyWere) {
        ASSERT_TRUE(Enqueue("fail", "a").has_value());
        RunAndPickAll("fail");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("fail"));
        ASSERT_EQ(AcceptAllCall("fail").ErrorCode(), "");
        const ArtGenSet before = OnDisk("fail");
        gAccepted.clear();

        String pick = Format(R"({{"set":"fail","job":"a","seed":{}}})", kSetSeed);

        gFailOn = "a";
        ArtGenSwitch write;
        EXPECT_EQ(session.PickVariant("fail", "a", kSetSeed, &write), ArtGenSetEdit::WriteFailed);
        EXPECT_NE(write.error.find("the disk is full"), String::npos) << write.error.c_str();
        EXPECT_NE(write.error.find(Format("the row still names seed {}", kSetSeed + 1)), String::npos) << write.error.c_str();
        const ControlReply refused = Call("artgen_pick", pick);
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        EXPECT_EQ(ManifestText(OnDisk("fail")), ManifestText(before));
        EXPECT_EQ(Require(session.FindSet("fail")).jobs[0].pick, Optional<int64_t>(kSetSeed + 1));

        gFailOn.clear();
        RemoveFileIfPresent(AbsoluteInSet(_root, "fail", "a/a_40.png"));
        EXPECT_EQ(session.PickVariant("fail", "a", kSetSeed, &write), ArtGenSetEdit::WriteFailed);
        EXPECT_NE(write.error.find("is gone"), String::npos) << write.error.c_str();
        EXPECT_TRUE(gAccepted.empty()) << "the files are checked before the acceptor is asked";

        session.SetAcceptor(nullptr);
        EXPECT_EQ(Call("artgen_pick", pick).ErrorCode(), "artgen_accept_failed");
        EXPECT_EQ(Require(session.FindSet("fail")).jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
    }

    TEST_F(ArtGenSetSessionTest, ALiveRowStillSwitchesAfterARestart) {
        ASSERT_TRUE(Enqueue("again", "a").has_value());
        RunAndPickAll("again");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("again"));
        ASSERT_EQ(AcceptAllCall("again").ErrorCode(), "");
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
        ASSERT_EQ(AcceptAllCall("clips").ErrorCode(), "");
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
        ASSERT_EQ(AcceptAllCall("keep").ErrorCode(), "");

        String params = R"({"set":"keep","job":"a"})";
        const ControlReply answerReply = Call("artgen_keep", params);
        const JsonView answer = answerReply.Result();
        const JsonView kept = MemberOf(answer, "kept");
        ASSERT_TRUE(kept.IsValid()) << String(answer.Raw()).c_str();
        ASSERT_EQ(kept.Size(), 1u);
        EXPECT_EQ(ItemAt(kept, 0).Raw(), R"("a")");
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

        String pick = Format(R"({{"set":"keep","job":"a","seed":{}}})", kSetSeed);
        gAccepted.clear();
        EXPECT_EQ(Call("artgen_pick", pick).ErrorCode(), "artgen_no_variant") << "the alternatives are gone";
        EXPECT_TRUE(gAccepted.empty());
    }

    TEST_F(ArtGenSetSessionTest, KeepOfARowThatIsNotLiveIsInvalidParams) {
        ASSERT_TRUE(Enqueue("notlive", "a\nb").has_value());
        RunAndPickAll("notlive");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("notlive"));
        String params = R"({"set":"notlive"})";
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "invalid_params") << "nothing is accepted yet";
        params = JsonWith(params, "/job", R"("a")");
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "invalid_params");

        ASSERT_EQ(AcceptAllCall("notlive").ErrorCode(), "");
        ASSERT_EQ(Call("artgen_keep", params).ErrorCode(), "");
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "invalid_params") << "already kept";
        params = JsonWithout(params, "/job");
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "") << "b is still live";
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "invalid_params") << "nothing live is left";
        EXPECT_EQ(gKept.size(), 2u);
    }

    TEST_F(ArtGenSetSessionTest, KeepOfAWholeSetKeepsEveryLiveRow) {
        ASSERT_TRUE(Enqueue("all", "a\nb\nc").has_value());
        RunAndPickAll("all");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("all"));
        ASSERT_EQ(AcceptAllCall("all").ErrorCode(), "");

        String params = R"({"set":"all"})";
        const ControlReply answerReply = Call("artgen_keep", params);
        const JsonView answer = answerReply.Result();
        ASSERT_TRUE(MemberOf(answer, "kept").IsValid());
        EXPECT_EQ(MemberOf(answer, "kept").Size(), 3u);
        EXPECT_EQ(gKept.size(), 3u);
        const ArtGenSet disk = OnDisk("all");
        EXPECT_EQ(CountKept(disk), 3);
        EXPECT_EQ(CountLive(disk), 0);
    }

    TEST_F(ArtGenSetSessionTest, KeepNamesWhatIsUnknownAndNeedsAKeeper) {
        ASSERT_TRUE(Enqueue("unk", "a").has_value());
        RunAndPickAll("unk");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("unk"));
        ASSERT_EQ(AcceptAllCall("unk").ErrorCode(), "");

        String params = R"({"set":"nothing"})";
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "artgen_no_set");
        params = JsonWith(params, "/set", R"("unk")");
        params = JsonWith(params, "/job", R"("zzz")");
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "artgen_no_set");
        params = JsonWith(params, "/job", "3");
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("artgen_keep").ErrorCode(), "invalid_params");

        params = JsonWithout(params, "/job");
        session.SetKeeper(nullptr);
        EXPECT_EQ(Call("artgen_keep", params).ErrorCode(), "artgen_accept_failed");
        EXPECT_TRUE(IsLive(OnDisk("unk").jobs[0]));
    }

    TEST_F(ArtGenSetSessionTest, AFailingKeeperChangesNothingForItsRowAndTheRowsBeforeStayKept) {
        ASSERT_TRUE(Enqueue("kfail", "a\nb\nc").has_value());
        RunAndPickAll("kfail");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("kfail"));
        ASSERT_EQ(AcceptAllCall("kfail").ErrorCode(), "");

        String params = R"({"set":"kfail"})";
        gKeepFailOn = "b";
        const ControlReply refused = Call("artgen_keep", params);
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
        EXPECT_NE(message.find("'b'"), String::npos) << message.c_str();
        EXPECT_NE(message.find("read-only"), String::npos) << message.c_str();
        EXPECT_NE(message.find("kept before it: a"), String::npos) << message.c_str();

        const ArtGenSet disk = OnDisk("kfail");
        EXPECT_TRUE(disk.jobs[0].accepted->kept);
        EXPECT_TRUE(IsLive(disk.jobs[1]));
        EXPECT_EQ(disk.jobs[1].variants.size(), 2u) << "the failed row keeps its alternatives";
        EXPECT_TRUE(IsLive(disk.jobs[2]));

        gKeepFailOn.clear();
        const ControlReply againReply = Call("artgen_keep", params);
        const JsonView again = againReply.Result();
        EXPECT_EQ(MemberOf(again, "kept").Size(), 2u);
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
        ASSERT_EQ(AcceptAllCall("kclips").ErrorCode(), "");
        EXPECT_TRUE(session.KeepSet("kclips").has_value());
        ASSERT_EQ(gKept.size(), 1u);
        EXPECT_EQ(gKept[0].kind, "audio");
        EXPECT_EQ(gKept[0].name, "knock");
    }

    TEST_F(ArtGenSetSessionTest, ArtgenStateCountsLiveAndKeptRows) {
        ASSERT_TRUE(Enqueue("counts", "a\nb\nc").has_value());
        RunAndPickAll("counts");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("counts"));
        const auto setState = [this](StringView key) {
            const ControlReply state = Call("artgen_state");
            return IntOf(ItemAt(MemberOf(state.Result(), "sets"), 0), key);
        };
        EXPECT_EQ(setState("live"), Optional<int64_t>(0));
        EXPECT_EQ(setState("kept"), Optional<int64_t>(0));

        ASSERT_EQ(AcceptAllCall("counts").ErrorCode(), "");
        EXPECT_EQ(setState("live"), Optional<int64_t>(3));
        EXPECT_EQ(setState("kept"), Optional<int64_t>(0));

        ASSERT_TRUE(session.KeepSet("counts", "b").has_value());
        EXPECT_EQ(setState("live"), Optional<int64_t>(2));
        EXPECT_EQ(setState("kept"), Optional<int64_t>(1));
        EXPECT_EQ(setState("accepted"), Optional<int64_t>(3));
    }

    TEST_F(ArtGenSetSessionTest, AConnectedServiceWhoseGeneratorIsNotReadyLeavesTheJobsPending) {
        ASSERT_TRUE(Enqueue("down", "a\nb").has_value());
        const String notRunning = JsonWith(JsonWith(kReadyPoll, "/state", R"("not_running")"), "/reason", R"("ComfyUI is not started")");
        const ControlReply first = Call("artgen_poll", notRunning);
        EXPECT_FALSE(RequestOf(first).IsValid());
        EXPECT_EQ(CountJobs(Require(session.FindSet("down")), ArtGenSetJobState::Pending), 2);
        EXPECT_EQ(CountJobs(Require(session.FindSet("down")), ArtGenSetJobState::Running), 0);
        EXPECT_FALSE(session.IsGeneratorReady());
        EXPECT_TRUE(session.IsServiceConnected());

        const ControlReply ready = Poll();
        ASSERT_TRUE(RequestOf(ready).IsValid()) << "the first poll with a ready generator starts a job";
        EXPECT_EQ(CountJobs(Require(session.FindSet("down")), ArtGenSetJobState::Running), 1);
        EXPECT_TRUE(session.IsGeneratorReady());
    }

    TEST_F(ArtGenSetSessionTest, ASwitchOfARowAnotherSetAcceptedLaterIsRefusedAndTheNewerSetStillSwitches) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());
        const ArtGenSet before = OnDisk("first");

        ArtGenSwitch write;
        EXPECT_EQ(session.PickVariant("first", "a", kSetSeed, &write), ArtGenSetEdit::WriteFailed);
        EXPECT_NE(write.error.find("'second'"), String::npos) << write.error.c_str();
        String pick = Format(R"({{"set":"first","job":"a","seed":{}}})", kSetSeed);
        const ControlReply refused = Call("artgen_pick", pick);
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        EXPECT_NE(refused.ErrorMessage().find("'second'"), String::npos) << refused.ErrorMessage().c_str();
        EXPECT_TRUE(gAccepted.empty()) << "the game's file was not written";
        EXPECT_EQ(ManifestText(OnDisk("first")), ManifestText(before));

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
        ASSERT_EQ(AcceptAllCall("second").ErrorCode(), "");
        ArtGenSet disk = OnDisk("second");
        disk.jobs[0].accepted->date = "2999-01-01T00:00:00Z";
        ASSERT_TRUE(WriteManifest(_root, disk).has_value());
        ReloadFromDisk();

        ASSERT_EQ(AcceptAllCall("first").ErrorCode(), "");

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
        ASSERT_EQ(AcceptAllCall("pictures").ErrorCode(), "");
        ASSERT_EQ(AcceptAllCall("clips").ErrorCode(), "");
        gAccepted.clear();

        EXPECT_EQ(session.PickVariant("pictures", "knock", kSetSeed), ArtGenSetEdit::Ok) << "assets/artgen/knock.png is not knock.wav";
        EXPECT_EQ(gAccepted.size(), 1u);
    }

    TEST_F(ArtGenSetSessionTest, KeepOfARowAnotherSetAcceptedLaterIsRefusedAndTheNewerSetKeeps) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());

        String params = R"({"set":"first","job":"a"})";
        const ControlReply refused = Call("artgen_keep", params);
        EXPECT_EQ(refused.ErrorCode(), "invalid_params");
        EXPECT_NE(refused.ErrorMessage().find("'second'"), String::npos) << refused.ErrorMessage().c_str();
        EXPECT_TRUE(gKept.empty()) << "no false kept seed reached the game's file";
        EXPECT_TRUE(IsLive(OnDisk("first").jobs[0]));

        // The whole set keeps what it still owns and says what it left
        params = JsonWithout(params, "/job");
        const ControlReply answerReply = Call("artgen_keep", params);
        const JsonView answer = answerReply.Result();
        ASSERT_TRUE(MemberOf(answer, "kept").IsValid()) << String(answer.Raw()).c_str();
        ASSERT_EQ(MemberOf(answer, "kept").Size(), 1u);
        EXPECT_EQ(ItemAt(MemberOf(answer, "kept"), 0).Raw(), R"("b")");
        const JsonView replaced = MemberOf(answer, "replaced");
        ASSERT_TRUE(replaced.IsValid());
        ASSERT_EQ(replaced.Size(), 1u);
        EXPECT_EQ(StringOf(ItemAt(replaced, 0), "job"), "a");
        EXPECT_EQ(StringOf(ItemAt(replaced, 0), "by"), "second");
        EXPECT_EQ(KeysOf(answer), (Vector<String>{"kept", "replaced", "set"}));
        EXPECT_EQ(KeysOf(ItemAt(replaced, 0)), (Vector<String>{"by", "job"}));
        ASSERT_EQ(gKept.size(), 1u);
        EXPECT_EQ(gKept[0].name, "b");

        const ControlReply onlyReplaced = Call("artgen_keep", params);
        EXPECT_EQ(onlyReplaced.ErrorCode(), "invalid_params");
        EXPECT_NE(onlyReplaced.ErrorMessage().find("second"), String::npos) << onlyReplaced.ErrorMessage().c_str();

        params = JsonWith(params, "/set", R"("second")");
        ASSERT_EQ(Call("artgen_keep", params).ErrorCode(), "");
        ASSERT_EQ(gKept.size(), 2u);
        EXPECT_EQ(gKept[1].name, "a");
        EXPECT_EQ(gKept[1].seed, kSetSeed + 1);
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfTheOlderSetLeavesTheRowTheNewerSetHolds) {
        ASSERT_NO_FATAL_FAILURE(AcceptTwoSetsOfOneName());
        const ArtGenSet before = OnDisk("first");

        const ControlReply answerReply = AcceptAllCall("first");
        const JsonView answer = answerReply.Result();
        ASSERT_EQ(gAccepted.size(), 1u) << "b is written again, a is not";
        EXPECT_EQ(gAccepted[0].name, "b");
        const JsonView rows = MemberOf(answer, "accepted");
        ASSERT_EQ(rows.Size(), 2u);
        EXPECT_EQ(StringOf(ItemAt(rows, 0), "job"), "a");
        EXPECT_EQ(StringOf(ItemAt(rows, 0), "replacedBy"), "second");
        EXPECT_FALSE(MemberOf(ItemAt(rows, 0), "as").IsValid());
        EXPECT_EQ(KeysOf(ItemAt(rows, 0)), (Vector<String>{"job", "replacedBy"})) << "a row left as it is has none of the acceptor's members";
        EXPECT_EQ(StringOf(ItemAt(rows, 1), "as"), "b");
        EXPECT_EQ(KeysOf(ItemAt(rows, 1)), (Vector<String>{"alternatives", "as", "job", "seed", "sprite"})) << "a row written: the acceptor's members, as, seed and alternatives";
        EXPECT_EQ(OnDisk("first").jobs[0].accepted->date, before.jobs[0].accepted->date) << "the row's mark did not move";

        // A set whose row of that name was never accepted writes it: it is the newest then
        ASSERT_TRUE(Enqueue("third", "a").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("third"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("third"));
        gAccepted.clear();
        ASSERT_EQ(AcceptAllCall("third").ErrorCode(), "");
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_EQ(session.PickVariant("second", "a", kSetSeed), ArtGenSetEdit::WriteFailed) << "the third holds it now";
    }

    TEST_F(ArtGenSetSessionTest, APickOnAKeptRowIsNoVariantEvenWhenTheManifestStillListsTheOthers) {
        ASSERT_TRUE(Enqueue("old", "a").has_value());
        RunAndPickAll("old");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("old"));
        ASSERT_EQ(AcceptAllCall("old").ErrorCode(), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("old", "a"));
        ASSERT_EQ(Require(session.FindSet("old")).jobs[0].variants.size(), 2u);
        gAccepted.clear();

        String pick = Format(R"({{"set":"old","job":"a","seed":{}}})", kSetSeed);
        const ControlReply refused = Call("artgen_pick", pick);
        EXPECT_EQ(refused.ErrorCode(), "artgen_no_variant");
        EXPECT_NE(refused.ErrorMessage().find("kept"), String::npos) << refused.ErrorMessage().c_str();
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

        String params = R"({"set":"sounds"})";
        const ControlReply answerReply = Call("artgen_accept_set", params);
        const JsonView answer = answerReply.Result();
        ASSERT_TRUE(MemberOf(answer, "accepted").IsValid());
        EXPECT_EQ(StringOf(ItemAt(MemberOf(answer, "accepted"), 0), "clip"), "knock");
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

        String params = R"({"set":"early"})";
        EXPECT_EQ(Call("artgen_accept_set", params).ErrorCode(), "");
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

        String params = R"({"set":"unlisted"})";
        EXPECT_EQ(Call("artgen_accept_set", params).ErrorCode(), "");
        ASSERT_EQ(gAccepted.size(), 1u);
        EXPECT_FALSE(gAccepted[0].image);
    }

    // ---- an atlas group of the set (Sets/CLAUDE.md, "Accept all") ----

    namespace {

        /// A poll whose catalog gives the saved preset `wood` the group @p woodAtlas (the text of its `atlas` value; empty: no key)
        String PollWithWoodAtlas(StringView woodAtlas = {}) {
            const String wood = woodAtlas.empty() ? String(R"({"name":"wood","preset":{"preset":1,"prompt":"{subject}","workflow":"txt2img"},"title":"wood"})")
                                                  : Format(R"({{"name":"wood","preset":{{"atlas":{},"preset":1,"prompt":"{{subject}}","workflow":"txt2img"}},"title":"wood"}})", Str(woodAtlas));
            return Format(
                R"({{"generator":"fake","presets":[{},{{"kind":"audio","name":"knock","preset":{{"atlas":"pieces","preset":1,"prompt":"{{subject}}","workflow":"sound"}},"title":"knock"}}],"state":"ready"}})",
                wood);
        }

        String EnqueueParams(StringView set) {
            return Format(R"({{"preset":"wood","seed":{},"set":{},"subjects":"a\nb"}})", kSetSeed, Str(set));
        }

    }  // namespace

    TEST_F(ArtGenSetSessionTest, EnqueueRecordsTheAtlasGroupInTheSetItsManifestAndTheState) {
        ASSERT_EQ(Call("artgen_enqueue", JsonWith(EnqueueParams("grouped"), "/atlas", R"("pieces")")).ErrorCode(), "");
        EXPECT_EQ(session.FindSet("grouped")->atlas, "pieces");
        EXPECT_EQ(OnDisk("grouped").atlas, "pieces");

        EXPECT_EQ(RawOf(Call("artgen_set", R"({"set":"grouped"})").Result(), "atlas"), R"("pieces")");
        const ControlReply stateReply = Call("artgen_state");
        EXPECT_EQ(RawOf(ItemAt(MemberOf(stateReply.Result(), "sets"), 0), "atlas"), R"("pieces")");

        ASSERT_EQ(Call("artgen_enqueue", EnqueueParams("plain")).ErrorCode(), "");
        EXPECT_TRUE(session.FindSet("plain")->atlas.empty());
        const ControlReply plainState = Call("artgen_state");
        EXPECT_FALSE(MemberOf(ItemAt(MemberOf(plainState.Result(), "sets"), 1), "atlas").IsValid());
        ReloadFromDisk();
        EXPECT_EQ(session.FindSet("grouped")->atlas, "pieces") << "a reload of the sets keeps it";
    }

    TEST_F(ArtGenSetSessionTest, EnqueueTakesTheGroupOfTheSavedPresetAndAnEmptyStringClearsIt) {
        Call("artgen_poll", PollWithWoodAtlas("icons"));
        ASSERT_EQ(Call("artgen_enqueue", EnqueueParams("by_preset")).ErrorCode(), "");
        EXPECT_EQ(session.FindSet("by_preset")->atlas, "icons");

        ASSERT_EQ(Call("artgen_enqueue", JsonWith(EnqueueParams("cleared"), "/atlas", R"("")")).ErrorCode(), "");
        EXPECT_TRUE(session.FindSet("cleared")->atlas.empty());

        ASSERT_EQ(Call("artgen_enqueue", JsonWith(EnqueueParams("other"), "/atlas", R"("tiles")")).ErrorCode(), "");
        EXPECT_EQ(session.FindSet("other")->atlas, "tiles");

        Call("artgen_poll", PollWithWoodAtlas("changed"));
        EXPECT_EQ(session.FindSet("by_preset")->atlas, "icons") << "editing the preset later does not reach the set";
        Call("artgen_poll", PollWithWoodAtlas());
        ASSERT_EQ(Call("artgen_enqueue", EnqueueParams("none")).ErrorCode(), "");
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
        Call("artgen_poll", PollWithWoodAtlas());
        String params = R"({"preset":"knock","set":"heard","subjects":"click"})";
        EXPECT_EQ(Call("artgen_enqueue", params).ErrorCode(), "") << "a group the sound preset names is ignored";
        EXPECT_TRUE(session.FindSet("heard")->atlas.empty());

        params = JsonWith(params, "/set", R"("refused")");
        params = JsonWith(params, "/atlas", R"("pieces")");
        const ControlReply refused = Call("artgen_enqueue", params);
        EXPECT_EQ(refused.ErrorCode(), "invalid_params");
        EXPECT_NE(refused.ErrorMessage().find("only an image set can have an atlas"), String::npos) << refused.ErrorMessage().c_str();
        EXPECT_EQ(session.FindSet("refused"), nullptr);
    }

    TEST_F(ArtGenSetSessionTest, EnqueueRefusesABadAtlasAndKeepsNothing) {
        const String longName = Str(String(65, 'a'));
        for (const StringView bad : {StringView(R"("a b")"), StringView(R"("../x")"), StringView("5"), StringView("[]"), StringView("true"), StringView(longName)}) {
            EXPECT_EQ(Call("artgen_enqueue", JsonWith(EnqueueParams("bad"), "/atlas", bad)).ErrorCode(), "invalid_params") << String(bad).c_str();
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
        ASSERT_EQ(AcceptAllCall("grouped").ErrorCode(), "");
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

        const ControlReply answerReply = AcceptAllCall("grouped");
        const JsonView answer = answerReply.Result();
        ASSERT_EQ(gPlans.size(), 3u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_EQ(plan.atlas, "pieces");
            EXPECT_TRUE(plan.deferPack);
        }
        ASSERT_EQ(gPacks.size(), 1u);
        EXPECT_EQ(gPacks[0].group, "pieces");
        EXPECT_EQ(gPacks[0].acceptedBefore, 3u) << "after the last row";
        const JsonView atlas = MemberOf(answer, "atlas");
        ASSERT_TRUE(atlas.IsValid());
        EXPECT_EQ(StringOf(atlas, "name"), "pieces");
        EXPECT_EQ(MemberOf(answer, "accepted").Size(), 3u);
        for (const ArtGenSetJob& job : OnDisk("grouped").jobs) {
            EXPECT_TRUE(job.accepted.has_value());
        }
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfASetWithoutAGroupNeverCallsThePacker) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("loose", "a\nb"));
        const ControlReply answerReply = AcceptAllCall("loose");
        const JsonView answer = answerReply.Result();
        EXPECT_TRUE(gPacks.empty());
        ASSERT_EQ(gPlans.size(), 2u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_TRUE(plan.atlas.empty());
            EXPECT_FALSE(plan.deferPack);
        }
        EXPECT_FALSE(MemberOf(answer, "atlas").IsValid()) << "no atlas member when none was packed";
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfAGroupSetWithNoPackerLetsEveryRowPackByItself) {
        session.SetAtlasPacker(nullptr);
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        const ControlReply answerReply = AcceptAllCall("grouped");
        const JsonView answer = answerReply.Result();
        ASSERT_EQ(gPlans.size(), 2u);
        for (const ArtGenImagePlan& plan : gPlans) {
            EXPECT_EQ(plan.atlas, "pieces");
            EXPECT_FALSE(plan.deferPack);
        }
        EXPECT_TRUE(gPacks.empty());
        EXPECT_FALSE(MemberOf(answer, "atlas").IsValid());
    }

    TEST_F(ArtGenSetSessionTest, AcceptAllOfAGroupSetPacksNothingWhenEveryRowIsKeptOrReplaced) {
        ASSERT_TRUE(EnqueueInGroup(session, "first", "a\nb", "pieces").has_value());
        ASSERT_TRUE(EnqueueInGroup(session, "second", "a", "pieces").has_value());
        ASSERT_NO_FATAL_FAILURE(RunAndPickAll("first"));
        ASSERT_EQ(session.PickVariant("second", "a", kSetSeed + 1), ArtGenSetEdit::Ok);
        ASSERT_NO_FATAL_FAILURE(MakeFiles("first"));
        ASSERT_NO_FATAL_FAILURE(MakeFiles("second"));
        ASSERT_EQ(AcceptAllCall("first").ErrorCode(), "");
        ASSERT_EQ(AcceptAllCall("second").ErrorCode(), "");
        ASSERT_NO_FATAL_FAILURE(KeepOnDisk("first", "b"));
        gAccepted.clear();
        gPacks.clear();

        // Row a of the first set is held by the second, row b is kept: nothing is written, so nothing is packed
        const ControlReply answerReply = AcceptAllCall("first");
        const JsonView answer = answerReply.Result();
        EXPECT_TRUE(gAccepted.empty());
        EXPECT_TRUE(gPacks.empty());
        EXPECT_FALSE(MemberOf(answer, "atlas").IsValid());
    }

    TEST_F(ArtGenSetSessionTest, AFailedRowOfAGroupSetPacksNothingAndTheNextCallFinishesAndPacksOnce) {
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb\nc", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));

        gFailOn = "c";
        const ControlReply refused = AcceptAllCall("grouped");
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
        EXPECT_NE(message.find("accepted before it: a, b"), String::npos) << message.c_str();
        EXPECT_NE(message.find("atlas 'pieces' is not packed"), String::npos) << message.c_str();
        EXPECT_TRUE(gPacks.empty());
        const ArtGenSet disk = OnDisk("grouped");
        EXPECT_TRUE(disk.jobs[0].accepted.has_value());
        EXPECT_TRUE(disk.jobs[1].accepted.has_value());
        EXPECT_FALSE(disk.jobs[2].accepted.has_value());

        gFailOn.clear();
        const ControlReply againReply = AcceptAllCall("grouped");
        const JsonView again = againReply.Result();
        ASSERT_EQ(gPacks.size(), 1u);
        EXPECT_EQ(gPacks[0].acceptedBefore, 5u) << "the first two rows replace themselves";
        EXPECT_TRUE(MemberOf(again, "atlas").IsValid());
    }

    TEST_F(ArtGenSetSessionTest, AFailedFirstRowOfAGroupSetSaysNothingAboutTheAtlas) {
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));
        gFailOn = "a";
        const ControlReply refused = AcceptAllCall("grouped");
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        EXPECT_EQ(refused.ErrorMessage().find("atlas"), String::npos) << refused.ErrorMessage().c_str();
        EXPECT_TRUE(gPacks.empty());
    }

    TEST_F(ArtGenSetSessionTest, AFailedPackKeepsTheRowsAcceptedAndNamesTheRepair) {
        ASSERT_TRUE(EnqueueInGroup(session, "grouped", "a\nb", "pieces").has_value());
        RunAndPickAll("grouped");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("grouped"));

        gPackFail = "the atlas file is locked";
        const ControlReply refused = AcceptAllCall("grouped");
        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
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
        const ControlReply againReply = AcceptAllCall("grouped");
        const JsonView again = againReply.Result();
        EXPECT_EQ(gPacks.size(), 2u);
        EXPECT_TRUE(MemberOf(again, "atlas").IsValid());
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

        const ControlReply answerReply = AcceptAllCall("grouped");
        const JsonView answer = answerReply.Result();

        const Vector<String> expected{"begin", "accept a", "accept b", "accept c", "pack pieces", "end"};
        EXPECT_EQ(gEvents, expected) << "the pack is a step of the batch, so the library is written once, by its end";
        EXPECT_EQ(markedWhileEnding, 0u);
        EXPECT_TRUE(MemberOf(answer, "atlas").IsValid());
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

        const ControlReply refused = AcceptAllCall("grouped");

        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        const String message = refused.ErrorMessage();
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

        const ControlReply refused = AcceptAllCall("grouped");

        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
        EXPECT_NE(refused.ErrorMessage().find("'pieces' is not packed"), String::npos) << refused.ErrorMessage().c_str();
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

        const ControlReply refused = AcceptAllCall("grouped");

        EXPECT_EQ(refused.ErrorCode(), "artgen_accept_failed");
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
        const ControlReply answerReply = AcceptAllCall("heard");
        const JsonView answer = answerReply.Result();
        EXPECT_TRUE(gPacks.empty());
        EXPECT_FALSE(MemberOf(answer, "atlas").IsValid());
    }

    // ---- one frame for the whole set (Sets/CLAUDE.md, "Accept all") ----

    TEST_F(ArtGenSetSessionTest, TheFirstAcceptAllMeasuresOnceOverEveryVariantAndStoresTheFrame) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        EXPECT_FALSE(Require(session.FindSet("framed")).processing.has_value());

        ASSERT_EQ(AcceptAllCall("framed").ErrorCode(), "");

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

        String params = R"({"set":"framed"})";
        const ControlReply shownReply = Call("artgen_set", params);
        const JsonView shown = shownReply.Result();
        const JsonView processing = MemberOf(shown, "processing");
        ASSERT_TRUE(processing.IsValid());
        EXPECT_EQ(IntOf(MemberOf(processing, "frame"), "outWidth"), Optional<int64_t>(150));
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
        ASSERT_EQ(AcceptAllCall("drafted").ErrorCode(), "");

        ASSERT_EQ(gMeasures.size(), 1u);
        EXPECT_EQ(gMeasures[0].settings, (ArtGenImageSettings{true, 7, 128}));
        EXPECT_EQ(Require(session.FindSet("drafted")).processing->settings, (ArtGenImageSettings{true, 7, 128}));
    }

    TEST_F(ArtGenSetSessionTest, ASecondAcceptAllAndASwitchKeepTheFrameAndMeasureNothing) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        ASSERT_EQ(AcceptAllCall("framed").ErrorCode(), "");
        const Optional<ArtGenSetProcessing> stored = Require(session.FindSet("framed")).processing;
        ASSERT_TRUE(stored.has_value());
        gMeasures.clear();
        gAccepted.clear();
        gPlans.clear();

        ASSERT_EQ(AcceptAllCall("framed").ErrorCode(), "");
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
        ASSERT_EQ(AcceptAllCall("framed").ErrorCode(), "");

        Call("artgen_poll", JsonWith(kReadyPoll, "/presets", R"([{"name":"wood","preset":{"accept":{"maxSize":0,"trim":false}},"title":"wood"}])"));

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
        ASSERT_EQ(AcceptAllCall("framed").ErrorCode(), "");

        String params = R"({"set":"framed","job":"a"})";
        ASSERT_EQ(Call("artgen_keep", params).ErrorCode(), "");
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

        ASSERT_EQ(AcceptAllCall("old").ErrorCode(), "");
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
        ASSERT_EQ(AcceptAllCall("plain").ErrorCode(), "");

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

        const ControlReply response = AcceptAllCall("framed");
        EXPECT_EQ(response.ErrorCode(), "artgen_accept_failed");
        EXPECT_NE(response.ErrorMessage().find("nothing drawn in any"), String::npos) << response.ErrorMessage().c_str();
        EXPECT_TRUE(gAccepted.empty());
        const ArtGenSet disk = OnDisk("framed");
        EXPECT_FALSE(disk.processing.has_value());
        EXPECT_FALSE(disk.jobs[0].accepted.has_value());

        gMeasureFail.clear();
        EXPECT_EQ(AcceptAllCall("framed").ErrorCode(), "") << "the next call measures again";
        EXPECT_TRUE(Require(session.FindSet("framed")).processing.has_value());
    }

    TEST_F(ArtGenSetSessionTest, AFailedFirstRowStoresNoFrame) {
        ASSERT_TRUE(Enqueue("framed", "a\nb").has_value());
        RunAndPickAll("framed");
        ASSERT_NO_FATAL_FAILURE(MakeFiles("framed"));
        gFailOn = "a";

        EXPECT_EQ(AcceptAllCall("framed").ErrorCode(), "artgen_accept_failed");
        EXPECT_FALSE(Require(session.FindSet("framed")).processing.has_value());
        EXPECT_FALSE(OnDisk("framed").processing.has_value());
        EXPECT_FALSE(IsLegacyAccepted(Require(session.FindSet("framed")))) << "nothing was written, so the set is not legacy";

        gFailOn.clear();
        ASSERT_EQ(AcceptAllCall("framed").ErrorCode(), "");
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
        ASSERT_EQ(AcceptAllCall("fix").ErrorCode(), "");
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

        String params = R"({"preset":"wood","subjects":"a","set":"fix"})";
        for (const StringView bad : {StringView("3"), StringView(R"("")"), StringView(R"("nowhere")")}) {
            params = JsonWith(params, "/frameOf", bad);
            EXPECT_EQ(Call("artgen_enqueue", params).ErrorCode(), "invalid_params") << String(bad).c_str();
        }
        EXPECT_EQ(session.FindSet("fix"), nullptr);

        params = JsonWith(params, "/frameOf", R"("old")");
        EXPECT_EQ(Call("artgen_enqueue", params).ErrorCode(), "");
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
        EXPECT_EQ(AcceptAllCall("sounds").ErrorCode(), "");
        EXPECT_TRUE(gMeasures.empty());
        EXPECT_FALSE(Require(session.FindSet("sounds")).processing.has_value());
    }

    namespace {

        constexpr StringView kStyleSource = "assets/artgen/white_knight.png";

        /// The reference the set carries, as the writer writes it (members in byte order)
        String StyleReferenceText() {
            return Format(R"({{"image":{},"mode":"style","weight":0.7}})", Str(kStyleSource));
        }

    }  // namespace

    TEST_F(ArtGenSetSessionTest, AReferenceOfAnEnqueuedSetGoesOutWithEveryJobAndIsKeptInTheSet) {
        const String params = JsonWith(EnqueueParams("styled"), "/reference", StyleReferenceText());
        ASSERT_EQ(Call("artgen_enqueue", params).ErrorCode(), "");

        const ControlReply first = Poll();
        const JsonView sent = RequestOf(first);
        ASSERT_TRUE(sent.IsValid());
        EXPECT_EQ(RawOf(sent, "reference"), StyleReferenceText());
        const ControlReply runState = Call("artgen_state");
        EXPECT_EQ(RawOf(MemberOf(runState.Result(), "run"), "reference"), StyleReferenceText()) << "the run in work shows it";
        Finish(sent, "ok");
        const ControlReply second = Poll();
        ASSERT_TRUE(RequestOf(second).IsValid());
        EXPECT_EQ(RawOf(RequestOf(second), "reference"), StyleReferenceText()) << "every job, not only the first";
        Finish(RequestOf(second), "ok");

        const ControlReply stateReply = Call("artgen_state");
        const JsonView state = stateReply.Result();
        EXPECT_EQ(RawOf(ItemAt(MemberOf(state, "sets"), 0), "reference"), StyleReferenceText());
        EXPECT_EQ(RawOf(Call("artgen_set", R"({"set":"styled"})").Result(), "reference"), StyleReferenceText());
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

        const ControlReply first = Poll();
        Finish(RequestOf(first), "failed", "the generator fell over");
        const ControlReply second = Poll();
        Finish(RequestOf(second), "ok");
        ASSERT_EQ(session.RetryJobs("styled", {}), ArtGenSetEdit::Ok);
        const ControlReply retried = Poll();
        const JsonView retriedRequest = RequestOf(retried);
        ASSERT_TRUE(retriedRequest.IsValid());
        EXPECT_EQ(StringOf(retriedRequest, "name"), "a");
        const JsonView retriedReference = MemberOf(retriedRequest, "reference");
        ASSERT_TRUE(retriedReference.IsValid()) << "a Retry runs with the set's reference";
        EXPECT_EQ(StringOf(retriedReference, "image"), String(kStyleSource));
        EXPECT_FALSE(MemberOf(retriedReference, "weight").IsValid()) << "no weight: the mode's default, as the set was made";
        Finish(retriedRequest, "ok");

        ASSERT_EQ(MoreCall("styled").ErrorCode(), "");
        const ControlReply more = Poll();
        const JsonView moreRequest = RequestOf(more);
        ASSERT_TRUE(moreRequest.IsValid());
        EXPECT_NE(IntOf(moreRequest, "seed"), Optional<int64_t>(kSetSeed)) << "this is the more-run";
        EXPECT_EQ(StringOf(MemberOf(moreRequest, "reference"), "mode"), "style");
    }

    TEST_F(ArtGenSetSessionTest, ASetWithNoReferenceSendsNoneAndTheSlotIsNotTakenForOne) {
        session.SetReferenceSlot(ArtGenReference{"init", String(kStyleSource), {}});
        ASSERT_TRUE(Enqueue("plain", "a").has_value());
        EXPECT_FALSE(session.FindSet("plain")->reference.has_value()) << "only the panel passes the slot";
        const ControlReply first = Poll();
        ASSERT_TRUE(RequestOf(first).IsValid());
        EXPECT_FALSE(MemberOf(RequestOf(first), "reference").IsValid());
        const ControlReply plainState = Call("artgen_state");
        EXPECT_FALSE(MemberOf(ItemAt(MemberOf(plainState.Result(), "sets"), 0), "reference").IsValid());
        const JsonDoc manifest(Require(ReadTextFile(ManifestPath(_root, "plain"))));
        EXPECT_EQ(RawOf(manifest, "reference"), "null");
    }

    TEST_F(ArtGenSetSessionTest, ABadReferenceOfEnqueueIsInvalidParamsThatNamesTheKey) {
        constexpr StringView kGood = R"({"preset":"wood","subjects":"a"})";
        const StringView bad[] = {R"("yes")", R"({"image":"a.png"})", R"({"mode":"style"})", R"({"image":"a.png","mode":"style","weight":"x"})"};
        for (const StringView reference : bad) {
            const ControlReply response = Call("artgen_enqueue", JsonWith(kGood, "/reference", reference));
            EXPECT_EQ(response.ErrorCode(), "invalid_params") << String(reference).c_str();
            EXPECT_TRUE(response.ErrorMessage().find("'reference") != String::npos) << response.ErrorMessage();
        }
        EXPECT_TRUE(session.GetSets().empty()) << "a refusal keeps nothing";
        EXPECT_EQ(Call("artgen_enqueue", JsonWith(kGood, "/reference", "null")).ErrorCode(), "") << "a null reference is none";
    }

    TEST_F(ArtGenSetSessionTest, MoreQueuesEveryDoneRowOnTheGivenSeedAndTheRunAppendsToTheVariants) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        constexpr int64_t kMoreSeed = 5000;
        const ControlReply answerReply = MoreCall("m", Format(R"({{"count":2,"seed":{}}})", kMoreSeed));
        const JsonView answer = answerReply.Result();
        EXPECT_EQ(StringOf(answer, "set"), "m");
        EXPECT_EQ(IntOf(answer, "seed"), Optional<int64_t>(kMoreSeed));
        EXPECT_EQ(IntOf(answer, "count"), Optional<int64_t>(2));
        const JsonView names = MemberOf(answer, "jobs");
        ASSERT_TRUE(names.IsValid());
        ASSERT_EQ(names.Size(), 2u);
        EXPECT_EQ(ItemAt(names, 0).Raw(), R"("a")");
        EXPECT_EQ(ItemAt(names, 1).Raw(), R"("b")");
        ASSERT_TRUE(MemberOf(answer, "skipped").IsValid());
        EXPECT_EQ(MemberOf(answer, "skipped").Size(), 0u);

        EXPECT_TRUE(session.IsBusy());
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Pending);
            ASSERT_TRUE(job.more.has_value());
            EXPECT_EQ(job.more->seed, kMoreSeed);
            EXPECT_EQ(job.more->count, 2);
            EXPECT_EQ(job.variants.size(), 2u) << "nothing is dropped while the row waits";
        }
        const ControlReply stateReply = Call("artgen_state");
        const JsonView state = stateReply.Result();
        EXPECT_EQ(StringOf(ItemAt(MemberOf(state, "sets"), 0), "state"), "Active");
        const ControlReply shownReply = Call("artgen_set", R"({"set":"m"})");
        const JsonView shownMore = MemberOf(ItemAt(MemberOf(shownReply.Result(), "jobs"), 0), "more");
        EXPECT_EQ(IntOf(shownMore, "seed"), Optional<int64_t>(kMoreSeed));
        EXPECT_EQ(IntOf(shownMore, "count"), Optional<int64_t>(2));

        const ControlReply first = Poll();
        const JsonView request = RequestOf(first);
        ASSERT_TRUE(request.IsValid());
        EXPECT_EQ(IntOf(request, "seed"), Optional<int64_t>(kMoreSeed));
        EXPECT_EQ(IntOf(request, "count"), Optional<int64_t>(2));
        EXPECT_EQ(StringOf(request, "outDir"), "m/a");
        EXPECT_EQ(StringOf(request, "name"), "a");
        Finish(request, "ok");
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
        EXPECT_EQ(StringOf(ItemAt(MemberOf(Call("artgen_state").Result(), "sets"), 0), "state"), "Complete");
    }

    TEST_F(ArtGenSetSessionTest, MoreDrawsOneFreshSeedForTheRowsOfTheCall) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb\nc"));
        const ControlReply answerReply = MoreCall("m");
        const JsonView answer = answerReply.Result();
        EXPECT_EQ(IntOf(answer, "count"), Optional<int64_t>(1)) << "the default is one variant per row";
        const int64_t seed = Require(IntOf(answer, "seed"));
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
        String extra = R"({"count":3})";
        extra = JsonWith(extra, "/seed", Format("{}", kSetSeed - 2));  // 38, 39, 40: meets the first variant
        const ControlReply refused = MoreCall("m", extra);
        EXPECT_EQ(refused.ErrorCode(), "invalid_params");
        EXPECT_NE(refused.ErrorMessage().find("'a'"), String::npos) << refused.ErrorMessage().c_str();
        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(session.Version(), before);
        for (const ArtGenSetJob& job : OnDisk("m").jobs) {
            EXPECT_EQ(job.state, ArtGenSetJobState::Done);
            EXPECT_FALSE(job.more.has_value());
        }

        extra = JsonWith(extra, "/seed", "-1");
        EXPECT_EQ(MoreCall("m", extra).ErrorCode(), "invalid_params");
        EXPECT_FALSE(session.IsBusy());
        extra = JsonWith(extra, "/seed", Format("{}", kSetSeed - 3));  // 37, 38, 39: ends right before the first variant
        EXPECT_EQ(MoreCall("m", extra).ErrorCode(), "");
    }

    TEST_F(ArtGenSetSessionTest, AKeptRowIsLeftOutAndAnAllKeptSetIsNothingToRun) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(KeepCall("m", "a").ErrorCode(), "");
        const ControlReply answerReply = MoreCall("m");
        const JsonView answer = answerReply.Result();
        const JsonView names = MemberOf(answer, "jobs");
        ASSERT_TRUE(names.IsValid());
        ASSERT_EQ(names.Size(), 1u);
        EXPECT_EQ(ItemAt(names, 0).Raw(), R"("b")");
        const JsonView skipped = MemberOf(answer, "skipped");
        ASSERT_TRUE(skipped.IsValid());
        ASSERT_EQ(skipped.Size(), 1u);
        EXPECT_EQ(StringOf(ItemAt(skipped, 0), "job"), "a");
        EXPECT_EQ(StringOf(ItemAt(skipped, 0), "reason"), "kept");
        EXPECT_EQ(KeysOf(answer), (Vector<String>{"count", "jobs", "seed", "set", "skipped"}));
        EXPECT_EQ(KeysOf(ItemAt(skipped, 0)), (Vector<String>{"job", "reason"}));
        const ArtGenSetJob* kept = FindJob(Require(session.FindSet("m")), "a");
        EXPECT_EQ(kept->state, ArtGenSetJobState::Done);
        EXPECT_FALSE(kept->more.has_value());
        EXPECT_EQ(kept->variants.size(), 1u) << "Keep trimmed it and More leaves it alone";
        ASSERT_NO_FATAL_FAILURE(RunAll());
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "a")).variants.size(), 1u);
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).variants.size(), 3u);

        ASSERT_EQ(KeepCall("m").ErrorCode(), "");
        const ControlReply refused = MoreCall("m");
        EXPECT_EQ(refused.ErrorCode(), "invalid_params");
        EXPECT_NE(refused.ErrorMessage().find("a (kept)"), String::npos) << refused.ErrorMessage().c_str();
        EXPECT_NE(refused.ErrorMessage().find("b (kept)"), String::npos) << refused.ErrorMessage().c_str();
        EXPECT_FALSE(session.IsBusy());
    }

    TEST_F(ArtGenSetSessionTest, KeepWaitsForARunningMoreRunAndNoKeptRowGrows) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(MoreCall("m").ErrorCode(), "");
        const uint64_t before = session.Version();

        EXPECT_EQ(KeepCall("m").ErrorCode(), "artgen_busy");
        EXPECT_EQ(KeepCall("m", "a").ErrorCode(), "artgen_busy");
        EXPECT_EQ(session.Version(), before);
        for (const ArtGenSetJob& job : OnDisk("m").jobs) {
            EXPECT_FALSE(job.accepted->kept);
        }

        ASSERT_NO_FATAL_FAILURE(RunAll());
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.variants.size(), 3u);
            EXPECT_FALSE(job.accepted->kept);
        }
        ASSERT_EQ(KeepCall("m").ErrorCode(), "");
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.variants.size(), 1u);
        }
    }

    TEST_F(ArtGenSetSessionTest, FailedAndCancelledRowsAreLeftOutForRetry) {
        ASSERT_TRUE(Enqueue("m", "a\nb\nc").has_value());
        Finish(RequestOf(Poll()), "ok");
        Finish(RequestOf(Poll()), "failed", "boom");
        session.CancelJob("m", "c");
        const ControlReply answerReply = MoreCall("m");
        const JsonView answer = answerReply.Result();
        ASSERT_EQ(MemberOf(answer, "jobs").Size(), 1u);
        const JsonView skipped = MemberOf(answer, "skipped");
        ASSERT_EQ(skipped.Size(), 2u);
        EXPECT_EQ(StringOf(ItemAt(skipped, 0), "reason"), "failed");
        EXPECT_EQ(StringOf(ItemAt(skipped, 1), "reason"), "cancelled");
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).state, ArtGenSetJobState::Failed);
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).error, "boom");
    }

    TEST_F(ArtGenSetSessionTest, MoreNarrowsToTheNamedRowsAndNamesTheUnknownOne) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb\nc"));
        String extra = R"({"jobs":["c"],"seed":7000})";
        const ControlReply answerReply = MoreCall("m", extra);
        const JsonView answer = answerReply.Result();
        ASSERT_EQ(MemberOf(answer, "jobs").Size(), 1u);
        EXPECT_EQ(ItemAt(MemberOf(answer, "jobs"), 0).Raw(), R"("c")");
        EXPECT_EQ(MemberOf(answer, "skipped").Size(), 0u) << "a row that was not named is not a skipped one";
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "a")).state, ArtGenSetJobState::Done);
        ASSERT_NO_FATAL_FAILURE(RunAll());

        extra = JsonWith(extra, "/jobs", R"(["c","nope"])");
        const ControlReply unknown = MoreCall("m", extra);
        EXPECT_EQ(unknown.ErrorCode(), "artgen_no_set");
        EXPECT_NE(unknown.ErrorMessage().find("nope"), String::npos);
        extra = JsonWith(extra, "/jobs", "[]");
        EXPECT_EQ(MoreCall("m", extra).ErrorCode(), "invalid_params");
        extra = JsonWith(extra, "/jobs", R"("c")");
        EXPECT_EQ(MoreCall("m", extra).ErrorCode(), "invalid_params");
        extra = JsonWith(extra, "/jobs", R"([3])");
        EXPECT_EQ(MoreCall("m", extra).ErrorCode(), "invalid_params");
        EXPECT_FALSE(session.IsBusy());
    }

    TEST_F(ArtGenSetSessionTest, MoreRefusesABadCountAnUnknownSetAndAnActiveSet) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        for (const int64_t count : {int64_t{0}, int64_t{17}, int64_t{-1}, int64_t{1} << 40}) {
            String extra = Format(R"({{"count":{}}})", count);
            EXPECT_EQ(MoreCall("m", extra).ErrorCode(), "invalid_params") << count;
        }
        String text = R"({"count":"two"})";
        EXPECT_EQ(MoreCall("m", text).ErrorCode(), "invalid_params");
        String fractional = R"({"count":1.5})";
        EXPECT_EQ(MoreCall("m", fractional).ErrorCode(), "invalid_params");
        String badSeed = R"({"seed":"x"})";
        EXPECT_EQ(MoreCall("m", badSeed).ErrorCode(), "invalid_params");
        EXPECT_EQ(MoreCall("nope").ErrorCode(), "artgen_no_set");
        EXPECT_EQ(Call("artgen_more").ErrorCode(), "invalid_params");
        EXPECT_FALSE(session.IsBusy());

        ASSERT_EQ(MoreCall("m").ErrorCode(), "");
        String two = R"({"count":2})";
        EXPECT_EQ(MoreCall("m", two).ErrorCode(), "artgen_busy");
        for (const ArtGenSetJob& job : Require(session.FindSet("m")).jobs) {
            EXPECT_EQ(job.more->count, 1) << "a refusal changes nothing";
        }
    }

    TEST_F(ArtGenSetSessionTest, ASingleRunInWorkDoesNotRefuseMoreAndTheRowsWait) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a"));
        ASSERT_TRUE(session.Request("wood", "a lone sprite", 1, 9, false).has_value());
        EXPECT_EQ(MoreCall("m").ErrorCode(), "");
        EXPECT_EQ(Require(session.FindSet("m")).jobs[0].state, ArtGenSetJobState::Pending);
    }

    TEST_F(ArtGenSetSessionTest, ACancelledMoreRunLeavesTheRowDoneWithItsVariantsAndPick) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb\nc"));
        ASSERT_EQ(MoreCall("m").ErrorCode(), "");
        const ControlReply first = Poll();
        ASSERT_TRUE(RequestOf(first).IsValid());
        EXPECT_EQ(session.CancelSet("m"), ArtGenSetEdit::Ok);
        // The pending rows end at once; the running one when the service says so
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).state, ArtGenSetJobState::Done);
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "b")).error, "cancelled");
        EXPECT_EQ(Require(FindJob(Require(session.FindSet("m")), "a")).state, ArtGenSetJobState::Running);
        EXPECT_EQ(IntOf(Poll().Result(), "cancel"), IntOf(RequestOf(first), "id"));
        Finish(RequestOf(first), "cancelled");

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
        String retry = R"({"set":"m"})";
        EXPECT_EQ(IntOf(Call("artgen_retry", retry).Result(), "retried"), Optional<int64_t>(0));
        EXPECT_FALSE(session.IsBusy());
        EXPECT_EQ(Require(session.FindSet("m")).jobs[0].variants.size(), 2u);
    }

    TEST_F(ArtGenSetSessionTest, ACancelOfOneRunningMoreJobLeavesTheOthersToRun) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(MoreCall("m").ErrorCode(), "");
        const ControlReply first = Poll();
        ASSERT_TRUE(RequestOf(first).IsValid());
        EXPECT_EQ(session.CancelJob("m", "a"), ArtGenSetEdit::Ok);
        Poll();
        Finish(RequestOf(first), "cancelled");
        const ArtGenSetJob* a = FindJob(Require(session.FindSet("m")), "a");
        EXPECT_EQ(a->state, ArtGenSetJobState::Done);
        EXPECT_EQ(a->variants.size(), 2u);
        EXPECT_EQ(a->pick, Optional<int64_t>(kSetSeed + 1));
        const ControlReply next = Poll();
        ASSERT_TRUE(RequestOf(next).IsValid());
        EXPECT_EQ(StringOf(RequestOf(next), "name"), "b");
    }

    TEST_F(ArtGenSetSessionTest, AFailedMoreRunKeepsTheImagesItMadeAndTheRest) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a"));
        ASSERT_EQ(MoreCall("m", R"({"count":3,"seed":6000})").ErrorCode(), "");
        const JsonView request = RequestOf(pollReply = Poll());
        ASSERT_TRUE(request.IsValid());
        const uint32_t id = static_cast<uint32_t>(Require(IntOf(request, "id")));
        Call("artgen_image", Report(id, Format(R"("path":{},"seed":6000)", Str(Format("{}/artgen/out/m/a/a_6000.png", _root.View())))));
        Call("artgen_done", Report(id, R"("error":"out of memory","status":"failed")"));

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
        ASSERT_EQ(MoreCall("m").ErrorCode(), "");
        ASSERT_TRUE(RequestOf(Poll()).IsValid());
        session.RequestServiceStop();
        EXPECT_EQ(BoolOf(Poll().Result(), "shutdown"), Optional<bool>(true));
        const ArtGenSet disk = OnDisk("m");
        EXPECT_EQ(disk.jobs[0].state, ArtGenSetJobState::Done);
        EXPECT_EQ(disk.jobs[0].error, "the service was stopped");
        EXPECT_EQ(disk.jobs[0].variants.size(), 2u);
        EXPECT_EQ(disk.jobs[0].pick, Optional<int64_t>(kSetSeed + 1));
        EXPECT_EQ(disk.jobs[1].state, ArtGenSetJobState::Pending);
        EXPECT_TRUE(disk.jobs[1].more.has_value()) << "it goes when a service polls again";
        const ControlReply again = Poll();
        ASSERT_TRUE(RequestOf(again).IsValid());
        EXPECT_EQ(StringOf(RequestOf(again), "name"), "b");
    }

    TEST_F(ArtGenSetSessionTest, ARestartInTheMiddleOfAMoreRunShowsDoneRowsWithTheNote) {
        ASSERT_NO_FATAL_FAILURE(FinishAndAccept("m", "a\nb"));
        ASSERT_EQ(MoreCall("m").ErrorCode(), "");
        ASSERT_TRUE(RequestOf(Poll()).IsValid());

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
        ASSERT_EQ(MoreCall("m", R"({"seed":8000})").ErrorCode(), "");
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
        ASSERT_EQ(MoreCall("m").ErrorCode(), "");
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_NO_FATAL_FAILURE(MakeFiles("m"));
        ASSERT_EQ(AcceptAllCall("m").ErrorCode(), "");
        ASSERT_EQ(gMeasures.size(), 1u);
        EXPECT_EQ(gMeasures[0].files.size(), 6u) << "two rows of two old and one new variant";
        ASSERT_EQ(gAccepted.size(), 2u);
        EXPECT_EQ(gAccepted[0].seed, kSetSeed + 1) << "the pick, not a new variant";
        EXPECT_EQ(gAccepted[1].seed, kSetSeed + 1);
    }

    TEST_F(ArtGenSetSessionTest, AFullRowIsLeftOutWhileTheOthersGrow) {
        ASSERT_TRUE(Enqueue("m", "a\nb", kArtGenSetMaxCount).has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        const String grow = Format(R"({{"count":{},"jobs":["a"]}})", kArtGenSetMaxCount);
        for (int round = 0; round < 3; ++round) {
            ASSERT_EQ(MoreCall("m", grow).ErrorCode(), "") << round;
            ASSERT_NO_FATAL_FAILURE(RunAll());
        }
        EXPECT_EQ(static_cast<int32_t>(Require(FindJob(Require(session.FindSet("m")), "a")).variants.size()), kArtGenJobMaxVariants);

        const ControlReply answerReply = MoreCall("m");
        const JsonView answer = answerReply.Result();
        ASSERT_EQ(MemberOf(answer, "jobs").Size(), 1u);
        EXPECT_EQ(ItemAt(MemberOf(answer, "jobs"), 0).Raw(), R"("b")");
        const JsonView skipped = MemberOf(answer, "skipped");
        ASSERT_EQ(skipped.Size(), 1u);
        EXPECT_EQ(StringOf(ItemAt(skipped, 0), "job"), "a");
        EXPECT_EQ(StringOf(ItemAt(skipped, 0), "reason"), "full");
    }

    // The service regenerates what failed its checks: a job asked for `count` images may come back with more, on the seeds after its own

    TEST_F(ArtGenSetSessionTest, AJobThatGetsMoreImagesThanItsCountEndsDoneWithAllOfThem) {
        ASSERT_TRUE(Enqueue("m", "a", 2).has_value());
        const JsonView request = RequestOf(pollReply = Poll());
        ASSERT_TRUE(request.IsValid());
        ASSERT_EQ(IntOf(request, "count"), Optional<int64_t>(2));
        FinishWithImages(request, 4);

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
        FinishWithImages(RequestOf(pollReply = Poll()), 4);  // seeds 40..43, the last two are extras
        const ArtGenSetJob& job = session.FindSet("m")->jobs[0];
        ASSERT_EQ(job.variants.size(), 4u);

        // An explicit seed whose range meets an extra is refused and changes nothing
        const Pair<int64_t, int64_t> meeting[] = {{42, 1}, {43, 2}, {41, 3}, {39, 5}};
        for (const auto& [seed, count] : meeting) {
            String extra = Format(R"({{"seed":{},"count":{}}})", seed, count);
            EXPECT_EQ(MoreCall("m", extra).ErrorCode(), "invalid_params") << seed << " x " << count;
            EXPECT_FALSE(session.IsBusy());
            EXPECT_FALSE(job.more.has_value());
            EXPECT_EQ(job.variants.size(), 4u);
        }

        // One that does not is queued
        String clear = R"({"seed":44,"count":2})";
        ASSERT_EQ(MoreCall("m", clear).ErrorCode(), "");
        EXPECT_TRUE(job.more.has_value());
        ASSERT_NO_FATAL_FAILURE(RunAll());
        ASSERT_EQ(job.variants.size(), 6u);
        EXPECT_EQ(job.variants[4].seed, 44);
        EXPECT_EQ(job.variants[5].seed, 45);

        // A drawn seed meets no variant the row holds, the extras among them
        for (int round = 0; round < 5; ++round) {
            String two = R"({"count":2})";
            const ControlReply answerReply = MoreCall("m", two);
            const JsonView answer = answerReply.Result();
            const Optional<int64_t> seed = IntOf(answer, "seed");
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
        ASSERT_EQ(MoreCall("sounds").ErrorCode(), "");
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
        ASSERT_EQ(AcceptAllCall("framed").ErrorCode(), "");
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

        EXPECT_EQ(ManifestText(OnDisk("framed")), ManifestText(before)) << "no pick, no accepted, nothing in the manifest";
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
