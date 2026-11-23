#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
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
    /// A row never holds more variants than this, so "more variants" cannot grow the Set tab's rows without a bound
    inline constexpr int32_t kArtGenJobMaxVariants = 64;
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

    /// The extra run a job is queued or in work for: `count` more variants on `seed`, appended to what the job has
    struct ArtGenSetMore {
        int64_t seed = 0;
        int32_t count = 1;
    };

    struct ArtGenSetJob {
        String name;
        String subject;
        ArtGenSetJobState state = ArtGenSetJobState::Pending;
        String error;  // why a Failed or Cancelled job ended; on a Done job the note of a more-run that did not finish
        Vector<ArtGenSetVariant> variants;
        Optional<int64_t> pick;  // a seed of `variants`
        Optional<ArtGenSetAccepted> accepted;
        Optional<ArtGenSetMore> more;  // set while the job is Pending or Running for an extra run
    };

    /// What every image of a set is cut and scaled by, fixed at the first Accept all so the rows keep one scale whatever is switched later
    struct ArtGenSetProcessing {
        ArtGenImageSettings settings;
        ArtGenImageFrame frame;

        [[nodiscard]] bool operator==(const ArtGenSetProcessing&) const = default;
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
        Optional<ArtGenSetProcessing> processing;  // an image set: none until the first Accept all (or for good, see IsLegacyAccepted)
        Optional<ArtGenReference> reference;       // the picture every job's run is given (a style source); none: the preset's own `reference` applies
        String atlas;                              // an image set: the atlas group its sprites join and are packed into; empty: loose PNGs
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

    inline constexpr size_t kArtGenRowSubjectSize = 512;
    inline constexpr size_t kArtGenRowNameSize = 65;  // a name of 64 and the terminator

    /// One row of the Set tab: a subject and the job's name. Fixed buffers, so ImGui edits them in place and a steady frame allocates nothing
    struct ArtGenSubjectRow {
        char subject[kArtGenRowSubjectSize] = {};
        char name[kArtGenRowNameSize] = {};
        bool nameEdited = false;  // a hand-typed name stops following the subject
    };

    /// The Subject of the row changed: while the name was never typed by hand it becomes the one `ArtGenNameFromSubject` suggests
    void ArtGenRowSubjectEdited(ArtGenSubjectRow& row);

    /// The Name of the row changed by hand: it stops following the subject, and an emptied name follows again at once
    void ArtGenRowNameEdited(ArtGenSubjectRow& row);

    /// The jobs of the rows in order: a row with an empty subject is skipped, an empty name is taken from the subject. The error names the row
    /// (1-based, counting the skipped ones): a bad name, a name used by an earlier row, no letter or digit to name a job from, more than kMaxSetJobs jobs
    [[nodiscard]] std::expected<Vector<ArtGenSubject>, String> ArtGenSubjectsFromRows(const Vector<ArtGenSubjectRow>& rows);

    /// Why Queue is off, or null when it is on: no preset, no row with a subject (a queued set clears the rows), a set name that is not a name
    [[nodiscard]] const char* ArtGenQueueBlocker(bool presetLoaded, const Vector<ArtGenSubjectRow>& rows, bool setNameValid);

    /// The checks of a list of subjects that did not come from text (a control call): 1..kMaxSetJobs, valid unique names, a subject each
    [[nodiscard]] std::expected<void, String> CheckArtGenSubjects(const Vector<ArtGenSubject>& subjects);

    /// `<preset>-<yyyymmdd-hhmmss>` in UTC, the preset name made safe and cut so the whole name stays a valid one. The time has a resolution
    /// of a second, so @p attempt 2, 3, ... adds `-2`, `-3`, ... for a name that is taken (the preset part is cut further to make room)
    [[nodiscard]] String DefaultArtGenSetName(StringView presetName, std::chrono::system_clock::time_point time, int32_t attempt = 1);

    /// `2026-11-17T12:34:56Z`
    [[nodiscard]] String ArtGenTimestamp(std::chrono::system_clock::time_point time);

    /// The time of a text `ArtGenTimestamp` wrote (that one format, UTC, a real calendar date); anything else is empty
    [[nodiscard]] Optional<std::chrono::system_clock::time_point> ParseArtGenTimestamp(StringView text);

    /// The `accepted.date` of an accept at @p now, which must sort after @p latestOther, the greatest date another set holds under the same
    /// name: the stamp of @p now, or the second after @p latestOther when that is not earlier (the stamps have a resolution of a second, so
    /// two accepts of one name within it would tie). An empty or unparsable @p latestOther changes nothing
    [[nodiscard]] String ArtGenAcceptDate(std::chrono::system_clock::time_point now, StringView latestOther);

    /// The manifest of @p set (Sets/CLAUDE.md, "The manifest"). A document by hand, and so is `artgen_set` that answers it: it is a file that holds
    /// `presetData`, a `Json` payload, and writes `null` for "none", which `ArtGenSetFromJson` reads back. A reflected leaf type for a `Json`
    /// payload does not exist; one would let both go
    [[nodiscard]] Json ArtGenSetToJson(const ArtGenSet& set);

    /// A set from a manifest. Checked reads: a wrong type, an unknown `manifest` number, a bad name, a pick that is not a variant are an error
    /// that says where, never an abort; unknown keys are ignored
    [[nodiscard]] std::expected<ArtGenSet, String> ArtGenSetFromJson(const Json& manifest);

    /// True while a job is Pending or Running
    [[nodiscard]] bool IsActive(const ArtGenSet& set);
    [[nodiscard]] int32_t CountJobs(const ArtGenSet& set, ArtGenSetJobState state);
    /// The jobs that have a pick
    [[nodiscard]] int32_t PickedCount(const ArtGenSet& set);

    /// A row was accepted before sets had a frame: its files are full frames, so the set keeps writing unprocessed files and its rows never
    /// differ in scale
    [[nodiscard]] bool IsLegacyAccepted(const ArtGenSet& set);

    /// The jobs the game holds with alternatives still switchable (accepted and not kept)
    [[nodiscard]] bool IsLive(const ArtGenSetJob& job);
    [[nodiscard]] int32_t CountLive(const ArtGenSet& set);
    [[nodiscard]] int32_t CountKept(const ArtGenSet& set);
    /// The row's place in the game; @p replacedByNewer is true when another set accepted the same name later (`ArtGenSession::FindNewerOwner`)
    [[nodiscard]] ArtGenRowStatus StatusOf(const ArtGenSetJob& job, bool replacedByNewer);
    /// The variants of @p job other than its pick (all of them when it has none)
    [[nodiscard]] int32_t AlternativesOf(const ArtGenSetJob& job);
    /// The variants of @p job other than @p seed: the answer of a pick that is not written to the row yet (a batched Accept all marks the rows last)
    [[nodiscard]] int32_t AlternativesBesides(const ArtGenSetJob& job, int64_t seed);

    /// Why "more variants" is off for @p set at @p count, or null: the set is active, or no row passes `ArtGenMoreSkipReason`
    [[nodiscard]] const char* ArtGenMoreBlocker(const ArtGenSet& set, int32_t count);
    /// Why Keep is off for @p set, or null: the set is active, and a more-run still to come would add variants to a kept row (`ArtGenSession::KeepSet` refuses the same)
    [[nodiscard]] const char* ArtGenKeepBlocker(const ArtGenSet& set);
    /// Why a row of a set that is not active does not run @p count more variants (`kept`, `failed`, `cancelled`, `full`), or null.
    /// Kept rows are final, failed and cancelled ones are for Retry; `running` is only for a row of an active set, which no caller asks about
    [[nodiscard]] const char* ArtGenMoreSkipReason(const ArtGenSetJob& job, int32_t count);
    /// True when no variant of @p job has a seed in `[seed, seed + count)`: the service makes image i of a run on `seed + i`
    [[nodiscard]] bool ArtGenSeedRangeFree(const ArtGenSetJob& job, int64_t seed, int32_t count);
    /**
     * @brief The end of a run of @p job, the one place that sets Done, Failed or Cancelled. A Failed or Cancelled end of a more-run leaves the job
     * Done with @p reason as the note in `error`, because Retry of a failed row drops its earlier variants, pick and accepted. `more` is cleared
     * by every end; Done clears `error`
     */
    void EndArtGenJob(ArtGenSetJob& job, ArtGenSetJobState ended, StringView reason);

    [[nodiscard]] ArtGenSetJob* FindJob(ArtGenSet& set, StringView name);
    [[nodiscard]] const ArtGenSetJob* FindJob(const ArtGenSet& set, StringView name);
    [[nodiscard]] const ArtGenSetVariant* FindVariant(const ArtGenSetJob& job, int64_t seed);
    /// Picks the variant with @p seed; false (nothing changed) when the job has none
    bool PickVariant(ArtGenSetJob& job, int64_t seed);
    /// Fixes the choice of a live job: `kept`, @p date, and every variant but the picked one dropped from the list (their files stay on disk).
    /// False and no change when the job is not live
    bool KeepJob(ArtGenSetJob& job, StringView date);

}  // namespace BECore
