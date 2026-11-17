#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <chrono>
#include <cstdint>
#include <expected>

namespace BECore {

    CORE_ENUM(ArtGenSetJobState, uint8_t, Pending, Running, Done, Failed, Cancelled)

    /// Whether a job's pick is in the game: not accepted, accepted and switchable, accepted and final, or accepted and then taken over by a later set
    CORE_ENUM(ArtGenRowStatus, uint8_t, NotInGame, Live, Kept, Replaced)

    /// The format number of `manifest.json`; a reader refuses any other
    inline constexpr int64_t kArtGenManifestFormat = 1;
    inline constexpr int32_t kMaxSetJobs = 64;
    inline constexpr int32_t kArtGenSetMaxCount = 16;
    inline constexpr int32_t kArtGenSetDefaultCount = 4;
    /// The two kinds of what a preset makes, as the service reports them and the manifest keeps them
    inline constexpr StringView kKindImage = "image";
    inline constexpr StringView kKindAudio = "audio";

    /// One line of the subjects of a set: the job's name (folder, file stem, sprite or clip name) and what to make
    struct ArtGenSubject {
        String name;
        String subject;
    };

    /// One image or sound a job made. Paths are relative to the set's folder with `/`, so the folder can be moved
    struct ArtGenSetVariant {
        int64_t seed = 0;
        String file;
        String provenance;
        String kind = "image";  // "image" or "audio"
    };

    /// What Accept all did to a job: the sprite or clip name it was accepted as and when (UTC, `ArtGenTimestamp`).
    /// An accepted job that is not `kept` is live: the game holds its pick and the other variants are alternatives to switch to
    struct ArtGenSetAccepted {
        String as;
        String date;
        bool kept = false;
    };

    struct ArtGenSetJob {
        String name;
        String subject;
        ArtGenSetJobState state = ArtGenSetJobState::Pending;
        String error;  // why a Failed or Cancelled job ended
        Vector<ArtGenSetVariant> variants;
        Optional<int64_t> pick;  // a seed of `variants`
        Optional<ArtGenSetAccepted> accepted;
    };

    /// A named batch: one preset applied to a list of subjects, one job per subject, all with the same first seed so the style matches.
    /// Plain data over the manifest; the session runs it, the store keeps it (Sets/CLAUDE.md)
    struct ArtGenSet {
        String name;     // the folder under `artgen/out/`
        String created;  // `ArtGenTimestamp`, UTC
        String kind = "image";
        String preset;    // the preset's name, or the form's name of a draft
        Json presetData;  // a draft's content; null when the preset is used by name
        int32_t count = kArtGenSetDefaultCount;
        int64_t seed = 0;  // the first seed of every job
        Vector<ArtGenSetJob> jobs;
    };

    /// The set name or job name from a subject: "White Knight!" -> "white_knight". Lowercase letters and digits, one `_` between words,
    /// cut at 64; empty when the subject has no letter or digit
    [[nodiscard]] String ArtGenNameFromSubject(StringView subject);

    /**
     * @brief The jobs of a set from the lines of @p text: one job per line, trimmed; an empty line and a line that starts with `#` are skipped.
     * `name: subject` names the job when the text before the first `:` is a valid name, else the name comes from the subject
     * (ArtGenNameFromSubject). The error names the line (1-based): a subject with no letter, a name used twice, more than kMaxSetJobs jobs.
     */
    [[nodiscard]] std::expected<Vector<ArtGenSubject>, String> ParseArtGenSubjects(StringView text);

    /// The checks of a list of subjects that did not come from text (a control call): 1..kMaxSetJobs, valid unique names, a subject each
    [[nodiscard]] std::expected<void, String> CheckArtGenSubjects(const Vector<ArtGenSubject>& subjects);

    /// `<preset>-<yyyymmdd-hhmmss>` in UTC, the preset name made safe and cut so the whole name stays a valid one. The time has a resolution
    /// of a second, so @p attempt 2, 3, ... adds `-2`, `-3`, ... for a name that is taken (the preset part is cut further to make room)
    [[nodiscard]] String DefaultArtGenSetName(StringView presetName, std::chrono::system_clock::time_point time, int32_t attempt = 1);

    /// `2026-11-17T12:34:56Z`
    [[nodiscard]] String ArtGenTimestamp(std::chrono::system_clock::time_point time);

    /// The manifest of @p set (Sets/CLAUDE.md, "The manifest")
    [[nodiscard]] Json ArtGenSetToJson(const ArtGenSet& set);

    /// A set from a manifest. Checked reads: a wrong type, an unknown `manifest` number, a bad name, a pick that is not a variant are an error
    /// that says where, never an abort; unknown keys are ignored
    [[nodiscard]] std::expected<ArtGenSet, String> ArtGenSetFromJson(const Json& manifest);

    /// True while a job is Pending or Running
    [[nodiscard]] bool IsActive(const ArtGenSet& set);
    [[nodiscard]] int32_t CountJobs(const ArtGenSet& set, ArtGenSetJobState state);
    /// The jobs that have a pick
    [[nodiscard]] int32_t PickedCount(const ArtGenSet& set);

    /// The jobs the game holds with alternatives still switchable (accepted and not kept)
    [[nodiscard]] bool IsLive(const ArtGenSetJob& job);
    [[nodiscard]] int32_t CountLive(const ArtGenSet& set);
    [[nodiscard]] int32_t CountKept(const ArtGenSet& set);
    /// The row's place in the game; @p replacedByNewer is true when another set accepted the same name later (`ArtGenSession::FindNewerOwner`)
    [[nodiscard]] ArtGenRowStatus StatusOf(const ArtGenSetJob& job, bool replacedByNewer);
    /// The variants of @p job other than its pick (all of them when it has none)
    [[nodiscard]] int32_t AlternativesOf(const ArtGenSetJob& job);

    [[nodiscard]] ArtGenSetJob* FindJob(ArtGenSet& set, StringView name);
    [[nodiscard]] const ArtGenSetJob* FindJob(const ArtGenSet& set, StringView name);
    [[nodiscard]] const ArtGenSetVariant* FindVariant(const ArtGenSetJob& job, int64_t seed);
    /// Picks the variant with @p seed; false (nothing changed) when the job has none
    bool PickVariant(ArtGenSetJob& job, int64_t seed);
    /// Fixes the choice of a live job: `kept`, @p date, and every variant but the picked one dropped from the list (their files stay on disk).
    /// False and no change when the job is not live
    bool KeepJob(ArtGenSetJob& job, StringView date);

}  // namespace BECore
