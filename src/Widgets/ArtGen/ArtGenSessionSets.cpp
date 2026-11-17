#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <chrono>
#include <random>

namespace BECore {

    namespace {

        constexpr StringView kDraftName = "draft";
        constexpr StringView kKindImage = "image";
        constexpr StringView kCancelledReason = "cancelled";
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

}  // namespace BECore
