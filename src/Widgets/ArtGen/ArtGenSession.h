#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/Json.h>
#include <BECore/File/Path.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/Singleton.h>
#include <Widgets/ArtGen/ArtGenAnswers.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSound.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class ControlService;
    class SceneNode;
    struct ArtGenSoundTarget;

    CORE_ENUM(ArtGenRunState, uint8_t, Idle, Queued, Running, Done, Failed, Cancelled)
    /// What the service is asked to do to the generator itself (tools/artgen/artgen_setup.py)
    CORE_ENUM(ArtGenSetupAction, uint8_t, None, Plan, Install, Start, Stop)
    CORE_ENUM(ArtGenSetupState, uint8_t, Idle, Queued, Running, Done, Failed)

    /// The `state` of a poll that says the generator can make images (the others: `not_running`, `not_installed`)
    inline constexpr StringView kGeneratorReady = "ready";

    /// What the generation service last said about itself in a poll
    struct ArtGenServiceStatus {
        String state;  // "ready", "not_running", "not_installed" (tools/artgen/CLAUDE.md, "Generators")
        String reason;
        String generator;
        Vector<ArtGenPreset> presets;      // the game's, then the engine's starters it does not override
        Vector<ArtGenWorkflow> workflows;  // what a preset may name, with the inputs the form shows
        bool startedHere = false;          // the running generator was started by a setup action, so Stop can end it
    };

    struct ArtGenVariant {
        int64_t seed = 0;
        String path;  // real path of the file the service wrote: a PNG, or a FLAC for a sound
        String provenancePath;
        String kind = "image";  // "image" or "audio"

        [[nodiscard]] bool IsAudio() const {
            return kind == "audio";
        }
    };

    /// One thing an install would do, as the service planned it
    struct ArtGenSetupStep {
        String what;
        String dest;
        String state;  // "present", "verify", "download", "install"
        String licence;
        int64_t size = 0;  // bytes to download
    };

    /// One setup action (plan, install, start, stop of the generator) and what the service reported for it
    struct ArtGenSetup {
        uint32_t id = 0;
        ArtGenSetupAction action = ArtGenSetupAction::None;
        ArtGenSetupState state = ArtGenSetupState::Idle;
        String platform;
        String platformNote;
        String root;
        Vector<ArtGenSetupStep> plan;  // of the last Plan or Install
        int64_t downloadBytes = 0;
        String current;  // the step in work
        int64_t done = 0;
        int64_t total = 0;
        String log;  // the last line the service said
        String error;
        bool cancelRequested = false;
    };

    /// One request and what came back for it
    struct ArtGenRun {
        uint32_t id = 0;
        ArtGenRunState state = ArtGenRunState::Idle;
        String preset;      // the name; with presetData, the name of the form the draft came from
        String presetData;  // a draft sent inline (the form as it is, saved or not): the compact text of one object, keys sorted; empty: the preset by name
        String subject;
        int32_t count = 0;
        Optional<int64_t> seed;
        String outDir;   // a job of a set: where the service writes, under `artgen/out/` (`<set>/<job>`); empty: the service's own folder
        String name;     // a job of a set: the file stem of its images
        String setName;  // the set and the job this run is the work of; empty for an ordinary run
        String jobName;
        int32_t image = 0;  // the image in work, 0-based
        int32_t step = 0;
        int32_t steps = 0;
        String error;
        Vector<ArtGenVariant> variants;
        int32_t keptVariants = 0;             // the variants of the last run it started with (Request with keepVariants)
        Optional<ArtGenReference> reference;  // the picture the run starts from, sent in the request; a job of a set has its set's
        bool cancelRequested = false;
    };

    /// What EnqueueSet takes: one preset applied to a list of subjects
    struct ArtGenSetRequest {
        String preset;      // a preset name; with presetData, the name of the form the draft came from
        String presetData;  // a draft sent inline, as for ArtGenRun; empty: the preset by name
        Vector<ArtGenSubject> subjects;
        int32_t count = kArtGenSetDefaultCount;
        Optional<int64_t> seed;               // the first seed of every job; none: one is drawn and recorded, so the style of the set matches
        String name;                          // the set's folder name; empty: DefaultArtGenSetName
        String frameOf;                       // an image set whose frame this set takes over (a replacement of one of its rows); empty: its own frame
        Optional<ArtGenReference> reference;  // the picture every job starts from (the set keeps it); none: the preset's own `reference`
        Optional<String> atlas;               // an image set's atlas group; none: the preset's `atlas` at enqueue time, "": no group even when the preset has one
    };

    /// What an edit of a set found: the set (or job) unknown, a pick of a seed the job has not, or a switch of a live row whose write failed
    CORE_ENUM(ArtGenSetEdit, uint8_t, Ok, NoSet, NoVariant, WriteFailed)

    /// What the write of a switch of a live row (PickVariant) answered: the acceptor's answer, or why it failed
    struct ArtGenSwitch {
        Optional<ArtGenAcceptAnswer> answer;  // the acceptor's answer; none when nothing was written (a row that is not live, or the pick it already has)
        String error;
    };

    /**
     * @brief The editor's side of the art generation contract: the request the Generate panel (or a client) makes, and what the generation
     * service (tools/artgen/artgen_bridge.py, a control API client) reports back. No ImGui: the panel draws it.
     *
     * The service pulls (src/Widgets/ArtGen/CLAUDE.md, "The contract"): it calls `artgen_poll` with its status about twice a second and
     * gets the game root, a new request once and a cancel once; it reports with `artgen_progress`, `artgen_image` and `artgen_done`. One run at
     * a time. A setup action of the generator travels the same way (`setup` in the poll's answer, `artgen_setup_report` back), never beside a run. It lives as long as the program (the control API
     * cannot drop a handler, and the panel is re-created at a switch of the run mode), so the handlers capture the session, never a widget. Main thread only, like the control API.
     */
    class ArtGenSession : public Singleton<ArtGenSession> {
    public:
        /// Seconds without a poll after which the service counts as gone
        static constexpr double kServiceTimeoutSeconds = 3.0;
        static constexpr int32_t kMaxCount = 16;

        using Clock = MoveOnlyFunction<double()>;
        /// Accepts a variant as the sprite @p sprite, cut and scaled by @p plan, and, when @p node is given, puts it on that node; the answer of
        /// `artgen_accept`
        using Acceptor = std::expected<ArtGenAcceptAnswer, ControlError> (*)(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan, SceneNode* node);
        /// Accepts a sound variant as a clip at @p target, processed by @p settings, and puts it on @p node's AudioSourceComponent when given
        using SoundAcceptor = std::expected<ArtGenAcceptAnswer, ControlError> (*)(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* node);
        /// The frame every image of a set is cut from: the union of the drawn boxes of the PNG files @p pngFiles (real paths, all of one size)
        /// grown and scaled by @p settings. Decodes them, so it is injected like the acceptors (a fake in the tests)
        using FrameMeasurer = std::expected<ArtGenImageFrame, String> (*)(const Vector<String>& pngFiles, const ArtGenImageSettings& settings);
        /// Writes into the game's provenance file of @p name (a sprite or a clip, by @p kind) that the choice with @p seed was kept on @p date
        using Keeper = std::expected<void, ControlError> (*)(StringView kind, StringView name, int64_t seed, StringView date);
        /// Packs the atlas group @p group once, after the rows of an Accept all that were written without packing and before the batch
        /// ends; the answer is the `atlas` of `artgen_accept_set` (`PackAtlasForControl` in the editor, a fake in the tests)
        using AtlasPacker = std::expected<ArtGenAtlasAnswer, ControlError> (*)(StringView group);
        /// Opens and closes the batch of one Accept all of images: the acceptors called between them write their files and change the library
        /// in memory only, and the end saves it once. The end's failure says the library was not saved
        using BatchBegin = void (*)();
        using BatchEnd = std::expected<void, ControlError> (*)();

        /// @param clock monotonic seconds; the default is the steady clock (a test passes its own). The editor's one session is GetInstance()
        explicit ArtGenSession(Clock clock = {});
        ~ArtGenSession() override;

        /// Registers the methods on @p service (once per service; a second call with the same service does nothing)
        void RegisterControl(ControlService& service);

        /// The real path of the game the service works for (where its presets are, `artgen/presets/`, and its sets, `artgen/out/`). The
        /// sets of the game are read from disk when the root changes (the panel calls this at every switch of the run mode: the same root
        /// changes nothing)
        void SetGameRoot(StringView root);
        [[nodiscard]] const String& GetGameRoot() const {
            return _gameRoot;
        }

        /// Queues a request; the error says why not (a run in work, a bad count or preset). @p keepVariants starts the new run with the
        /// variants of the last one, so a few more can be made without losing the ones on screen (not those of a set's job: they are its row's)
        /// @p reference is the picture the images start from (it replaces the preset's own `reference` whole); the service checks it and fails the run with the reason
        std::expected<uint32_t, String> Request(StringView preset, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants = false, Optional<ArtGenReference> reference = {});
        /// The same with the preset's content inline (the panel's form, saved or not): the service checks it as it checks a file, and the
        /// provenance names it `draft`. @p name is the form's, for the messages; @p presetText is the draft's document as compact text with its keys
        /// sorted (`ArtGenPresetForm::ToJsonText`; it goes to the service as written); @p keepVariants is as for Request
        std::expected<uint32_t, String> RequestDraft(StringView name, StringView presetText, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants = false,
                                                     Optional<ArtGenReference> reference = {});

        /// The panel's reference slot: one picture for the next single run, shared by the Generate and the Set tab and kept across a switch of the
        /// run mode (the session outlives the panel). A run takes it only when the panel passes it to Request; a set's job never carries it
        [[nodiscard]] const Optional<ArtGenReference>& GetReferenceSlot() const {
            return _referenceSlot;
        }
        void SetReferenceSlot(ArtGenReference reference);
        void ClearReferenceSlot();

        /// Writes @p preset as `<game root>/artgen/presets/<name>.json` (an engine starter saved under its name becomes the game's own) and
        /// shows it at once; the service checks it at its next look (asked for in the next poll). The path, or why not: a bad name, a shape
        /// that is not a preset (CheckArtGenPresetShape: @p presetText is not a JSON object too), no game root, a write that failed. @p presetText
        /// is compact text with its keys sorted, as for RequestDraft
        std::expected<String, String> SavePreset(StringView name, StringView presetText);
        /// The workflow the service described by that name, or null
        [[nodiscard]] const ArtGenWorkflow* FindWorkflow(StringView name) const;
        /// The preset the service reported by that name, or null
        [[nodiscard]] const ArtGenPreset* FindPreset(StringView name) const;
        /// Asks the service to stop the run; the images already made stay
        void Cancel();

        /// Asks the service to plan, install, start or stop the generator (the panel's Set up / Start / Stop); refused while a setup action or a
        /// run is in work. An install is asked only after the owner saw the plan: the service downloads without asking again
        std::expected<uint32_t, String> RequestSetup(ArtGenSetupAction action);
        /// Asks the service to stop an install; what is downloaded stays and the next install resumes
        void CancelSetup();
        /// Asks the service to exit (the panel's Disconnect): the next poll answers `shutdown`, and from then the service counts as gone; a run or
        /// a setup action in work ends there (the service cancels it and says nothing more)
        void RequestServiceStop();

        /// Forgets the last setup action (a plan the owner turned down, an old error); nothing while one is in work
        void ClearSetup();
        [[nodiscard]] const ArtGenSetup& GetSetup() const {
            return _setup;
        }
        [[nodiscard]] bool IsSetupBusy() const {
            return _setup.state == ArtGenSetupState::Queued || _setup.state == ArtGenSetupState::Running;
        }

        /// What `artgen_accept` and the panel's Accept run (ArtGenAccept.h in the editor; a fake in the tests); without one `artgen_accept` fails
        void SetAcceptor(Acceptor acceptor) {
            _acceptor = acceptor;
        }
        /// What a sound variant's Accept runs (ArtGenAccept.h in the editor; a fake in the tests)
        void SetSoundAcceptor(SoundAcceptor acceptor) {
            _soundAcceptor = acceptor;
        }
        /// What Keep runs for each row it keeps, before the manifest changes (ArtGenAccept.h in the editor; a fake in the tests)
        void SetKeeper(Keeper keeper) {
            _keeper = keeper;
        }
        /// What an Accept all of a set with an atlas group packs it with, once after the rows. With none installed each row packs through its
        /// own acceptor and the answer's `atlas` stays absent
        void SetAtlasPacker(AtlasPacker packer) {
            _atlasPacker = packer;
        }
        /// What the first Accept all of an image set measures its frame with (ArtGenAccept.h in the editor; a fake in the tests)
        void SetFrameMeasurer(FrameMeasurer measurer) {
            _frameMeasurer = measurer;
        }
        /// The batch an Accept all of images runs its acceptors inside (ArtGenAccept.h in the editor; fakes in the tests). Both hooks must be
        /// given for a batch; a null one leaves every accept saving the library itself and every row marked as soon as it is written
        void SetAcceptBatch(BatchBegin begin, BatchEnd end) {
            _batchBegin = begin;
            _batchEnd = end;
        }
        /// The node "Use on selected" puts the sprite on (the scene editor's primary selection); null for none. Held, so it is checked alive
        /// in the active scene before use
        void SetSelectedNode(IntrusivePtr<SceneNode> node);
        /// The selected node when it is still in the tree of the active scene, else null
        [[nodiscard]] SceneNode* GetSelectedNodeIfAlive() const;
        /// The variant of the current run with @p seed, or null
        [[nodiscard]] const ArtGenVariant* FindVariant(int64_t seed) const;
        /// The image settings of the current run's preset: a draft's own `accept` (none: the defaults), else the saved preset's by name;
        /// a malformed key is the defaults
        [[nodiscard]] ArtGenImageSettings ImageSettingsOfRun() const;
        /// The atlas group of the current run's preset (the same preset lookup as ImageSettingsOfRun()); empty when it names none
        [[nodiscard]] String AtlasOfRun() const;
        /// Accepts the variant @p seed as @p sprite, and puts it on the selected node when @p useOnSelected. The image is processed by
        /// @p settings (the panel's form), or by ImageSettingsOfRun() when null; a single image has no frame of a set, so it is trimmed on its own.
        /// The sprite joins the atlas group @p atlas and the group is packed (`ArtGenImagePlan::atlas`); null is AtlasOfRun(), "" no group
        std::expected<ArtGenAcceptAnswer, ControlError> Accept(int64_t seed, StringView sprite, bool useOnSelected, const ArtGenImageSettings* settings = nullptr, const String* atlas = nullptr);
        /// Accepts the sound variant @p seed as a clip at @p target, processed by @p settings; on the selected node's AudioSourceComponent
        /// when @p useOnSelected
        std::expected<ArtGenAcceptAnswer, ControlError> AcceptSound(int64_t seed, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, bool useOnSelected);

        [[nodiscard]] bool IsServiceConnected() const;
        [[nodiscard]] const ArtGenServiceStatus& GetServiceStatus() const {
            return _status;
        }
        [[nodiscard]] const ArtGenRun& GetRun() const {
            return _run;
        }
        /// A run is in work or a set has a job to do: an ordinary run, a request and a setup action are refused meanwhile
        [[nodiscard]] bool IsBusy() const;
        /// A run is Queued or Running (a set that only has Pending jobs is busy, but has no run to cancel)
        [[nodiscard]] bool IsRunInWork() const {
            return _run.state == ArtGenRunState::Queued || _run.state == ArtGenRunState::Running;
        }
        /// A run of the Generate tab is in work: not the job of a set (that one is the Set tab's, and its Cancel is there)
        [[nodiscard]] bool IsSingleRunInWork() const {
            return IsRunInWork() && _run.setName.empty();
        }
        /// The service polls and says its generator is ready: what a run needs to start
        [[nodiscard]] bool IsGeneratorReady() const;

        /// Queues a set (Sets/CLAUDE.md): validates like Request / RequestDraft, draws the shared seed when none is given, writes the manifest.
        /// The set's name, or why not. Jobs go out one per poll, first in first out
        std::expected<String, String> EnqueueSet(ArtGenSetRequest request);
        /// Starts the next Pending job as the ordinary run when the service is there and nothing else is in work. The poll and the end of a
        /// run call it; without a service the jobs wait
        void PumpSets();
        /// Cancels a Pending job at once, the running one through the service; any other job is left. NoSet for an unknown set or job
        ArtGenSetEdit CancelJob(StringView set, StringView job);
        /// Cancels every Pending job and the running one of @p set
        ArtGenSetEdit CancelSet(StringView set);
        /// Puts the Failed and Cancelled jobs of @p set (or only @p job) back to Pending, with nothing kept of the earlier try
        ArtGenSetEdit RetryJobs(StringView set, StringView job = {});
        /**
         * @brief Queues @p count more variants for every Done row of a set that is not active, on one fresh seed (@p seed when given), appended to
         * the variants the rows have (Sets/CLAUDE.md, "More variants"). @p jobs narrows the rows (empty: all). Kept, failed, cancelled and full rows
         * are left out and listed in the answer. Everything is checked first and a refusal changes nothing: `artgen_no_set`, `artgen_busy` (the set is
         * active), `invalid_params` (a bad count or seed, a seed that meets a variant of a row that would run, nothing to run)
         */
        std::expected<ArtGenMoreAnswer, ControlError> MoreVariants(StringView set, int32_t count, Optional<int64_t> seed = {}, const Vector<String>& jobs = {});
        /// Picks the variant @p seed of a job (saved in the manifest). On a **live** row (accepted, not kept) it switches: the variant is
        /// written into the game under the name the row was accepted as, and only then are the pick and the date changed; a failed write
        /// (also a row that another set replaced, FindNewerOwner) is `WriteFailed` and changes nothing. A kept row takes no other pick
        /// (`NoVariant`). @p write gets what the write answered
        ArtGenSetEdit PickVariant(StringView set, StringView job, int64_t seed, ArtGenSwitch* write = nullptr);
        /// Accepts one image variant of a set row as the sprite @p sprite, through the acceptor of Accept (a drop of the variant, ArtGenDrag.h):
        /// the set's own processing (settings and frame, so a pawn stays smaller than a king), else the settings of its preset with a frame
        /// of its own; the set's atlas group. Only a write: the manifest, the pick and the row's `accepted` stay as they were, and no node is
        /// touched. The acceptor's answer, or `artgen_no_set`, `artgen_no_variant`, `artgen_accept_failed` (a sound variant, a bad name, a gone file)
        std::expected<ArtGenAcceptAnswer, ControlError> AcceptSetVariant(StringView set, StringView job, int64_t seed, StringView sprite);
        /// Accepts the candidate of every job of a finished set as its job's name, through the acceptors Accept uses, marking each row in the
        /// manifest (Sets/CLAUDE.md, "Accept all"): the pick, else for images the first variant. A kept row and a row another set replaced are skipped. The checks come first and a
        /// refusal by them changes nothing. With the batch hooks an image set's files are written first and the library is saved once; the
        /// rows are marked only after that save went through, so the manifest never names a row whose library entry was rolled back. A
        /// failure halfway leaves the rows before it accepted (and marked); a failed save marks none and says which files were written.
        /// A set with an atlas group and an installed AtlasPacker writes its rows into the group without packing it and packs it once after
        /// the last row, inside the batch, so nothing reaches TextureLibrary.xml before the batch's one save (the answer's `atlas`; a set of
        /// only kept or replaced rows packs nothing). A failed row or a failed pack leaves the rows done marked and in the game, says the
        /// group is not packed, and calling again finishes it
        std::expected<ArtGenAcceptSetAnswer, ControlError> AcceptSet(StringView name);
        /// Fixes the choice of every live row of a set, or of @p job only: the Keeper writes the kept seed into the game first, then the row is
        /// kept (its other variants leave the manifest) and the manifest written. Final; nothing live to keep is `invalid_params`, and so is a named
        /// row that another set replaced (the whole-set call skips such rows and lists them). The answer
        /// names the rows kept; a failure halfway names the row and the rows done
        std::expected<ArtGenKeepAnswer, ControlError> KeepSet(StringView name, StringView job = {});
        /// The other set that accepted a row under the same name and kind of @p job later, so the game's file is its variant now (null when
        /// @p job owns it, or was never accepted). A new accept's date is strictly later than the others' (`NewAcceptDate`), so a tie
        /// (the set later in the list is the newer) is left only for manifests written before that rule
        [[nodiscard]] const ArtGenSet* FindNewerOwner(const ArtGenSet& set, const ArtGenSetJob& job) const;
        [[nodiscard]] const ArtGenSet* FindSet(StringView name) const;
        [[nodiscard]] const Vector<ArtGenSet>& GetSets() const {
            return _sets;
        }
        /// Grows on every change the panel shows (a poll that changed the status, a step, an image, the end)
        [[nodiscard]] uint64_t Version() const {
            return _version;
        }
        [[nodiscard]] uint64_t PollCount() const {
            return _pollCount;
        }

        /// The handlers, public for the tests: each takes the params of the call and answers like the method. `HandlePoll` and `HandleSet` answer the
        /// compact text of the reply, written by hand: `request.preset` of the poll is a preset's name or an inline draft (two types under one key,
        /// the draft's text in the middle), and `artgen_set` is the manifest (`WriteArtGenSet`) with `state`
        std::expected<String, ControlError> HandlePoll(JsonView params);
        std::expected<ArtGenStaleAnswer, ControlError> HandleProgress(JsonView params);
        std::expected<ArtGenStaleAnswer, ControlError> HandleImage(JsonView params);
        std::expected<ArtGenStaleAnswer, ControlError> HandleDone(JsonView params);
        std::expected<ArtGenQueuedAnswer, ControlError> HandleGenerate(JsonView params);
        std::expected<ArtGenCancelAnswer, ControlError> HandleCancel(JsonView params);
        std::expected<ArtGenAcceptAnswer, ControlError> HandleAccept(JsonView params);
        std::expected<ArtGenStaleAnswer, ControlError> HandleSetupReport(JsonView params);
        std::expected<ArtGenSetupQueuedAnswer, ControlError> HandleSetup(JsonView params);
        std::expected<ArtGenSavedAnswer, ControlError> HandleSavePreset(JsonView params);
        std::expected<ArtGenEnqueueAnswer, ControlError> HandleEnqueue(const Json& params);
        std::expected<String, ControlError> HandleSet(const Json& params);
        std::expected<ArtGenPickAnswer, ControlError> HandlePick(const Json& params);
        std::expected<ArtGenRetryAnswer, ControlError> HandleRetry(const Json& params);
        std::expected<ArtGenAcceptSetAnswer, ControlError> HandleAcceptSet(const Json& params);
        std::expected<ArtGenKeepAnswer, ControlError> HandleKeep(const Json& params);
        std::expected<ArtGenMoreAnswer, ControlError> HandleMore(const Json& params);
        [[nodiscard]] ArtGenStateAnswer BuildState() const;

    private:
        /// The run a report of the service is about, or null when it is about an older one (a late message after a new request)
        ArtGenRun* RunOf(JsonView params);
        std::expected<uint32_t, String> Queue(StringView preset, String presetData, StringView subject, int32_t count, Optional<int64_t> seed, bool keepVariants, Optional<ArtGenReference> reference);
        void ReadCatalog(JsonView params);
        [[nodiscard]] ArtGenSet* MutableSet(StringView name);
        /// The job of `_run` while it is Running (the run is the work of a set), else null
        [[nodiscard]] ArtGenSetJob* RunningJobOfRun(ArtGenSet*& set);
        /// The `accepted.date` of an accept of @p name by @p set now: strictly later than the date of every row of the other sets of its kind
        /// that holds the name (live or kept), so `FindNewerOwner` never meets a tie of two accepts within one second
        [[nodiscard]] String NewAcceptDate(const ArtGenSet& set, StringView name) const;
        void SaveSet(const ArtGenSet& set);
        /// The document of a preset: a draft's @p presetData, else the text of the saved one by @p preset (empty when unknown)
        [[nodiscard]] StringView PresetTextOf(StringView presetData, StringView preset) const;
        /// The image settings a preset (a draft's @p presetData, else the saved one by @p preset) asks for; a malformed `accept` is the defaults
        [[nodiscard]] ArtGenImageSettings ImageSettingsOf(StringView presetData, StringView preset) const;
        /// The `atlas` group a preset (a draft's @p presetData, else the saved one by @p preset) names; empty when none, or when the value is not a valid name
        [[nodiscard]] String AtlasOf(StringView presetData, StringView preset) const;
        /// What the rows of an image set are written with: the set's own processing, else (the first Accept all) one measured now over every
        /// variant of every Done job; none (the identity plan) for a legacy set or settings that change nothing. Nothing is stored here
        [[nodiscard]] std::expected<Optional<ArtGenSetProcessing>, String> ProcessingOfSet(const ArtGenSet& set, const Path& root) const;
        /// The kind of the set's preset or draft workflow from the service's catalog; the set's own kind while the catalog does not know it
        [[nodiscard]] String KindOfSet(const ArtGenSet& set) const;
        void StartJob(ArtGenSet& set, ArtGenSetJob& job);
        /// The run just ended: the job it was the work of takes its state (Done, Failed, Cancelled with @p cancelReason) and the manifest is written
        void EndSetJob(StringView cancelReason);
        void RecordSetImage(const ArtGenVariant& variant);
        void QueueSet(StringView name);
        /// The methods of the sets (ArtGenSessionSets.cpp): the handlers and their descriptions
        void RegisterSetControl(ControlService& service);
        /// `artgen_cancel` with `job` or `set`: the one job, or the whole set
        std::expected<ArtGenCancelAnswer, ControlError> CancelFromParams(JsonView params);
        [[nodiscard]] Vector<ArtGenSetSummaryAnswer> BuildSetSummaries() const;

        Clock _clock;
        Acceptor _acceptor = nullptr;
        SoundAcceptor _soundAcceptor = nullptr;
        Keeper _keeper = nullptr;
        FrameMeasurer _frameMeasurer = nullptr;
        AtlasPacker _atlasPacker = nullptr;
        BatchBegin _batchBegin = nullptr;
        BatchEnd _batchEnd = nullptr;
        IntrusivePtr<SceneNode> _selectedNode;
        String _gameRoot;
        Vector<ArtGenSet> _sets;
        Vector<String> _setQueue;  // the sets with work, in the order they were queued or retried: the next job comes from the first
        ArtGenServiceStatus _status;
        ArtGenRun _run;
        Optional<ArtGenReference> _referenceSlot;
        ArtGenSetup _setup;
        uint32_t _nextId = 1;
        bool _stopService = false;
        bool _refreshCatalog = false;  // a preset was saved: the next poll asks the service to look at the files again
        String _catalogPresets;        // the raw text of the two lists of the last poll, so an unchanged list is not read again
        String _catalogWorkflows;
        double _lastPollSeconds = -1.0;
        uint64_t _pollCount = 0;
        uint64_t _version = 0;
        const ControlService* _registeredOn = nullptr;
    };

}  // namespace BECore
