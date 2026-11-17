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
        constexpr StringView kKindImage = "image";
        constexpr StringView kCancelledReason = "cancelled";
        constexpr StringView kStateActive = "Active";
        constexpr StringView kStateComplete = "Complete";
        // The range the service draws its own first seed from (tools/artgen/artgen_service.py)
        constexpr int64_t kSeedRange = int64_t{1} << 32;

        std::filesystem::path RootPath(const String& root) {
            return std::filesystem::path(ToStd(StringView(root)));  //-NoStd: std::filesystem::path takes a std::string_view
        }

        int64_t DrawSeed() {
            std::random_device device;
            return std::uniform_int_distribution<int64_t>(0, kSeedRange - 1)(device);
        }

        ArtGenSetJob* FindJob(ArtGenSet& set, StringView name) {
            const auto it = std::find_if(set.jobs.begin(), set.jobs.end(), [&](const ArtGenSetJob& job) { return StringView(job.name) == name; });
            return it == set.jobs.end() ? nullptr : &*it;
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

        const auto now = std::chrono::system_clock::now();
        ArtGenSet set;
        set.name = request.name.empty() ? DefaultArtGenSetName(StringView(request.preset), now) : request.name;
        if (!IsValidSpriteName(set.name)) {
            return std::unexpected(String("a set name is 1 to 64 of A-Z a-z 0-9 _ -"));
        }
        if (FindSet(set.name) != nullptr || !NewSetFolderFree(RootPath(_gameRoot), set.name)) {
            return std::unexpected(Format("a set named '{}' exists; give it another name", set.name));
        }
        set.created = ArtGenTimestamp(now);
        set.preset = request.preset;
        set.presetData = std::move(request.presetData);
        set.count = request.count;
        set.seed = request.seed ? *request.seed : DrawSeed();
        set.kind = String(kKindImage);
        if (draft) {
            const ArtGenWorkflow* workflow = FindWorkflow(StringView(GetJsonString(set.presetData, "workflow").value_or(String())));
            set.kind = workflow != nullptr ? workflow->kind : set.kind;
        } else if (const ArtGenPreset* known = FindPreset(set.preset); known != nullptr) {
            set.kind = known->kind;
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

    void ArtGenSession::PumpSets() {
        if (IsRunInWork() || IsSetupBusy() || !IsServiceConnected()) {
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

    ArtGenSetEdit ArtGenSession::PickVariant(StringView setName, StringView jobName, int64_t seed) {
        ArtGenSet* set = MutableSet(setName);
        ArtGenSetJob* job = set != nullptr ? FindJob(*set, jobName) : nullptr;
        if (job == nullptr) {
            return ArtGenSetEdit::NoSet;
        }
        if (!BECore::PickVariant(*job, seed)) {
            return ArtGenSetEdit::NoVariant;
        }
        SaveSet(*set);
        ++_version;
        return ArtGenSetEdit::Ok;
    }

    void ArtGenSession::RegisterSetControl(ControlService& service) {
        Register<&ArtGenSession::HandleEnqueue>(service, *this, "artgen_enqueue");
        Register<&ArtGenSession::HandleSet>(service, *this, "artgen_set");
        Register<&ArtGenSession::HandlePick>(service, *this, "artgen_pick");
        Register<&ArtGenSession::HandleRetry>(service, *this, "artgen_retry");

        service.DescribeMethod(
            "artgen_enqueue",
            ControlMethodInfo{"Queues a set: one preset applied to a list of subjects, one job each, run one after the other with the same first seed (so the style matches); answers at once",
                              {ControlParamInfo{"preset", "string|object", true, "a preset name, or a preset object (a draft)"},
                               ControlParamInfo{"presetName", "string", false, "with a draft: the name it is shown under"},
                               ControlParamInfo{"subjects", "string|array", true,
                                                "one string with a job per line (`name: subject`, `#` comments), or a list of such strings and {name?, subject} objects; at most 64, names unique"},
                               ControlParamInfo{"count", "integer", false, "images per job, 1 to 16 (default 4)"},
                               ControlParamInfo{"seed", "integer", false, "the first seed of every job (default: drawn once and recorded in the set)"},
                               ControlParamInfo{"set", "string", false, "the set's name and folder under artgen/out, 1 to 64 of A-Z a-z 0-9 _ - (default <preset>-<date>-<time>)"}}});
        service.DescribeMethod("artgen_set", ControlMethodInfo{"One set as its manifest.json, plus state (Active while a job is pending or running, else Complete)",
                                                               {ControlParamInfo{"set", "string", true, "the set's name"}}});
        service.DescribeMethod("artgen_pick", ControlMethodInfo{"Picks the variant of a job that Accept all takes (kept in the manifest)",
                                                                {ControlParamInfo{"set", "string", true, "the set's name"}, ControlParamInfo{"job", "string", true, "the job's name"},
                                                                 ControlParamInfo{"seed", "integer", true, "the seed of one of the job's variants"}}});
        service.DescribeMethod("artgen_retry", ControlMethodInfo{"Puts the failed and cancelled jobs of a set (or one job) back to pending; what the earlier try made is forgotten",
                                                                 {ControlParamInfo{"set", "string", true, "the set's name"}, ControlParamInfo{"job", "string", false, "only this job"}}});
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
        switch (PickVariant(*set, *job, *seed)) {
            case ArtGenSetEdit::NoSet:
                return std::unexpected(MakeError(kNoSetCode, Format("no job '{}' in a set named '{}'", *job, *set)));
            case ArtGenSetEdit::NoVariant:
                return std::unexpected(MakeError(kNoVariantCode, Format("the job '{}' has no variant of seed {}", *job, *seed)));
            case ArtGenSetEdit::Ok:
                break;
        }
        Json reply = Json::object();
        reply["set"] = ToJsonString(*set);
        reply["job"] = ToJsonString(*job);
        reply["pick"] = *seed;
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
            sets.push_back(std::move(item));
        }
        return sets;
    }

}  // namespace BECore
