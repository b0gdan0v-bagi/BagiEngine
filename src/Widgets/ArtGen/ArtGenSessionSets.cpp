#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/ArtGenSessionControl.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <chrono>
#include <random>

namespace BECore {

    using namespace ArtGenControl;

    namespace {

        constexpr StringView kDraftName = "draft";
        constexpr StringView kCancelledReason = "cancelled";
        constexpr StringView kStateActive = "Active";
        constexpr StringView kStateComplete = "Complete";
        // The range the service draws its own first seed from (tools/artgen/artgen_service.py)
        constexpr int64_t kSeedRange = int64_t{1} << 32;
        // Default names tried (`-2`, `-3`, ...) before EnqueueSet gives up; a person cannot queue this many sets in one second
        constexpr int32_t kMaxNameAttempts = 100;

        std::filesystem::path RootPath(const String& root) {
            return std::filesystem::path(ToStd(StringView(root)));  //-NoStd: std::filesystem::path takes a std::string_view
        }

        int64_t DrawSeed() {
            std::random_device device;
            return std::uniform_int_distribution<int64_t>(0, kSeedRange - 1)(device);
        }

        ArtGenSetJob* FirstPending(ArtGenSet& set) {
            const auto it = std::find_if(set.jobs.begin(), set.jobs.end(), [](const ArtGenSetJob& job) { return job.state == ArtGenSetJobState::Pending; });
            return it == set.jobs.end() ? nullptr : &*it;
        }

        void MarkCancelled(ArtGenSetJob& job) {
            job.state = ArtGenSetJobState::Cancelled;
            job.error = String(kCancelledReason);
        }

        /// What `artgen_set` and `artgen_state` call a set: Active while a job is Pending or Running, else Complete
        StringView StateNameOf(const ArtGenSet& set) {
            return IsActive(set) ? kStateActive : kStateComplete;
        }

        int32_t AcceptedCount(const ArtGenSet& set) {
            return static_cast<int32_t>(std::count_if(set.jobs.begin(), set.jobs.end(), [](const ArtGenSetJob& job) { return job.accepted.has_value(); }));
        }

        String TextOfPath(const std::filesystem::path& path) {
            const std::string text = path.generic_string();  //-NoStd: the text of a std::filesystem::path
            return String(FromStd(text));
        }

        bool FileExists(const std::filesystem::path& path) {
            std::error_code error;
            return !path.empty() && std::filesystem::is_regular_file(path, error);
        }

        /// "label: a, b"; nothing when @p names is empty. The parts are joined with "; "
        void AppendNames(String& text, StringView label, const Vector<String>& names) {
            if (names.empty()) {
                return;
            }
            String joined;
            for (const String& name : names) {
                joined += joined.empty() ? name : Format(", {}", name);
            }
            text += text.empty() ? Format("{}: {}", label, joined) : Format("; {}: {}", label, joined);
        }

        /// The seed Accept all writes for a job: its pick, else for images its first variant; none for a sound with no pick (a person listens)
        Optional<int64_t> CandidateSeedOf(const ArtGenSet& set, const ArtGenSetJob& job) {
            if (job.pick) {
                return job.pick;
            }
            if (StringView(set.kind) != kKindAudio && !job.variants.empty()) {
                return job.variants.front().seed;
            }
            return std::nullopt;
        }

        /// The variant @p seed of a job of a set about to be written into the game, with the real paths of its files; the reason when it cannot be
        std::expected<ArtGenVariant, String> VariantToWrite(const std::filesystem::path& gameRoot, const ArtGenSet& set, const ArtGenSetJob& job, int64_t seed) {
            const ArtGenSetVariant* picked = FindVariant(job, seed);
            if (picked == nullptr) {
                return std::unexpected(String("the pick is not one of its variants"));
            }
            if (picked->kind != set.kind) {
                return std::unexpected(Format("the picked variant is a {}, the set makes {}s", picked->kind, set.kind));
            }
            ArtGenVariant variant;
            variant.seed = picked->seed;
            variant.kind = picked->kind;
            const std::filesystem::path file = AbsoluteInSet(gameRoot, set.name, picked->file);
            if (!FileExists(file)) {
                return std::unexpected(Format("the picked file '{}' is gone", picked->file));
            }
            variant.path = TextOfPath(file);
            if (!picked->provenance.empty()) {
                const std::filesystem::path provenance = AbsoluteInSet(gameRoot, set.name, picked->provenance);
                if (!FileExists(provenance)) {
                    return std::unexpected(Format("the provenance file '{}' is gone", picked->provenance));
                }
                variant.provenancePath = TextOfPath(provenance);
            }
            return variant;
        }

        /// How a row of an image set is written: through the set's frame, or (a legacy set, or settings that change nothing) as the PNG is
        ArtGenImagePlan PlanOf(const Optional<ArtGenSetProcessing>& processing) {
            ArtGenImagePlan plan;
            if (processing) {
                plan.settings = processing->settings;
                plan.frame = processing->frame;
            } else {
                plan.settings.trim = false;
                plan.settings.maxSize = 0;
            }
            return plan;
        }

        /// The subjects of the `subjects` param: one string of lines, or a list of strings (each may be a `name: subject` line) and
        /// `{name?, subject}` objects
        std::expected<Vector<ArtGenSubject>, String> SubjectsOf(const Json& params) {
            const Json* list = FindJsonMember(params, "subjects");
            if (list == nullptr) {
                return std::unexpected(String("'subjects' is required: a string of lines, or a list of strings and {name, subject} objects"));
            }
            if (list->is_string()) {
                return ParseArtGenSubjects(StringView(GetJsonString(params, "subjects").value_or(String())));
            }
            if (!list->is_array()) {
                return std::unexpected(String("'subjects' must be a string of lines or a list"));
            }
            Vector<ArtGenSubject> subjects;
            int32_t index = 0;
            for (const Json& item : *list) {
                if (const Optional<String> line = AsString(item); line) {
                    auto parsed = ParseArtGenSubjects(StringView(*line));
                    if (!parsed) {
                        return std::unexpected(Format("subjects[{}]: {}", index, parsed.error()));
                    }
                    for (ArtGenSubject& subject : *parsed) {
                        subjects.push_back(std::move(subject));
                    }
                } else if (item.is_object()) {
                    const auto text = GetJsonString(item, "subject");
                    if (!text) {
                        return std::unexpected(Format("subjects[{}]: 'subject' (string) is required", index));
                    }
                    if (IsGiven(item, "name") && !GetJsonString(item, "name")) {
                        return std::unexpected(Format("subjects[{}]: 'name' must be a string", index));
                    }
                    ArtGenSubject subject;
                    subject.subject = *text;
                    subject.name = IsGiven(item, "name") ? *GetJsonString(item, "name") : ArtGenNameFromSubject(StringView(*text));
                    subjects.push_back(std::move(subject));
                } else {
                    return std::unexpected(Format("subjects[{}] must be a string or an object", index));
                }
                ++index;
            }
            return subjects;
        }

    }  // namespace

    void ArtGenSession::SetGameRoot(StringView root) {
        if (StringView(_gameRoot) == root) {
            return;
        }
        _gameRoot = String(root);
        _sets.clear();
        _setQueue.clear();
        if (!_gameRoot.empty()) {
            _sets = LoadSets(RootPath(_gameRoot));
        }
        ++_version;
    }

    std::expected<Optional<ArtGenSetProcessing>, String> ArtGenSession::ProcessingOfSet(const ArtGenSet& set, const std::filesystem::path& root) const {
        // A legacy set's rows are full frames already: a frame now would make the next row differ in scale from the first
        if (set.processing || IsLegacyAccepted(set)) {
            return set.processing;
        }
        const ArtGenImageSettings settings = ImageSettingsOf(set.presetData, StringView(set.preset));
        if (settings.IsIdentity()) {
            return Optional<ArtGenSetProcessing>{};
        }
        if (_frameMeasurer == nullptr) {
            return std::unexpected(String("measuring the frame is not available here"));
        }
        // Every variant, not only the picks: a switch to any of them later must fit the frame, and one scale is what keeps the pieces in proportion
        Vector<String> files;
        for (const ArtGenSetJob& job : set.jobs) {
            if (job.state != ArtGenSetJobState::Done) {
                continue;
            }
            for (const ArtGenSetVariant& variant : job.variants) {
                const std::filesystem::path file = AbsoluteInSet(root, set.name, variant.file);
                if (variant.kind == kKindImage && FileExists(file)) {
                    files.push_back(TextOfPath(file));
                }
            }
        }
        auto frame = _frameMeasurer(files, settings);
        if (!frame) {
            return std::unexpected(frame.error());
        }
        return Optional<ArtGenSetProcessing>(ArtGenSetProcessing{settings, *frame});
    }

    bool ArtGenSession::IsBusy() const {
        return IsRunInWork() || std::any_of(_sets.begin(), _sets.end(), [](const ArtGenSet& set) { return IsActive(set); });
    }

    const ArtGenSet* ArtGenSession::FindSet(StringView name) const {
        const auto it = std::find_if(_sets.begin(), _sets.end(), [&](const ArtGenSet& set) { return StringView(set.name) == name; });
        return it == _sets.end() ? nullptr : &*it;
    }

    ArtGenSet* ArtGenSession::MutableSet(StringView name) {
        const auto it = std::find_if(_sets.begin(), _sets.end(), [&](const ArtGenSet& set) { return StringView(set.name) == name; });
        return it == _sets.end() ? nullptr : &*it;
    }

    const ArtGenSet* ArtGenSession::FindNewerOwner(const ArtGenSet& set, const ArtGenSetJob& job) const {
        if (!job.accepted) {
            return nullptr;
        }
        bool laterInList = false;
        for (const ArtGenSet& other : _sets) {
            if (&other == &set) {
                laterInList = true;
                continue;
            }
            if (other.kind != set.kind) {
                continue;
            }
            for (const ArtGenSetJob& row : other.jobs) {
                if (!row.accepted || row.accepted->as != job.accepted->as) {
                    continue;
                }
                if (row.accepted->date > job.accepted->date || (row.accepted->date == job.accepted->date && laterInList)) {
                    return &other;
                }
            }
        }
        return nullptr;
    }

    ArtGenSetJob* ArtGenSession::RunningJobOfRun(ArtGenSet*& set) {
        set = nullptr;
        if (_run.setName.empty()) {
            return nullptr;
        }
        set = MutableSet(_run.setName);
        ArtGenSetJob* job = set != nullptr ? FindJob(*set, _run.jobName) : nullptr;
        return job != nullptr && job->state == ArtGenSetJobState::Running ? job : nullptr;
    }

    void ArtGenSession::SaveSet(const ArtGenSet& set) {
        if (auto written = WriteManifest(RootPath(_gameRoot), set); !written) {
            LOG_WARNING(Format("Art generation: the manifest of the set '{}' was not written: {}", set.name, written.error()));
        }
    }

    void ArtGenSession::QueueSet(StringView name) {
        if (std::none_of(_setQueue.begin(), _setQueue.end(), [&](const String& queued) { return StringView(queued) == name; })) {
            _setQueue.push_back(String(name));
        }
    }

    std::expected<String, String> ArtGenSession::EnqueueSet(ArtGenSetRequest request) {
        if (_gameRoot.empty()) {
            return std::unexpected(String("no game root to keep the set in"));
        }
        const bool draft = !request.presetData.is_null();
        if (draft) {
            if (auto shape = CheckArtGenPresetShape(request.presetData); !shape) {
                return std::unexpected(Format("the draft is not a preset: {}", shape.error()));
            }
            if (request.preset.empty()) {
                request.preset = String(kDraftName);
            }
        } else {
            if (request.preset.empty()) {
                return std::unexpected(String("no preset"));
            }
            if (const ArtGenPreset* known = FindPreset(request.preset); known != nullptr && !known->error.empty()) {
                return std::unexpected(Format("the preset '{}' is broken: {}", request.preset, known->error));
            }
        }
        if (request.count < 1 || request.count > kArtGenSetMaxCount) {
            return std::unexpected(Format("count must be 1 to {}", kArtGenSetMaxCount));
        }
        if (request.seed && *request.seed < 0) {
            return std::unexpected(String("seed must not be negative"));
        }
        if (auto subjects = CheckArtGenSubjects(request.subjects); !subjects) {
            return std::unexpected(subjects.error());
        }
        // The new set is cut and scaled as the old one is, so its object comes out the size of the row it replaces
        Optional<ArtGenSetProcessing> inherited;
        if (!request.frameOf.empty()) {
            const ArtGenSet* source = FindSet(request.frameOf);
            if (source == nullptr) {
                return std::unexpected(Format("'frameOf': no set named '{}'", request.frameOf));
            }
            if (source->kind != kKindImage) {
                return std::unexpected(Format("'frameOf': the set '{}' makes {}s, only an image set has a frame", request.frameOf, source->kind));
            }
            if (!source->processing) {
                return std::unexpected(
                    Format("'frameOf': the set '{}' has no frame yet: it is fixed at its first Accept all (and a set whose files are not trimmed, full frames, has none)", request.frameOf));
            }
            inherited = source->processing;
        }

        const auto now = std::chrono::system_clock::now();
        ArtGenSet set;
        const auto taken = [&](const String& name) { return FindSet(name) != nullptr || !NewSetFolderFree(RootPath(_gameRoot), name); };
        if (request.name.empty()) {
            // The name has a resolution of a second: a second set of the same preset in it takes the next number
            for (int32_t attempt = 1; attempt <= kMaxNameAttempts && (attempt == 1 || taken(set.name)); ++attempt) {
                set.name = DefaultArtGenSetName(StringView(request.preset), now, attempt);
            }
        } else {
            set.name = request.name;
        }
        if (!IsValidSpriteName(set.name)) {
            return std::unexpected(String("a set name is 1 to 64 of A-Z a-z 0-9 _ -"));
        }
        if (taken(set.name)) {
            return std::unexpected(Format("a set named '{}' exists; give it another name", set.name));
        }
        set.created = ArtGenTimestamp(now);
        set.preset = request.preset;
        set.presetData = std::move(request.presetData);
        set.count = request.count;
        set.seed = request.seed ? *request.seed : DrawSeed();
        // The catalog is empty until the service has polled, so this is a guess; StartJob and the first reported variant settle it
        set.kind = String(kKindImage);
        set.kind = KindOfSet(set);
        if (inherited) {
            if (set.kind != kKindImage) {
                return std::unexpected(Format("'frameOf': the preset '{}' makes {}s, only an image set takes a frame", request.preset, set.kind));
            }
            set.processing = inherited;
        }
        for (ArtGenSubject& subject : request.subjects) {
            ArtGenSetJob job;
            job.name = std::move(subject.name);
            job.subject = std::move(subject.subject);
            set.jobs.push_back(std::move(job));
        }
        if (auto written = WriteManifest(RootPath(_gameRoot), set); !written) {
            return std::unexpected(Format("the set cannot be kept: {}", written.error()));
        }
        QueueSet(set.name);
        String name = set.name;
        _sets.push_back(std::move(set));
        ++_version;
        return name;
    }

    String ArtGenSession::KindOfSet(const ArtGenSet& set) const {
        if (!set.presetData.is_null()) {
            const ArtGenWorkflow* workflow = FindWorkflow(StringView(GetJsonString(set.presetData, "workflow").value_or(String())));
            return workflow != nullptr ? workflow->kind : set.kind;
        }
        const ArtGenPreset* known = FindPreset(set.preset);
        return known != nullptr ? known->kind : set.kind;
    }

    void ArtGenSession::PumpSets() {
        // A service that polls with its generator down leaves the jobs waiting: a job started then would only fail
        if (IsRunInWork() || IsSetupBusy() || !IsGeneratorReady()) {
            return;
        }
        for (size_t index = 0; index < _setQueue.size();) {
            ArtGenSet* set = MutableSet(_setQueue[index]);
            ArtGenSetJob* job = set != nullptr ? FirstPending(*set) : nullptr;
            if (job == nullptr) {
                _setQueue.erase(_setQueue.begin() + static_cast<ptrdiff_t>(index));
                continue;
            }
            StartJob(*set, *job);
            return;
        }
    }

    void ArtGenSession::StartJob(ArtGenSet& set, ArtGenSetJob& job) {
        set.kind = KindOfSet(set);
        job.state = ArtGenSetJobState::Running;
        job.error.clear();
        _run = ArtGenRun{};
        _run.id = _nextId++;
        _run.state = ArtGenRunState::Queued;
        _run.preset = set.preset;
        _run.presetData = set.presetData;
        _run.subject = job.subject;
        _run.count = set.count;
        _run.seed = set.seed;
        _run.outDir = OutDirOf(set.name, job.name);
        _run.name = job.name;
        _run.setName = set.name;
        _run.jobName = job.name;
        SaveSet(set);
        ++_version;
    }

    void ArtGenSession::EndSetJob(StringView cancelReason) {
        ArtGenSet* set = nullptr;
        ArtGenSetJob* job = RunningJobOfRun(set);
        if (job == nullptr) {
            return;
        }
        switch (_run.state) {
            case ArtGenRunState::Done:
                job->state = ArtGenSetJobState::Done;
                job->error.clear();
                break;
            case ArtGenRunState::Failed:
                job->state = ArtGenSetJobState::Failed;
                job->error = _run.error;
                break;
            case ArtGenRunState::Cancelled:
                job->state = ArtGenSetJobState::Cancelled;
                job->error = String(cancelReason);
                break;
            default:
                return;
        }
        SaveSet(*set);
        ++_version;
    }

    void ArtGenSession::RecordSetImage(const ArtGenVariant& variant) {
        ArtGenSet* set = nullptr;
        ArtGenSetJob* job = RunningJobOfRun(set);
        if (job == nullptr) {
            return;
        }
        // What the service made is the truth about the set's kind, whatever the catalog said when the set was queued
        if (variant.kind != set->kind) {
            set->kind = variant.kind;
            ++_version;
        }
        const std::filesystem::path folder = SetFolder(RootPath(_gameRoot), set->name);
        ArtGenSetVariant entry;
        entry.seed = variant.seed;
        entry.file = RelativeToSet(folder, variant.path);
        entry.provenance = variant.provenancePath.empty() ? String() : RelativeToSet(folder, variant.provenancePath);
        entry.kind = variant.kind;
        const auto same = std::find_if(job->variants.begin(), job->variants.end(), [&](const ArtGenSetVariant& known) { return known.seed == variant.seed; });
        if (same != job->variants.end()) {
            *same = std::move(entry);
        } else {
            job->variants.push_back(std::move(entry));
        }
        SaveSet(*set);
    }

    ArtGenSetEdit ArtGenSession::CancelJob(StringView setName, StringView jobName) {
        ArtGenSet* set = MutableSet(setName);
        ArtGenSetJob* job = set != nullptr ? FindJob(*set, jobName) : nullptr;
        if (job == nullptr) {
            return ArtGenSetEdit::NoSet;
        }
        if (job->state == ArtGenSetJobState::Pending) {
            MarkCancelled(*job);
            SaveSet(*set);
            ++_version;
        } else if (job->state == ArtGenSetJobState::Running && StringView(_run.setName) == setName && StringView(_run.jobName) == jobName) {
            Cancel();
        }
        return ArtGenSetEdit::Ok;
    }

    ArtGenSetEdit ArtGenSession::CancelSet(StringView setName) {
        ArtGenSet* set = MutableSet(setName);
        if (set == nullptr) {
            return ArtGenSetEdit::NoSet;
        }
        // The pending jobs first, so the end of the running one finds nothing of this set to start
        bool changed = false;
        for (ArtGenSetJob& job : set->jobs) {
            if (job.state == ArtGenSetJobState::Pending) {
                MarkCancelled(job);
                changed = true;
            }
        }
        if (changed) {
            SaveSet(*set);
            ++_version;
        }
        ArtGenSet* running = nullptr;
        if (RunningJobOfRun(running) != nullptr && running == set) {
            Cancel();
        }
        return ArtGenSetEdit::Ok;
    }

    ArtGenSetEdit ArtGenSession::RetryJobs(StringView setName, StringView jobName) {
        ArtGenSet* set = MutableSet(setName);
        if (set == nullptr || (!jobName.empty() && FindJob(*set, jobName) == nullptr)) {
            return ArtGenSetEdit::NoSet;
        }
        // A set with no job waiting goes to the end of the line, behind the sets queued since
        if (FirstPending(*set) == nullptr) {
            _setQueue.erase(std::remove_if(_setQueue.begin(), _setQueue.end(), [&](const String& queued) { return StringView(queued) == setName; }), _setQueue.end());
        }
        bool changed = false;
        for (ArtGenSetJob& job : set->jobs) {
            const bool ended = job.state == ArtGenSetJobState::Failed || job.state == ArtGenSetJobState::Cancelled;
            if (!ended || (!jobName.empty() && StringView(job.name) != jobName)) {
                continue;
            }
            // Nothing of the earlier try is kept: the new one writes the same files under the same seeds
            job.state = ArtGenSetJobState::Pending;
            job.error.clear();
            job.variants.clear();
            job.pick.reset();
            job.accepted.reset();
            changed = true;
        }
        if (changed) {
            QueueSet(set->name);
            SaveSet(*set);
            ++_version;
        }
        return ArtGenSetEdit::Ok;
    }

    ArtGenSetEdit ArtGenSession::PickVariant(StringView setName, StringView jobName, int64_t seed, ArtGenSwitch* write) {
        ArtGenSet* set = MutableSet(setName);
        ArtGenSetJob* job = set != nullptr ? FindJob(*set, jobName) : nullptr;
        if (job == nullptr) {
            return ArtGenSetEdit::NoSet;
        }
        if (BECore::FindVariant(*job, seed) == nullptr) {
            return ArtGenSetEdit::NoVariant;
        }
        if (job->accepted && job->accepted->kept) {
            // A kept row is final; a manifest from before Keep trimmed the variants may still list others
            return job->pick == seed ? ArtGenSetEdit::Ok : ArtGenSetEdit::NoVariant;
        }
        if (!IsLive(*job)) {
            BECore::PickVariant(*job, seed);
            SaveSet(*set);
            ++_version;
            return ArtGenSetEdit::Ok;
        }
        if (job->pick == seed) {
            return ArtGenSetEdit::Ok;
        }

        // The game holds the pick of a live row, so the manifest follows only a write that went through
        const auto fail = [&](String reason) {
            if (write != nullptr) {
                write->error = std::move(reason);
            }
            return ArtGenSetEdit::WriteFailed;
        };
        if (const ArtGenSet* newer = FindNewerOwner(*set, *job)) {
            return fail(Format("the game's '{}' comes from the set '{}' now, which accepted it later; switch it there", job->accepted->as, newer->name));
        }
        const bool sound = StringView(set->kind) == kKindAudio;
        if (sound ? _soundAcceptor == nullptr : _acceptor == nullptr) {
            return fail(String("switching is not available here"));
        }
        const String name = job->accepted->as;
        if (!IsValidSpriteName(name)) {
            return fail(Format("the row was accepted as '{}', which is not a valid sprite or clip name", name));
        }
        const auto variant = VariantToWrite(RootPath(_gameRoot), *set, *job, seed);
        if (!variant) {
            return fail(Format("the job '{}': {}", job->name, variant.error()));
        }
        auto answer = sound ? _soundAcceptor(*variant, ArtGenSoundTarget{name, String()}, ArtGenSoundSettings{}, nullptr) : _acceptor(*variant, name, PlanOf(set->processing), nullptr);
        if (!answer) {
            return fail(Format("the job '{}': {}", job->name, answer.error().message));
        }
        job->pick = seed;
        job->accepted->date = ArtGenTimestamp(std::chrono::system_clock::now());
        SaveSet(*set);
        ++_version;
        if (write != nullptr) {
            write->answer = std::move(*answer);
        }
        return ArtGenSetEdit::Ok;
    }

    void ArtGenSession::RegisterSetControl(ControlService& service) {
        Register<&ArtGenSession::HandleEnqueue>(service, *this, "artgen_enqueue");
        Register<&ArtGenSession::HandleSet>(service, *this, "artgen_set");
        Register<&ArtGenSession::HandlePick>(service, *this, "artgen_pick");
        Register<&ArtGenSession::HandleRetry>(service, *this, "artgen_retry");
        Register<&ArtGenSession::HandleAcceptSet>(service, *this, "artgen_accept_set");
        Register<&ArtGenSession::HandleKeep>(service, *this, "artgen_keep");

        service.DescribeMethod(
            "artgen_enqueue",
            ControlMethodInfo{
                "Queues a set: one preset applied to a list of subjects, one job each, run one after the other with the same first seed (so the style matches); answers at once",
                {ControlParamInfo{"preset", "string|object", true, "a preset name, or a preset object (a draft)"},
                 ControlParamInfo{"presetName", "string", false, "with a draft: the name it is shown under"},
                 ControlParamInfo{"subjects", "string|array", true,
                                  "one string with a job per line (`name: subject`, `#` comments), or a list of such strings and {name?, subject} objects; at most 64, names unique"},
                 ControlParamInfo{"count", "integer", false, "images per job, 1 to 16 (default 4)"},
                 ControlParamInfo{"seed", "integer", false, "the first seed of every job (default: drawn once and recorded in the set)"},
                 ControlParamInfo{"set", "string", false, "the set's name and folder under artgen/out, 1 to 64 of A-Z a-z 0-9 _ - (default <preset>-<date>-<time>)"},
                 ControlParamInfo{"frameOf", "string", false,
                                  "an image set that already has a frame (it is fixed at its first Accept all): this set is cut and scaled by the same frame, so a replacement of one row "
                                  "comes out the size of the others; refused when the set is unknown, a sound set or has no frame"}}});
        service.DescribeMethod("artgen_set", ControlMethodInfo{"One set as its manifest.json, plus state (Active while a job is pending or running, else Complete)",
                                                               {ControlParamInfo{"set", "string", true, "the set's name"}}});
        service.DescribeMethod("artgen_pick",
                               ControlMethodInfo{"Picks the variant of a job that Accept all takes (kept in the manifest); on a row already accepted and not kept it switches the game "
                                                 "to that variant (the same name; `switched` is the acceptor's answer), unless another set accepted that name later; a kept row takes no pick",
                                                 {ControlParamInfo{"set", "string", true, "the set's name"}, ControlParamInfo{"job", "string", true, "the job's name"},
                                                  ControlParamInfo{"seed", "integer", true, "the seed of one of the job's variants"}}});
        service.DescribeMethod("artgen_retry", ControlMethodInfo{"Puts the failed and cancelled jobs of a set (or one job) back to pending; what the earlier try made is forgotten",
                                                                 {ControlParamInfo{"set", "string", true, "the set's name"}, ControlParamInfo{"job", "string", false, "only this job"}}});
        service.DescribeMethod(
            "artgen_accept_set",
            ControlMethodInfo{
                "Accepts the candidate of every job of a finished set into the game, as the job name (a sprite, or a clip assets/artgen/<name>.wav): the pick, else for images the first variant "
                "(recorded as the pick); a sound needs a pick on every row. The other variants stay switchable with artgen_pick; a kept row, and a row another set accepted under the same name later, "
                "are skipped; repeatable",
                {ControlParamInfo{"set", "string", true, "the set name; every job must be done (a sound: and picked)"}}});
        service.DescribeMethod("artgen_keep",
                               ControlMethodInfo{"Keeps the choice of the live rows of a set (accepted, not kept), or of one job: the game keeps the picked variant for good, the job's other variants "
                                                 "leave the manifest and switching is refused after. Final: the way back is a new set. Nothing live to keep is invalid_params; a row another set "
                                                 "accepted under the same name later is not kept",
                                                 {ControlParamInfo{"set", "string", true, "the set name"}, ControlParamInfo{"job", "string", false, "only this job"}}});
    }

    std::expected<Json, ControlError> ArtGenSession::HandleEnqueue(const Json& params) {
        ArtGenSetRequest request;
        const Json* preset = FindJsonMember(params, "preset");
        if (preset != nullptr && preset->is_string()) {
            request.preset = StringOr(params, "preset");
        } else if (preset != nullptr && preset->is_object()) {
            request.presetData = *preset;
            request.preset = StringOr(params, "presetName");
        } else {
            return std::unexpected(InvalidParams("'preset' must be a name or a preset object"));
        }
        auto subjects = SubjectsOf(params);
        if (!subjects) {
            return std::unexpected(InvalidParams(subjects.error()));
        }
        request.subjects = std::move(*subjects);
        if (IsGiven(params, "count")) {
            const auto count = GetJsonInt(params, "count");
            if (!count) {
                return std::unexpected(InvalidParams("'count' must be an integer"));
            }
            // Out of the range of int32_t is out of the allowed range too: EnqueueSet says so
            request.count = *count > kArtGenSetMaxCount || *count < 0 ? 0 : static_cast<int32_t>(*count);
        }
        if (IsGiven(params, "seed")) {
            request.seed = GetJsonInt(params, "seed");
            if (!request.seed) {
                return std::unexpected(InvalidParams("'seed' must be an integer"));
            }
        }
        if (IsGiven(params, "set")) {
            const auto name = GetJsonString(params, "set");
            if (!name || name->empty()) {
                return std::unexpected(InvalidParams("'set' must be a name of 1 to 64 of A-Z a-z 0-9 _ -"));
            }
            request.name = *name;
        }
        if (IsGiven(params, "frameOf")) {
            const auto frameOf = GetJsonString(params, "frameOf");
            if (!frameOf || frameOf->empty()) {
                return std::unexpected(InvalidParams("'frameOf' must be the name of a set"));
            }
            request.frameOf = *frameOf;
        }
        auto name = EnqueueSet(std::move(request));
        if (!name) {
            return std::unexpected(InvalidParams(name.error()));
        }
        const ArtGenSet* set = FindSet(*name);
        Json jobs = Json::array();
        for (const ArtGenSetJob& job : set->jobs) {
            jobs.push_back(ToJsonString(job.name));
        }
        Json reply = Json::object();
        reply["set"] = ToJsonString(*name);
        reply["jobs"] = std::move(jobs);
        reply["seed"] = set->seed;
        reply["serviceConnected"] = IsServiceConnected();
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleSet(const Json& params) {
        const auto name = GetJsonString(params, "set");
        if (!name) {
            return std::unexpected(InvalidParams("'set' (string) is required"));
        }
        const ArtGenSet* set = FindSet(*name);
        if (set == nullptr) {
            return std::unexpected(MakeError(kNoSetCode, Format("no set named '{}'", *name)));
        }
        Json reply = ArtGenSetToJson(*set);
        reply["state"] = ToJsonString(String(StateNameOf(*set)));
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandlePick(const Json& params) {
        const auto set = GetJsonString(params, "set");
        const auto job = GetJsonString(params, "job");
        const auto seed = GetJsonInt(params, "seed");
        if (!set || !job || !seed) {
            return std::unexpected(InvalidParams("'set' (string), 'job' (string) and 'seed' (integer) are required"));
        }
        ArtGenSwitch write;
        switch (PickVariant(*set, *job, *seed, &write)) {
            case ArtGenSetEdit::NoSet:
                return std::unexpected(MakeError(kNoSetCode, Format("no job '{}' in a set named '{}'", *job, *set)));
            case ArtGenSetEdit::NoVariant: {
                const ArtGenSet* known = FindSet(*set);
                const ArtGenSetJob* row = known != nullptr ? FindJob(*known, *job) : nullptr;
                if (row != nullptr && row->accepted && row->accepted->kept) {
                    return std::unexpected(MakeError(kNoVariantCode, Format("the job '{}' is kept: its choice is final", *job)));
                }
                return std::unexpected(MakeError(kNoVariantCode, Format("the job '{}' has no variant of seed {}", *job, *seed)));
            }
            case ArtGenSetEdit::WriteFailed:
                return std::unexpected(MakeError(kAcceptFailedCode, write.error));
            case ArtGenSetEdit::Ok:
                break;
        }
        Json reply = Json::object();
        reply["set"] = ToJsonString(*set);
        reply["job"] = ToJsonString(*job);
        reply["pick"] = *seed;
        if (!write.answer.is_null()) {
            reply["switched"] = std::move(write.answer);
        }
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleRetry(const Json& params) {
        const auto set = GetJsonString(params, "set");
        if (!set) {
            return std::unexpected(InvalidParams("'set' (string) is required"));
        }
        if (IsGiven(params, "job") && !GetJsonString(params, "job")) {
            return std::unexpected(InvalidParams("'job' must be a string"));
        }
        const String job = IsGiven(params, "job") ? *GetJsonString(params, "job") : String();
        const ArtGenSet* known = FindSet(*set);
        const int32_t pendingBefore = known != nullptr ? CountJobs(*known, ArtGenSetJobState::Pending) : 0;
        if (RetryJobs(*set, StringView(job)) == ArtGenSetEdit::NoSet) {
            return std::unexpected(MakeError(kNoSetCode, job.empty() ? Format("no set named '{}'", *set) : Format("no job '{}' in a set named '{}'", job, *set)));
        }
        Json reply = Json::object();
        reply["set"] = ToJsonString(*set);
        reply["retried"] = CountJobs(*known, ArtGenSetJobState::Pending) - pendingBefore;
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::CancelFromParams(const Json& params) {
        if (IsGiven(params, "job") && !GetJsonString(params, "job")) {
            return std::unexpected(InvalidParams("'job' must be a string"));
        }
        if (IsGiven(params, "set") && !GetJsonString(params, "set")) {
            return std::unexpected(InvalidParams("'set' must be a string"));
        }
        if (IsGiven(params, "all") && !GetJsonBool(params, "all")) {
            return std::unexpected(InvalidParams("'all' must be a boolean"));
        }
        const bool all = GetJsonBool(params, "all").value_or(false);
        const Optional<String> job = IsGiven(params, "job") ? GetJsonString(params, "job") : Optional<String>();
        Optional<String> setName = IsGiven(params, "set") ? GetJsonString(params, "set") : Optional<String>();

        if (job) {
            if (all) {
                return std::unexpected(InvalidParams("'job' and 'all' cannot be combined"));
            }
            if (!setName) {
                // The set of a job named alone: the one that has such a job to cancel
                for (const ArtGenSet& candidate : _sets) {
                    const auto found = std::find_if(candidate.jobs.begin(), candidate.jobs.end(), [&](const ArtGenSetJob& other) {
                        return other.name == *job && (other.state == ArtGenSetJobState::Pending || other.state == ArtGenSetJobState::Running);
                    });
                    if (found == candidate.jobs.end()) {
                        continue;
                    }
                    if (setName) {
                        return std::unexpected(InvalidParams(Format("several sets have a job '{}' in work; name the set", *job)));
                    }
                    setName = candidate.name;
                }
                if (!setName) {
                    return std::unexpected(MakeError(kNoSetCode, Format("no job '{}' is in work in any set", *job)));
                }
            }
            if (CancelJob(*setName, *job) == ArtGenSetEdit::NoSet) {
                return std::unexpected(MakeError(kNoSetCode, Format("no job '{}' in a set named '{}'", *job, *setName)));
            }
            Json reply = Json::object();
            reply["set"] = ToJsonString(*setName);
            reply["job"] = ToJsonString(*job);
            reply["state"] = ToStdString(EnumUtils<ArtGenSetJobState>::ToString(FindJob(*MutableSet(*setName), *job)->state));
            return reply;
        }
        if (setName && all) {
            if (CancelSet(*setName) == ArtGenSetEdit::NoSet) {
                return std::unexpected(MakeError(kNoSetCode, Format("no set named '{}'", *setName)));
            }
            Json reply = Json::object();
            reply["set"] = ToJsonString(*setName);
            reply["state"] = ToJsonString(String(StateNameOf(*FindSet(*setName))));
            return reply;
        }
        return std::unexpected(InvalidParams("cancel a job with 'job' (and 'set' when two sets have it), or a whole set with 'set' and 'all': true"));
    }

    std::expected<Json, ControlError> ArtGenSession::AcceptSet(StringView setName) {
        ArtGenSet* set = MutableSet(setName);
        if (set == nullptr) {
            return std::unexpected(MakeError(kNoSetCode, Format("no set named '{}'", setName)));
        }
        if (IsActive(*set)) {
            return std::unexpected(MakeError(kBusyCode, Format("the set '{}' still has jobs to run; wait for it or cancel them", setName)));
        }

        // Nothing is written before every row is known to be ready; a kept row is final and a row another set accepted later holds the
        // game's file: both are left as they are
        Vector<const ArtGenSet*> newer;
        for (const ArtGenSetJob& job : set->jobs) {
            newer.push_back(FindNewerOwner(*set, job));
        }
        Vector<String> notPicked;
        Vector<String> failed;
        Vector<String> cancelled;
        for (size_t row = 0; row < set->jobs.size(); ++row) {
            const ArtGenSetJob& job = set->jobs[row];
            if ((job.accepted && job.accepted->kept) || newer[row] != nullptr) {
                continue;
            }
            if (job.state == ArtGenSetJobState::Failed) {
                failed.push_back(job.name);
            } else if (job.state == ArtGenSetJobState::Cancelled) {
                cancelled.push_back(job.name);
            } else if (!CandidateSeedOf(*set, job)) {
                notPicked.push_back(job.name);
            }
        }
        if (!notPicked.empty() || !failed.empty() || !cancelled.empty()) {
            String which;
            AppendNames(which, "not picked", notPicked);
            AppendNames(which, "failed", failed);
            AppendNames(which, "cancelled", cancelled);
            return std::unexpected(MakeError(kSetIncompleteCode, Format("the set '{}' is not ready to accept: {}", setName, which)));
        }
        const bool sound = StringView(set->kind) == kKindAudio;
        const std::filesystem::path root = RootPath(_gameRoot);
        struct Planned {
            size_t row = 0;
            int64_t seed = 0;
            ArtGenVariant variant;
        };
        Vector<Planned> planned;
        for (size_t row = 0; row < set->jobs.size(); ++row) {
            const ArtGenSetJob& job = set->jobs[row];
            if ((job.accepted && job.accepted->kept) || newer[row] != nullptr) {
                continue;
            }
            if (planned.empty() && (sound ? _soundAcceptor == nullptr : _acceptor == nullptr)) {
                return std::unexpected(MakeError(kAcceptFailedCode, String("accepting is not available here")));
            }
            if (!IsValidSpriteName(job.name)) {
                return std::unexpected(MakeError(kAcceptFailedCode, Format("the job name '{}' is not a valid sprite or clip name", job.name)));
            }
            const int64_t seed = *CandidateSeedOf(*set, job);
            auto variant = VariantToWrite(root, *set, job, seed);
            if (!variant) {
                return std::unexpected(MakeError(kAcceptFailedCode, Format("the job '{}': {}", job.name, variant.error())));
            }
            planned.push_back(Planned{row, seed, std::move(*variant)});
        }

        // The frame comes after the cheap checks: it decodes every variant of the set. A refusal changes nothing
        Optional<ArtGenSetProcessing> processing = set->processing;
        if (!sound && !planned.empty()) {
            auto measured = ProcessingOfSet(*set, root);
            if (!measured) {
                return std::unexpected(MakeError(kAcceptFailedCode, Format("the set '{}': the frame of its pictures cannot be made: {}", setName, measured.error())));
            }
            processing = std::move(*measured);
        }
        const ArtGenImagePlan imagePlan = PlanOf(processing);

        // A failure halfway leaves the rows done marked in the manifest; the call can be made again, an accept replaces the same name
        Json accepted = Json::array();
        Vector<String> done;
        size_t next = 0;
        for (size_t row = 0; row < set->jobs.size(); ++row) {
            ArtGenSetJob& job = set->jobs[row];
            Json item = Json::object();
            item["job"] = ToJsonString(job.name);
            if (next >= planned.size() || planned[next].row != row) {
                if (job.accepted && job.accepted->kept) {
                    item["kept"] = true;
                } else {
                    item["replacedBy"] = ToJsonString(newer[row]->name);
                }
                accepted.push_back(std::move(item));
                continue;
            }
            Planned& plan = planned[next++];
            auto answer = sound ? _soundAcceptor(plan.variant, ArtGenSoundTarget{job.name, String()}, ArtGenSoundSettings{}, nullptr) : _acceptor(plan.variant, job.name, imagePlan, nullptr);
            if (!answer) {
                String doneNames;
                AppendNames(doneNames, "accepted before it", done);
                return std::unexpected(MakeError(kAcceptFailedCode, Format("the job '{}' (row {} of {}): {}; {}", job.name, row + 1, set->jobs.size(), answer.error().message,
                                                                           done.empty() ? String("accepted before it: none") : doneNames)));
            }
            job.pick = plan.seed;
            job.accepted = ArtGenSetAccepted{job.name, ArtGenTimestamp(std::chrono::system_clock::now()), false};
            // The frame is kept with the first row written, so a refusal or a failed first row leaves the set as it was
            if (!sound && !set->processing) {
                set->processing = processing;
            }
            done.push_back(job.name);
            SaveSet(*set);
            ++_version;
            item["as"] = ToJsonString(job.name);
            if (answer->is_object()) {
                for (const auto& [key, value] : answer->items()) {
                    item[key] = value;
                }
            }
            item["seed"] = plan.seed;
            item["alternatives"] = AlternativesOf(job);
            accepted.push_back(std::move(item));
        }
        Json reply = Json::object();
        reply["set"] = ToJsonString(set->name);
        reply["accepted"] = std::move(accepted);
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::KeepSet(StringView setName, StringView jobName) {
        ArtGenSet* set = MutableSet(setName);
        if (set == nullptr) {
            return std::unexpected(MakeError(kNoSetCode, Format("no set named '{}'", setName)));
        }
        if (!jobName.empty() && FindJob(*set, jobName) == nullptr) {
            return std::unexpected(MakeError(kNoSetCode, Format("no job '{}' in a set named '{}'", jobName, setName)));
        }
        Vector<ArtGenSetJob*> rows;
        Json replaced = Json::array();
        Vector<String> replacedNames;
        for (ArtGenSetJob& job : set->jobs) {
            if (!IsLive(job) || (!jobName.empty() && StringView(job.name) != jobName)) {
                continue;
            }
            // The game's file is another set's variant now: keeping this row would record a seed the game does not hold
            if (const ArtGenSet* newer = FindNewerOwner(*set, job)) {
                if (!jobName.empty()) {
                    return std::unexpected(InvalidParams(Format("the job '{}' is replaced: the set '{}' accepted '{}' later; keep it there", job.name, newer->name, job.accepted->as)));
                }
                Json item = Json::object();
                item["job"] = ToJsonString(job.name);
                item["by"] = ToJsonString(newer->name);
                replaced.push_back(std::move(item));
                replacedNames.push_back(Format("{} (by {})", job.name, newer->name));
                continue;
            }
            rows.push_back(&job);
        }
        if (rows.empty()) {
            String reason = jobName.empty() ? Format("the set '{}' has no live row to keep", setName) : Format("the job '{}' is not live: only an accepted row that is not kept can be kept", jobName);
            AppendNames(reason, "replaced by a newer set", replacedNames);
            return std::unexpected(InvalidParams(reason));
        }
        for (const ArtGenSetJob* job : rows) {
            if (!job->pick || BECore::FindVariant(*job, *job->pick) == nullptr) {
                return std::unexpected(MakeError(kAcceptFailedCode, Format("the job '{}' has no pick to keep", job->name)));
            }
        }
        if (_keeper == nullptr) {
            return std::unexpected(MakeError(kAcceptFailedCode, String("keeping is not available here")));
        }

        // The rows kept stay kept when a later one fails: the call can be made again for the rest
        Vector<String> kept;
        for (ArtGenSetJob* job : rows) {
            const String date = ArtGenTimestamp(std::chrono::system_clock::now());
            if (auto written = _keeper(StringView(set->kind), StringView(job->accepted->as), *job->pick, StringView(date)); !written) {
                String doneNames;
                AppendNames(doneNames, "kept before it", kept);
                return std::unexpected(MakeError(kAcceptFailedCode, Format("the job '{}': {}; {}", job->name, written.error().message, kept.empty() ? String("kept before it: none") : doneNames)));
            }
            BECore::KeepJob(*job, StringView(date));
            kept.push_back(job->name);
            SaveSet(*set);
            ++_version;
        }
        Json names = Json::array();
        for (const String& name : kept) {
            names.push_back(ToJsonString(name));
        }
        Json reply = Json::object();
        reply["set"] = ToJsonString(set->name);
        reply["kept"] = std::move(names);
        if (!replaced.empty()) {
            reply["replaced"] = std::move(replaced);
        }
        return reply;
    }

    std::expected<Json, ControlError> ArtGenSession::HandleKeep(const Json& params) {
        const auto set = GetJsonString(params, "set");
        if (!set) {
            return std::unexpected(InvalidParams("'set' (string) is required"));
        }
        if (IsGiven(params, "job") && !GetJsonString(params, "job")) {
            return std::unexpected(InvalidParams("'job' must be a string"));
        }
        const String job = IsGiven(params, "job") ? *GetJsonString(params, "job") : String();
        return KeepSet(*set, StringView(job));
    }

    std::expected<Json, ControlError> ArtGenSession::HandleAcceptSet(const Json& params) {
        const auto set = GetJsonString(params, "set");
        if (!set) {
            return std::unexpected(InvalidParams("'set' (string) is required"));
        }
        return AcceptSet(*set);
    }

    Json ArtGenSession::SetsJson() const {
        Json sets = Json::array();
        for (const ArtGenSet& set : _sets) {
            Json jobs = Json::object();
            jobs["pending"] = CountJobs(set, ArtGenSetJobState::Pending);
            jobs["running"] = CountJobs(set, ArtGenSetJobState::Running);
            jobs["done"] = CountJobs(set, ArtGenSetJobState::Done);
            jobs["failed"] = CountJobs(set, ArtGenSetJobState::Failed);
            jobs["cancelled"] = CountJobs(set, ArtGenSetJobState::Cancelled);
            Json item = Json::object();
            item["name"] = ToJsonString(set.name);
            item["preset"] = ToJsonString(set.preset);
            item["kind"] = ToJsonString(set.kind);
            item["count"] = set.count;
            item["seed"] = set.seed;
            item["state"] = ToJsonString(String(StateNameOf(set)));
            item["jobs"] = std::move(jobs);
            item["picked"] = PickedCount(set);
            item["accepted"] = AcceptedCount(set);
            item["live"] = CountLive(set);
            item["kept"] = CountKept(set);
            sets.push_back(std::move(item));
        }
        return sets;
    }

}  // namespace BECore
