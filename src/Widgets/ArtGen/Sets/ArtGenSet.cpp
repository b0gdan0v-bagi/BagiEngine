#include "ArtGenSet.h"

#include <Widgets/ArtGen/ArtGenAccept.h>
#include <cstdio>

namespace BECore {

    namespace {

        constexpr StringView kDefaultSetPrefix = "set";
        // "-yyyymmdd-hhmmss"
        constexpr size_t kSetNameSuffixLength = 16;
        constexpr char kCommentMark = '#';
        constexpr char kNameSeparator = ':';

        bool IsBlank(char c) {
            return c == ' ' || c == '\t' || c == '\r';
        }

        StringView Trim(StringView text) {
            while (!text.empty() && IsBlank(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && IsBlank(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        bool IsNameChar(char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        bool IsKind(StringView kind) {
            return kind == kKindImage || kind == kKindAudio;
        }

        struct Civil {
            int year = 0;
            int month = 0;
            int day = 0;
            int hour = 0;
            int minute = 0;
            int second = 0;
        };

        /// UTC, so a name does not depend on the machine's zone
        Civil ToCivil(std::chrono::system_clock::time_point time) {
            const auto day = std::chrono::floor<std::chrono::days>(time);
            const std::chrono::year_month_day date{day};
            const std::chrono::hh_mm_ss clock{std::chrono::floor<std::chrono::seconds>(time - day)};
            return Civil{static_cast<int>(date.year()),
                         static_cast<int>(static_cast<unsigned>(date.month())),
                         static_cast<int>(static_cast<unsigned>(date.day())),
                         static_cast<int>(clock.hours().count()),
                         static_cast<int>(clock.minutes().count()),
                         static_cast<int>(clock.seconds().count())};
        }

        // ---- manifest reading: every member is checked, the error says where ----

        using Where = String;

        std::expected<String, String> RequireString(const Json& object, StringView key, const Where& where) {
            if (auto value = GetJsonString(object, key)) {
                return *value;
            }
            return std::unexpected(Format("{}: '{}' must be a string", where, key));
        }

        std::expected<int64_t, String> RequireInt(const Json& object, StringView key, const Where& where) {
            if (auto value = GetJsonInt(object, key)) {
                return *value;
            }
            return std::unexpected(Format("{}: '{}' must be an integer", where, key));
        }

        /// A member that may be absent or null, but when present must be a string
        std::expected<String, String> OptionalString(const Json& object, StringView key, const Where& where) {
            const Json* member = FindJsonMember(object, key);
            if (member == nullptr || member->is_null()) {
                return String();
            }
            return RequireString(object, key, where);
        }

        std::expected<ArtGenSetVariant, String> ReadVariant(const Json& item, const Where& where) {
            if (!item.is_object()) {
                return std::unexpected(Format("{}: must be an object", where));
            }
            ArtGenSetVariant variant;
            auto seed = RequireInt(item, "seed", where);
            auto file = RequireString(item, "file", where);
            auto provenance = OptionalString(item, "provenance", where);
            auto kind = OptionalString(item, "kind", where);
            if (!seed) {
                return std::unexpected(seed.error());
            }
            if (!file) {
                return std::unexpected(file.error());
            }
            if (!provenance) {
                return std::unexpected(provenance.error());
            }
            if (!kind) {
                return std::unexpected(kind.error());
            }
            variant.seed = *seed;
            variant.file = std::move(*file);
            variant.provenance = std::move(*provenance);
            if (!kind->empty()) {
                if (!IsKind(*kind)) {
                    return std::unexpected(Format("{}: 'kind' must be image or audio", where));
                }
                variant.kind = std::move(*kind);
            }
            if (variant.file.empty()) {
                return std::unexpected(Format("{}: 'file' is empty", where));
            }
            return variant;
        }

        /// The `more` of a job: absent or null is none
        std::expected<Optional<ArtGenSetMore>, String> ReadMore(const Json& item, const Where& where) {
            const Json* member = FindJsonMember(item, "more");
            if (member == nullptr || member->is_null()) {
                return Optional<ArtGenSetMore>();
            }
            const Where moreWhere = Format("{}.more", where);
            if (!member->is_object()) {
                return std::unexpected(Format("{}: must be an object or null", moreWhere));
            }
            auto seed = RequireInt(*member, "seed", moreWhere);
            auto count = RequireInt(*member, "count", moreWhere);
            if (!seed) {
                return std::unexpected(seed.error());
            }
            if (!count) {
                return std::unexpected(count.error());
            }
            if (*seed < 0) {
                return std::unexpected(Format("{}: 'seed' must not be negative", moreWhere));
            }
            if (*count < 1 || *count > kArtGenSetMaxCount) {
                return std::unexpected(Format("{}: 'count' must be 1 to {}", moreWhere, kArtGenSetMaxCount));
            }
            return Optional<ArtGenSetMore>(ArtGenSetMore{*seed, static_cast<int32_t>(*count)});
        }

        std::expected<ArtGenSetJob, String> ReadJob(const Json& item, const Where& where) {
            if (!item.is_object()) {
                return std::unexpected(Format("{}: must be an object", where));
            }
            ArtGenSetJob job;
            auto name = RequireString(item, "name", where);
            if (!name) {
                return std::unexpected(name.error());
            }
            if (!IsValidSpriteName(*name)) {
                return std::unexpected(Format("{}: '{}' is not a valid name", where, *name));
            }
            job.name = std::move(*name);
            auto subject = RequireString(item, "subject", where);
            auto state = RequireString(item, "state", where);
            auto error = OptionalString(item, "error", where);
            if (!subject) {
                return std::unexpected(subject.error());
            }
            if (!state) {
                return std::unexpected(state.error());
            }
            if (!error) {
                return std::unexpected(error.error());
            }
            const auto parsedState = EnumUtils<ArtGenSetJobState>::Cast(*state);
            if (!parsedState) {
                return std::unexpected(Format("{}: unknown state '{}'", where, *state));
            }
            job.subject = std::move(*subject);
            job.state = *parsedState;
            job.error = std::move(*error);

            const Json* variants = FindJsonMember(item, "variants");
            if (variants != nullptr && !variants->is_null()) {
                if (!variants->is_array()) {
                    return std::unexpected(Format("{}: 'variants' must be an array", where));
                }
                for (size_t index = 0; index < variants->size(); ++index) {
                    auto variant = ReadVariant((*variants)[index], Format("{}.variants[{}]", where, index));
                    if (!variant) {
                        return std::unexpected(variant.error());
                    }
                    job.variants.push_back(std::move(*variant));
                }
            }

            const Json* pick = FindJsonMember(item, "pick");
            if (pick != nullptr && !pick->is_null()) {
                const auto seed = GetJsonInt(item, "pick");
                if (!seed) {
                    return std::unexpected(Format("{}: 'pick' must be an integer or null", where));
                }
                if (FindVariant(job, *seed) == nullptr) {
                    return std::unexpected(Format("{}: 'pick' {} is not a seed of the variants", where, *seed));
                }
                job.pick = *seed;
            }

            const Json* accepted = FindJsonMember(item, "accepted");
            if (accepted != nullptr && !accepted->is_null()) {
                if (!accepted->is_object()) {
                    return std::unexpected(Format("{}: 'accepted' must be an object or null", where));
                }
                const Where acceptedWhere = Format("{}.accepted", where);
                auto as = RequireString(*accepted, "as", acceptedWhere);
                auto date = OptionalString(*accepted, "date", acceptedWhere);
                if (!as) {
                    return std::unexpected(as.error());
                }
                if (!date) {
                    return std::unexpected(date.error());
                }
                // A manifest of before Keep existed has no key: its rows were accepted with no alternatives in play, so they read as kept
                bool kept = true;
                if (const Json* keptMember = FindJsonMember(*accepted, "kept"); keptMember != nullptr) {
                    const auto parsed = GetJsonBool(*accepted, "kept");
                    if (!parsed) {
                        return std::unexpected(Format("{}: 'kept' must be a boolean", acceptedWhere));
                    }
                    kept = *parsed;
                }
                job.accepted = ArtGenSetAccepted{std::move(*as), std::move(*date), kept};
            }

            auto more = ReadMore(item, where);
            if (!more) {
                return std::unexpected(more.error());
            }
            job.more = *more;
            return job;
        }

        std::expected<ArtGenSetProcessing, String> ReadProcessing(const Json& item) {
            const Where where = "manifest.processing";
            if (!item.is_object()) {
                return std::unexpected(Format("{}: must be an object or null", where));
            }
            const Json* settings = FindJsonMember(item, "settings");
            const Json* frame = FindJsonMember(item, "frame");
            if (settings == nullptr || frame == nullptr) {
                return std::unexpected(Format("{}: needs 'settings' and 'frame'", where));
            }
            auto readSettings = ArtGenImageSettingsFromJson(*settings);
            if (!readSettings) {
                return std::unexpected(Format("{}.settings: {}", where, readSettings.error()));
            }
            auto readFrame = ArtGenImageFrameFromJson(*frame);
            if (!readFrame) {
                return std::unexpected(Format("{}.frame: {}", where, readFrame.error()));
            }
            return ArtGenSetProcessing{*readSettings, *readFrame};
        }

    }  // namespace

    String ArtGenNameFromSubject(StringView subject) {
        String name;
        bool underscore = false;
        for (const char c : subject) {
            const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
                const size_t needed = underscore && !name.empty() ? 2 : 1;
                if (name.size() + needed > kMaxSpriteNameLength) {
                    break;
                }
                if (needed == 2) {
                    name.push_back('_');
                }
                name.push_back(lower);
                underscore = false;
            } else {
                underscore = true;
            }
        }
        return name;
    }

    std::expected<void, String> CheckArtGenSubjects(const Vector<ArtGenSubject>& subjects) {
        if (subjects.empty()) {
            return std::unexpected(String("no subjects"));
        }
        if (subjects.size() > static_cast<size_t>(kMaxSetJobs)) {
            return std::unexpected(Format("{} subjects, at most {}", subjects.size(), kMaxSetJobs));
        }
        for (size_t index = 0; index < subjects.size(); ++index) {
            const ArtGenSubject& subject = subjects[index];
            if (!IsValidSpriteName(subject.name)) {
                return std::unexpected(Format("subject {}: '{}' is not a valid name (1 to {} of A-Z a-z 0-9 _ -)", index + 1, subject.name, kMaxSpriteNameLength));
            }
            if (Trim(subject.subject).empty()) {
                return std::unexpected(Format("subject {} ('{}') is empty", index + 1, subject.name));
            }
            for (size_t other = 0; other < index; ++other) {
                if (subjects[other].name == subject.name) {
                    return std::unexpected(Format("subject {}: the name '{}' is already used by subject {}", index + 1, subject.name, other + 1));
                }
            }
        }
        return {};
    }

    std::expected<Vector<ArtGenSubject>, String> ParseArtGenSubjects(StringView text) {
        Vector<ArtGenSubject> subjects;
        Vector<size_t> lineOf;
        size_t lineNumber = 0;
        size_t position = 0;
        while (position <= text.size()) {
            const size_t end = std::min(text.find('\n', position), text.size());
            const StringView line = Trim(text.substr(position, end - position));
            position = end + 1;
            ++lineNumber;
            if (line.empty() || line.front() == kCommentMark) {
                continue;
            }

            ArtGenSubject subject;
            const size_t colon = line.find(kNameSeparator);
            const StringView before = colon == StringView::npos ? StringView() : Trim(line.substr(0, colon));
            if (!before.empty() && IsValidSpriteName(before)) {
                subject.name = String(before);
                subject.subject = String(Trim(line.substr(colon + 1)));
                if (subject.subject.empty()) {
                    return std::unexpected(Format("line {}: '{}:' has no subject after it", lineNumber, before));
                }
            } else {
                subject.subject = String(line);
                subject.name = ArtGenNameFromSubject(line);
                if (subject.name.empty()) {
                    return std::unexpected(Format("line {}: no letter or digit to name the job from; write 'name: subject'", lineNumber));
                }
            }

            if (subjects.size() >= static_cast<size_t>(kMaxSetJobs)) {
                return std::unexpected(Format("line {}: more than {} subjects", lineNumber, kMaxSetJobs));
            }
            for (size_t index = 0; index < subjects.size(); ++index) {
                if (subjects[index].name == subject.name) {
                    return std::unexpected(Format("line {}: the name '{}' is already used by line {}", lineNumber, subject.name, lineOf[index]));
                }
            }
            subjects.push_back(std::move(subject));
            lineOf.push_back(lineNumber);
        }
        if (subjects.empty()) {
            return std::unexpected(String("no subjects: write one per line"));
        }
        return subjects;
    }

    static_assert(kArtGenRowNameSize == kMaxSpriteNameLength + 1);

    void ArtGenRowSubjectEdited(ArtGenSubjectRow& row) {
        if (row.nameEdited) {
            return;
        }
        const String suggested = ArtGenNameFromSubject(StringView(row.subject));
        std::snprintf(row.name, sizeof(row.name), "%s", suggested.c_str());
    }

    void ArtGenRowNameEdited(ArtGenSubjectRow& row) {
        row.nameEdited = row.name[0] != '\0';
        if (!row.nameEdited) {
            ArtGenRowSubjectEdited(row);
        }
    }

    std::expected<Vector<ArtGenSubject>, String> ArtGenSubjectsFromRows(const Vector<ArtGenSubjectRow>& rows) {
        Vector<ArtGenSubject> subjects;
        Vector<size_t> rowOf;
        for (size_t index = 0; index < rows.size(); ++index) {
            const StringView text = Trim(StringView(rows[index].subject));
            if (text.empty()) {
                continue;
            }
            ArtGenSubject subject;
            subject.subject = String(text);
            const StringView typed = Trim(StringView(rows[index].name));
            subject.name = typed.empty() ? ArtGenNameFromSubject(text) : String(typed);
            if (subject.name.empty()) {
                return std::unexpected(Format("row {}: no letter or digit to name the job from; type a name", index + 1));
            }
            if (!IsValidSpriteName(subject.name)) {
                return std::unexpected(Format("row {}: '{}' is not a valid name (1 to {} of A-Z a-z 0-9 _ -)", index + 1, subject.name, kMaxSpriteNameLength));
            }
            for (size_t other = 0; other < subjects.size(); ++other) {
                if (subjects[other].name == subject.name) {
                    return std::unexpected(Format("row {}: the name '{}' is already used by row {}", index + 1, subject.name, rowOf[other] + 1));
                }
            }
            if (subjects.size() >= static_cast<size_t>(kMaxSetJobs)) {
                return std::unexpected(Format("row {}: more than {} subjects", index + 1, kMaxSetJobs));
            }
            subjects.push_back(std::move(subject));
            rowOf.push_back(index);
        }
        if (subjects.empty()) {
            return std::unexpected(String("no subjects: fill in a subject"));
        }
        return subjects;
    }

    const char* ArtGenQueueBlocker(bool presetLoaded, const Vector<ArtGenSubjectRow>& rows, bool setNameValid) {
        if (!presetLoaded) {
            return "Pick a preset above";
        }
        const bool anySubject = std::any_of(rows.begin(), rows.end(), [](const ArtGenSubjectRow& row) { return !Trim(StringView(row.subject)).empty(); });
        if (!anySubject) {
            return "Write a subject in a row above: a queued set clears them";
        }
        if (!setNameValid) {
            return "The set name is 1 to 64 of A-Z a-z 0-9 _ -";
        }
        return nullptr;
    }

    String ArtGenTimestamp(std::chrono::system_clock::time_point time) {
        const Civil civil = ToCivil(time);
        return Format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", civil.year, civil.month, civil.day, civil.hour, civil.minute, civil.second);
    }

    String DefaultArtGenSetName(StringView presetName, std::chrono::system_clock::time_point time, int32_t attempt) {
        const String attemptSuffix = attempt > 1 ? Format("-{}", attempt) : String();
        String prefix;
        for (const char c : presetName) {
            if (prefix.size() + attemptSuffix.size() >= kMaxSpriteNameLength - kSetNameSuffixLength) {
                break;
            }
            prefix.push_back(IsNameChar(c) ? c : '_');
        }
        if (prefix.empty()) {
            prefix = String(kDefaultSetPrefix);
        }
        const Civil civil = ToCivil(time);
        return Format("{}-{:04}{:02}{:02}-{:02}{:02}{:02}{}", prefix, civil.year, civil.month, civil.day, civil.hour, civil.minute, civil.second, attemptSuffix);
    }

    Json ArtGenSetToJson(const ArtGenSet& set) {
        Json manifest = Json::object();
        manifest["manifest"] = kArtGenManifestFormat;
        manifest["set"] = MakeJsonString(set.name);
        manifest["created"] = MakeJsonString(set.created);
        manifest["kind"] = MakeJsonString(set.kind);
        manifest["preset"] = MakeJsonString(set.preset);
        manifest["presetData"] = set.presetData.is_object() ? set.presetData : Json(nullptr);
        manifest["count"] = set.count;
        manifest["seed"] = set.seed;

        Json jobs = Json::array();
        for (const ArtGenSetJob& job : set.jobs) {
            Json row = Json::object();
            row["name"] = MakeJsonString(job.name);
            row["subject"] = MakeJsonString(job.subject);
            row["state"] = MakeJsonString(EnumUtils<ArtGenSetJobState>::ToString(job.state));
            row["error"] = MakeJsonString(job.error);
            Json variants = Json::array();
            for (const ArtGenSetVariant& variant : job.variants) {
                Json entry = Json::object();
                entry["seed"] = variant.seed;
                entry["file"] = MakeJsonString(variant.file);
                entry["provenance"] = MakeJsonString(variant.provenance);
                entry["kind"] = MakeJsonString(variant.kind);
                variants.push_back(std::move(entry));
            }
            row["variants"] = std::move(variants);
            row["pick"] = job.pick ? Json(*job.pick) : Json(nullptr);
            if (job.accepted) {
                Json accepted = Json::object();
                accepted["as"] = MakeJsonString(job.accepted->as);
                accepted["date"] = MakeJsonString(job.accepted->date);
                accepted["kept"] = job.accepted->kept;
                row["accepted"] = std::move(accepted);
            } else {
                row["accepted"] = Json(nullptr);
            }
            if (job.more) {
                Json more = Json::object();
                more["seed"] = job.more->seed;
                more["count"] = job.more->count;
                row["more"] = std::move(more);
            } else {
                row["more"] = Json(nullptr);
            }
            jobs.push_back(std::move(row));
        }
        manifest["jobs"] = std::move(jobs);
        if (set.processing) {
            Json processing = Json::object();
            processing["settings"] = ArtGenImageSettingsToJson(set.processing->settings);
            processing["frame"] = ArtGenImageFrameToJson(set.processing->frame);
            manifest["processing"] = std::move(processing);
        } else {
            manifest["processing"] = Json(nullptr);
        }
        manifest["reference"] = set.reference ? ArtGenReferenceToJson(*set.reference) : Json(nullptr);
        return manifest;
    }

    std::expected<ArtGenSet, String> ArtGenSetFromJson(const Json& manifest) {
        const Where top = "manifest";
        if (!manifest.is_object()) {
            return std::unexpected(String("manifest: not a JSON object"));
        }
        const auto format = GetJsonInt(manifest, "manifest");
        if (!format) {
            return std::unexpected(String("manifest: 'manifest' (the format number) must be an integer"));
        }
        if (*format != kArtGenManifestFormat) {
            return std::unexpected(Format("manifest: format {} is not known to this editor (it reads {})", *format, kArtGenManifestFormat));
        }

        ArtGenSet set;
        auto name = RequireString(manifest, "set", top);
        auto created = OptionalString(manifest, "created", top);
        auto kind = RequireString(manifest, "kind", top);
        auto preset = OptionalString(manifest, "preset", top);
        auto count = RequireInt(manifest, "count", top);
        auto seed = RequireInt(manifest, "seed", top);
        if (!name) {
            return std::unexpected(name.error());
        }
        if (!created) {
            return std::unexpected(created.error());
        }
        if (!kind) {
            return std::unexpected(kind.error());
        }
        if (!preset) {
            return std::unexpected(preset.error());
        }
        if (!count) {
            return std::unexpected(count.error());
        }
        if (!seed) {
            return std::unexpected(seed.error());
        }
        if (!IsValidSpriteName(*name)) {
            return std::unexpected(Format("manifest: '{}' is not a valid set name", *name));
        }
        if (!IsKind(*kind)) {
            return std::unexpected(String("manifest: 'kind' must be image or audio"));
        }
        if (*count < 1 || *count > kArtGenSetMaxCount) {
            return std::unexpected(Format("manifest: 'count' must be 1 to {}", kArtGenSetMaxCount));
        }
        set.name = std::move(*name);
        set.created = std::move(*created);
        set.kind = std::move(*kind);
        set.preset = std::move(*preset);
        set.count = static_cast<int32_t>(*count);
        set.seed = *seed;

        const Json* presetData = FindJsonMember(manifest, "presetData");
        if (presetData != nullptr && !presetData->is_null()) {
            if (!presetData->is_object()) {
                return std::unexpected(String("manifest: 'presetData' must be an object or null"));
            }
            set.presetData = *presetData;
        }

        const Json* jobs = FindJsonMember(manifest, "jobs");
        if (jobs == nullptr || !jobs->is_array()) {
            return std::unexpected(String("manifest: 'jobs' must be an array"));
        }
        if (jobs->empty() || jobs->size() > static_cast<size_t>(kMaxSetJobs)) {
            return std::unexpected(Format("manifest: 'jobs' must hold 1 to {} jobs", kMaxSetJobs));
        }
        for (size_t index = 0; index < jobs->size(); ++index) {
            auto job = ReadJob((*jobs)[index], Format("jobs[{}]", index));
            if (!job) {
                return std::unexpected(job.error());
            }
            if (FindJob(set, job->name) != nullptr) {
                return std::unexpected(Format("jobs[{}]: the name '{}' is used twice", index, job->name));
            }
            set.jobs.push_back(std::move(*job));
        }

        const Json* processing = FindJsonMember(manifest, "processing");
        if (processing != nullptr && !processing->is_null()) {
            auto read = ReadProcessing(*processing);
            if (!read) {
                return std::unexpected(read.error());
            }
            set.processing = *read;
        }

        const Json* reference = FindJsonMember(manifest, "reference");
        if (reference != nullptr && !reference->is_null()) {
            auto read = ReadArtGenReference(*reference);
            if (!read) {
                return std::unexpected(Format("manifest: {}", read.error()));
            }
            set.reference = std::move(*read);
        }
        return set;
    }

    bool IsActive(const ArtGenSet& set) {
        return std::any_of(set.jobs.begin(), set.jobs.end(), [](const ArtGenSetJob& job) { return job.state == ArtGenSetJobState::Pending || job.state == ArtGenSetJobState::Running; });
    }

    int32_t CountJobs(const ArtGenSet& set, ArtGenSetJobState state) {
        return static_cast<int32_t>(std::count_if(set.jobs.begin(), set.jobs.end(), [state](const ArtGenSetJob& job) { return job.state == state; }));
    }

    int32_t PickedCount(const ArtGenSet& set) {
        return static_cast<int32_t>(std::count_if(set.jobs.begin(), set.jobs.end(), [](const ArtGenSetJob& job) { return job.pick.has_value(); }));
    }

    bool IsLegacyAccepted(const ArtGenSet& set) {
        return !set.processing && std::any_of(set.jobs.begin(), set.jobs.end(), [](const ArtGenSetJob& job) { return job.accepted.has_value(); });
    }

    bool IsLive(const ArtGenSetJob& job) {
        return job.accepted.has_value() && !job.accepted->kept;
    }

    int32_t CountLive(const ArtGenSet& set) {
        return static_cast<int32_t>(std::count_if(set.jobs.begin(), set.jobs.end(), [](const ArtGenSetJob& job) { return IsLive(job); }));
    }

    int32_t CountKept(const ArtGenSet& set) {
        return static_cast<int32_t>(std::count_if(set.jobs.begin(), set.jobs.end(), [](const ArtGenSetJob& job) { return job.accepted && job.accepted->kept; }));
    }

    ArtGenRowStatus StatusOf(const ArtGenSetJob& job, bool replacedByNewer) {
        if (!job.accepted) {
            return ArtGenRowStatus::NotInGame;
        }
        if (replacedByNewer) {
            return ArtGenRowStatus::Replaced;
        }
        return job.accepted->kept ? ArtGenRowStatus::Kept : ArtGenRowStatus::Live;
    }

    int32_t AlternativesOf(const ArtGenSetJob& job) {
        return static_cast<int32_t>(std::count_if(job.variants.begin(), job.variants.end(), [&job](const ArtGenSetVariant& variant) { return !job.pick || variant.seed != *job.pick; }));
    }

    const char* ArtGenMoreBlocker(const ArtGenSet& set, int32_t count) {
        if (IsActive(set)) {
            return "The set is still running";
        }
        const bool anyEligible = std::any_of(set.jobs.begin(), set.jobs.end(), [count](const ArtGenSetJob& job) { return ArtGenMoreSkipReason(job, count) == nullptr; });
        return anyEligible ? nullptr : "No row can get more variants: kept, failed, cancelled and full rows are left out";
    }

    const char* ArtGenKeepBlocker(const ArtGenSet& set) {
        return IsActive(set) ? "The set is still running: a row kept now would get the variants still to come. Wait for it or cancel its jobs" : nullptr;
    }

    const char* ArtGenMoreSkipReason(const ArtGenSetJob& job, int32_t count) {
        switch (job.state) {
            case ArtGenSetJobState::Failed:
                return "failed";
            case ArtGenSetJobState::Cancelled:
                return "cancelled";
            case ArtGenSetJobState::Pending:
            case ArtGenSetJobState::Running:
                return "running";
            case ArtGenSetJobState::Done:
                break;
        }
        if (job.accepted && job.accepted->kept) {
            return "kept";
        }
        return job.variants.size() + static_cast<size_t>(count) > static_cast<size_t>(kArtGenJobMaxVariants) ? "full" : nullptr;
    }

    bool ArtGenSeedRangeFree(const ArtGenSetJob& job, int64_t seed, int32_t count) {
        // The unsigned distance of a seed that is not below `seed` cannot overflow, which `seed + count` could
        return std::none_of(job.variants.begin(), job.variants.end(), [seed, count](const ArtGenSetVariant& variant) {
            return variant.seed >= seed && static_cast<uint64_t>(variant.seed) - static_cast<uint64_t>(seed) < static_cast<uint64_t>(count);
        });
    }

    void EndArtGenJob(ArtGenSetJob& job, ArtGenSetJobState ended, StringView reason) {
        const bool lost = ended == ArtGenSetJobState::Failed || ended == ArtGenSetJobState::Cancelled;
        if (lost && job.more) {
            job.state = ArtGenSetJobState::Done;
            job.error = String(reason);
        } else {
            job.state = ended;
            job.error = ended == ArtGenSetJobState::Done ? String() : String(reason);
        }
        job.more.reset();
    }

    ArtGenSetJob* FindJob(ArtGenSet& set, StringView name) {
        const auto it = std::find_if(set.jobs.begin(), set.jobs.end(), [&](const ArtGenSetJob& job) { return StringView(job.name) == name; });
        return it == set.jobs.end() ? nullptr : &*it;
    }

    const ArtGenSetJob* FindJob(const ArtGenSet& set, StringView name) {
        const auto it = std::find_if(set.jobs.begin(), set.jobs.end(), [&](const ArtGenSetJob& job) { return StringView(job.name) == name; });
        return it == set.jobs.end() ? nullptr : &*it;
    }

    const ArtGenSetVariant* FindVariant(const ArtGenSetJob& job, int64_t seed) {
        const auto it = std::find_if(job.variants.begin(), job.variants.end(), [seed](const ArtGenSetVariant& variant) { return variant.seed == seed; });
        return it == job.variants.end() ? nullptr : &*it;
    }

    bool PickVariant(ArtGenSetJob& job, int64_t seed) {
        if (FindVariant(job, seed) == nullptr) {
            return false;
        }
        job.pick = seed;
        return true;
    }

    bool KeepJob(ArtGenSetJob& job, StringView date) {
        if (!IsLive(job) || !job.pick || FindVariant(job, *job.pick) == nullptr) {
            return false;
        }
        const int64_t pick = *job.pick;
        job.variants.erase(std::remove_if(job.variants.begin(), job.variants.end(), [pick](const ArtGenSetVariant& variant) { return variant.seed != pick; }), job.variants.end());
        job.accepted->kept = true;
        job.accepted->date = String(date);
        return true;
    }

}  // namespace BECore
