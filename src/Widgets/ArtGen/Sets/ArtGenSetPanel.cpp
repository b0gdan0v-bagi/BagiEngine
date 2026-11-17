#include "ArtGenSetPanel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <cstdio>

namespace BECore {

    namespace {

        constexpr float kSubjectLines = 6.0f;
        constexpr const char* kSingleRunNote = "A single run is in work on the Generate tab; the set starts after it";
        constexpr const char* kSetDraftNote = "A set is made into artgen/out/<set>/ (drafts, git ignores them). Nothing reaches the game until Accept all on the Review tab.";
        constexpr const char* kReviewDraftNote =
            "Variants are drafts in artgen/out/<set>/ (git ignores that folder). Accept all, and a click on a live row, write into the game: assets/artgen/ and config/TextureLibrary.xml, "
            "which are in git.";
        constexpr const char* kAcceptAllTip =
            "Put a candidate of every row into the game under the row's name: the pick, else the first variant. It writes assets/artgen/<job>.png (.wav for a sound) with its "
            ".artgen.json and, for images, the sprite in config/TextureLibrary.xml; these are in git. The other variants stay switchable until Keep";
        constexpr const char* kKeepTip =
            "Keep this variant for good: the others are dropped from the set and the row can no longer be switched. Changes only the game's provenance file (\"kept\") and the manifest";
        constexpr const char* kKeepAllTip = "Keep the shown variant of every live row for good: the others are dropped from the set and the rows can no longer be switched. Changes only the game's "
                                            "provenance files (\"kept\") and the manifest";
        constexpr const char* kReplacedExplanation =
            "The game has one file per name. A set that accepted this name later owns it, so this row is only a draft in artgen/out/: it cannot be switched or kept. Accept a newer set with this name "
            "to take the file over.";
        constexpr float kJobIndent = 16.0f;
        // Squares behind a thumbnail, so a transparent background (a cut-out sprite) shows as transparent
        constexpr float kCheckerCell = 10.0f;
        constexpr size_t kLabelSize = 160;
        // A review thumbnail is smaller than the Generate grid's, so a row of four fits the panel
        constexpr float kReviewThumbnailSize = 112.0f;

        const char* IconOfState(ArtGenSetJobState state) {
            switch (state) {
                case ArtGenSetJobState::Pending:
                    return ICON_LC_CLOCK;
                case ArtGenSetJobState::Running:
                    return ICON_LC_LOADER;
                case ArtGenSetJobState::Done:
                    return ICON_LC_CIRCLE_CHECK;
                case ArtGenSetJobState::Failed:
                    return ICON_LC_CIRCLE_ALERT;
                case ArtGenSetJobState::Cancelled:
                    return ICON_LC_BAN;
                default:
                    return ICON_LC_CIRCLE_SLASH;
            }
        }

        ImVec4 ColorOfState(const ImGuiPalette& palette, ArtGenSetJobState state) {
            switch (state) {
                case ArtGenSetJobState::Running:
                    return palette.accent;
                case ArtGenSetJobState::Done:
                    return palette.success;
                case ArtGenSetJobState::Failed:
                    return palette.error;
                default:
                    return palette.textDisabled;
            }
        }

        /// The bar of the run in work: image n of N, step a of b
        void DrawRunProgress(const ArtGenRun& run) {
            if (run.state == ArtGenRunState::Queued) {
                ImGui::TextDisabled("Waiting for the service to take the job...");
                return;
            }
            const int32_t made = static_cast<int32_t>(run.variants.size()) - run.keptVariants;
            const float perImage = run.steps > 0 ? static_cast<float>(run.step) / static_cast<float>(run.steps) : 0.0f;
            const float fraction = run.count > 0 ? (static_cast<float>(made) + perImage) / static_cast<float>(run.count) : 0.0f;
            char overlay[kLabelSize];
            std::snprintf(overlay, sizeof(overlay), "%s: image %d of %d, step %d of %d", run.jobName.c_str(), static_cast<int>(made) + 1, run.count, run.step, run.steps);
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
            if (run.cancelRequested) {
                ImGui::TextDisabled("Cancelling...");
            }
        }

    }  // namespace

    void DrawArtGenChecker(ImDrawList* drawList, ImVec2 origin, float size, ImU32 light, ImU32 dark) {
        drawList->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), light);
        const int cells = static_cast<int>(size / kCheckerCell);
        for (int y = 0; y < cells; ++y) {
            for (int x = (y % 2); x < cells; x += 2) {
                const ImVec2 min(origin.x + static_cast<float>(x) * kCheckerCell, origin.y + static_cast<float>(y) * kCheckerCell);
                drawList->AddRectFilled(min, ImVec2(min.x + kCheckerCell, min.y + kCheckerCell), dark);
            }
        }
    }

    void ArtGenSetPanel::DrawQueue(ArtGenSession& session, const ArtGenSetPanelForm& form) {
        const auto& palette = GetImGuiPalette();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("One job per line: 'name: subject' (the name is the file and sprite name) or just the subject. Empty lines and lines starting with # are skipped.");
        ImGui::PopTextWrapPos();
        ImGui::InputTextMultiline("##setSubjects", _subjects, sizeof(_subjects), ImVec2(-1.0f, ImGui::GetTextLineHeight() * kSubjectLines));
        ImGui::InputTextWithHint("Set name", "<preset>-<date> when empty", _setName, sizeof(_setName));
        ItemTooltip("The folder under artgen/out/ that keeps the set; 1 to 64 of A-Z a-z 0-9 _ -");
        const bool nameValid = _setName[0] == '\0' || IsValidSpriteName(StringView(_setName));
        if (!nameValid) {
            ImGui::TextColored(palette.warning, "1 to 64 of A-Z a-z 0-9 _ -");
        }
        {
            ScopedMonoFont mono;
            ImGui::InputInt("Count", &_count);
        }
        _count = std::clamp(_count, 1, kArtGenSetMaxCount);
        ItemTooltip("Variants made for every job of this set");
        ImGui::Checkbox("Random seed", &_randomSeed);
        ItemTooltip("Off: the same first seed for every job of the set, the one below");
        if (!_randomSeed) {
            ImGui::SameLine();
            ScopedMonoFont mono;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputScalar("##setSeed", ImGuiDataType_S64, &_seed);
            _seed = std::max<int64_t>(_seed, 0);
        }

        const bool canQueue = !form.preset.IsEmpty() && _subjects[0] != '\0' && nameValid;
        ImGui::BeginDisabled(!canQueue);
        if (IconTextButton(ICON_LC_LIST_PLUS, "Queue",
                           "Make every subject with the preset above and this set's count and seed, one job after another; a set draws one seed for all its jobs when random")) {
            _error.clear();
            _queued.clear();
            auto subjects = ParseArtGenSubjects(StringView(_subjects));
            if (subjects) {
                ArtGenSetRequest request;
                request.subjects = std::move(*subjects);
                request.count = _count;
                request.seed = _randomSeed ? Optional<int64_t>() : Optional<int64_t>(_seed);
                request.name = String(_setName);
                // A saved preset goes by name, so its provenance names the file; anything else goes inline as a draft
                if (form.preset.IsDirty()) {
                    request.preset = String(form.preset.name);
                    request.presetData = form.preset.ToJson();
                } else {
                    request.preset = form.preset.GetBaseName();
                }
                const int32_t jobs = static_cast<int32_t>(request.subjects.size());
                auto queued = session.EnqueueSet(std::move(request));
                if (queued) {
                    _queued = Format("Queued '{}': {} job(s)", *queued, jobs);
                    _subjects[0] = '\0';
                    _setName[0] = '\0';
                } else {
                    _error = queued.error();
                }
            } else {
                _error = subjects.error();
            }
        }
        ImGui::EndDisabled();
        if (session.IsSingleRunInWork()) {
            ImGui::TextDisabled("%s", kSingleRunNote);
        }
        if (!_error.empty()) {
            ImGui::TextColored(palette.error, "%s", _error.c_str());
        }
        if (!_queued.empty()) {
            ImGui::TextColored(palette.success, "%s", _queued.c_str());
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", kSetDraftNote);
        ImGui::PopTextWrapPos();
        ImGui::Separator();

        const auto& sets = session.GetSets();
        if (sets.empty()) {
            EmptyState(ICON_LC_LIST_CHECKS, "No sets yet: write the subjects above and press Queue");
            return;
        }
        ImGui::BeginChild("##sets");
        for (size_t i = sets.size(); i-- > 0;) {
            DrawSetEntry(session, sets[i]);
        }
        ImGui::EndChild();
    }

    void ArtGenSetPanel::DrawSetEntry(ArtGenSession& session, const ArtGenSet& set) {
        const auto& palette = GetImGuiPalette();
        const bool active = IsActive(set);
        const int32_t total = static_cast<int32_t>(set.jobs.size());
        const int32_t done = CountJobs(set, ArtGenSetJobState::Done);
        const int32_t failed = CountJobs(set, ArtGenSetJobState::Failed);
        const int32_t cancelled = CountJobs(set, ArtGenSetJobState::Cancelled);

        ImGui::PushID(set.name.c_str());
        ImGui::Text("%s %s", set.kind == kKindAudio ? ICON_LC_AUDIO_LINES : ICON_LC_IMAGE, set.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", set.preset.c_str());

        char summary[kLabelSize];
        int length = std::snprintf(summary, sizeof(summary), "%s  %d/%d done", active ? "Active" : "Complete", static_cast<int>(done), static_cast<int>(total));
        if (failed > 0 && length > 0 && static_cast<size_t>(length) < sizeof(summary)) {
            length += std::snprintf(summary + length, sizeof(summary) - static_cast<size_t>(length), ", %d failed", static_cast<int>(failed));
        }
        if (cancelled > 0 && length > 0 && static_cast<size_t>(length) < sizeof(summary)) {
            std::snprintf(summary + length, sizeof(summary) - static_cast<size_t>(length), ", %d cancelled", static_cast<int>(cancelled));
        }
        {
            ScopedMonoFont mono;
            ImGui::TextColored(failed > 0 ? palette.error : (active ? palette.accent : palette.success), "%s", summary);
        }

        const ArtGenRun& run = session.GetRun();
        if (StringView(run.setName) == StringView(set.name) && (run.state == ArtGenRunState::Queued || run.state == ArtGenRunState::Running)) {
            DrawRunProgress(run);
        }

        ImGui::BeginDisabled(!active);
        if (IconTextButton(ICON_LC_X, "Cancel set", "Cancel every waiting job and the one in work; the images already made stay")) {
            session.CancelSet(StringView(set.name));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(failed + cancelled == 0);
        if (IconTextButton(ICON_LC_ROTATE_CCW, "Retry", "Put the failed and cancelled jobs back in the queue; their earlier images are dropped")) {
            session.RetryJobs(StringView(set.name));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (IconTextButton(ICON_LC_EYE, "Review", "Look at the variants of every job, put them into the game and flip between them, then keep the ones you like")) {
            _reviewSet = set.name;
            _openReview = true;
            _acceptMessage.clear();
        }

        if (active) {
            DrawJobRows(session, set);
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    void ArtGenSetPanel::DrawJobRows(ArtGenSession& session, const ArtGenSet& set) {
        const auto& palette = GetImGuiPalette();
        ImGui::Indent(kJobIndent);
        for (size_t i = 0; i < set.jobs.size(); ++i) {
            const ArtGenSetJob& job = set.jobs[i];
            ImGui::PushID(static_cast<int>(i));
            const bool open = job.state == ArtGenSetJobState::Pending || job.state == ArtGenSetJobState::Running;
            if (open) {
                if (IconButton(ICON_LC_X, "Cancel this job; the others go on", "##cancelJob")) {
                    session.CancelJob(StringView(set.name), StringView(job.name));
                }
                ImGui::SameLine();
            }
            ImGui::TextColored(ColorOfState(palette, job.state), "%s", IconOfState(job.state));
            ImGui::SameLine();
            ImGui::TextUnformatted(job.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", job.subject.c_str());
            if (job.state == ArtGenSetJobState::Failed) {
                ImGui::SameLine();
                ImGui::TextColored(palette.error, "%s", job.error.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Unindent(kJobIndent);
    }

    ArtGenSetPanel::~ArtGenSetPanel() {
        DropThumbnails();
    }

    bool ArtGenSetPanel::TakeReviewRequest() {
        const bool requested = _openReview;
        _openReview = false;
        return requested;
    }

    const ArtGenSetPanel::Thumb* ArtGenSetPanel::FindThumb(const RowThumbs& row, int64_t seed) {
        const auto it = std::find_if(row.thumbs.begin(), row.thumbs.end(), [&](const Thumb& thumb) { return thumb.seed == seed; });
        return it == row.thumbs.end() ? nullptr : &*it;
    }

    void ArtGenSetPanel::EvictThumb(Thumb& thumb) {
        // The pictures of a review are seen a few times: keeping them in the cache would hold every texture of a set until the editor closes
        if (!thumb.missing && !thumb.path.empty()) {
            CoreManager::GetResourceManager().Evict(StringView(thumb.path));
        }
        thumb.texture = ResourceHandle<ITexture>();
        thumb.clip = ResourceHandle<AudioClip>();
    }

    void ArtGenSetPanel::EvictRow(RowThumbs& row) {
        for (Thumb& thumb : row.thumbs) {
            EvictThumb(thumb);
        }
        row.thumbs.clear();
    }

    void ArtGenSetPanel::DropThumbnails() {
        for (RowThumbs& row : _rows) {
            EvictRow(row);
        }
        _rows.clear();
        _loadedSet.clear();
    }

    void ArtGenSetPanel::SyncRow(const ArtGenSession& session, const ArtGenSet& set, size_t index) {
        RowThumbs& row = _rows[index];
        const ArtGenSetJob& job = set.jobs[index];
        for (size_t i = 0; i < row.thumbs.size();) {
            if (FindVariant(job, row.thumbs[i].seed) == nullptr) {
                EvictThumb(row.thumbs[i]);
                row.thumbs.erase(row.thumbs.begin() + static_cast<ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        auto& resources = CoreManager::GetResourceManager();
        for (const ArtGenSetVariant& variant : job.variants) {
            if (FindThumb(row, variant.seed) != nullptr) {
                continue;
            }
            Thumb thumb;
            thumb.seed = variant.seed;
            const std::filesystem::path root(ToStd(StringView(session.GetGameRoot())));  //-NoStd: std::filesystem::path takes a std::string_view
            const std::filesystem::path file = AbsoluteInSet(root, StringView(set.name), StringView(variant.file));
            std::error_code error;
            if (file.empty() || !std::filesystem::is_regular_file(file, error)) {
                thumb.missing = true;
            } else {
                const std::string text = file.generic_string();  //-NoStd: the text of a std::filesystem::path
                thumb.path = String(FromStd(text));
                if (StringView(variant.kind) == kKindAudio) {
                    thumb.clip = resources.Load<AudioClip>(StringView(thumb.path));
                } else {
                    thumb.texture = resources.Load<ITexture>(StringView(thumb.path));
                }
            }
            row.thumbs.push_back(std::move(thumb));
        }
    }

    void ArtGenSetPanel::DrawReview(ArtGenSession& session, const ArtGenSetPanelSound& sound) {
        const auto& palette = GetImGuiPalette();
        const ArtGenSet* set = _reviewSet.empty() ? nullptr : session.FindSet(StringView(_reviewSet));
        if (set == nullptr) {
            if (sound.playing) {
                sound.stop();
            }
            DropThumbnails();
            _reviewSet.clear();
            EmptyState(ICON_LC_EYE, "Press Review on a set of the Set tab to pick the best variant of each subject");
            return;
        }
        if (StringView(_loadedSet) != StringView(set->name)) {
            if (sound.playing) {
                sound.stop();
            }
            DropThumbnails();
            _loadedSet = set->name;
            _acceptMessage.clear();
        }
        _rows.resize(set->jobs.size());
        ++_frame;

        if (IconButton(ICON_LC_X, "Close the review and free its pictures", "##closeReview")) {
            if (sound.playing) {
                sound.stop();
            }
            DropThumbnails();
            _reviewSet.clear();
            return;
        }
        ImGui::SameLine();
        ImGui::Text("%s %s", set->kind == kKindAudio ? ICON_LC_AUDIO_LINES : ICON_LC_IMAGE, set->name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(IsActive(*set) ? palette.accent : palette.textDisabled, "%s", IsActive(*set) ? "still running" : "complete");

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", kReviewDraftNote);
        ImGui::PopTextWrapPos();

        const ImGuiStyle& style = ImGui::GetStyle();
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + (_acceptMessage.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing());
        ImGui::BeginChild("##reviewRows", ImVec2(0.0f, -footerHeight));
        // Every row has the height of a full one, so the clipper knows where row n is without drawing the rows before it
        const float inner = ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2.0f;
        const int32_t columns = std::max(1, static_cast<int32_t>((inner + style.ItemSpacing.x) / (kReviewThumbnailSize + style.ItemSpacing.x)));
        const int32_t thumbRows = (std::max(set->count, 1) + columns - 1) / columns;
        const float cellHeight = kReviewThumbnailSize + style.ItemSpacing.y + ImGui::GetTextLineHeightWithSpacing();
        // The header line and the status line, the hint line with its buttons, then the thumbnails
        const float rowHeight = style.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetFrameHeightWithSpacing() + static_cast<float>(thumbRows) * cellHeight;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(set->jobs.size()), rowHeight + style.ItemSpacing.y);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImGui::PushID(i);
                ImGui::BeginChild("##row", ImVec2(0.0f, rowHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                DrawReviewRow(session, *set, static_cast<size_t>(i), columns, sound);
                ImGui::EndChild();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        // A row that left the screen gives its pictures back
        for (size_t i = 0; i < _rows.size(); ++i) {
            if (_rows[i].seenFrame != _frame && !_rows[i].thumbs.empty()) {
                if (sound.playing && _playingJob == static_cast<int32_t>(i)) {
                    sound.stop();
                }
                EvictRow(_rows[i]);
            }
        }
        DrawReviewFooter(session, *set);
    }

    void ArtGenSetPanel::RefreshStatusLine(RowThumbs& row, const ArtGenSet& set, const ArtGenSetJob& job, ArtGenRowStatus status, const ArtGenSet* newer) {
        // The text is made again only when what it says moved, so a steady frame allocates nothing
        const StringView name = job.accepted ? StringView(job.accepted->as) : StringView(job.name);
        const StringView newerName = newer != nullptr ? StringView(newer->name) : StringView();
        if (row.statusValid && row.statusKey == status && StringView(row.statusName) == name && StringView(row.statusNewer) == newerName && StringView(row.statusKind) == StringView(set.kind)) {
            return;
        }
        row.statusValid = true;
        row.statusKey = status;
        row.statusName = String(name);
        row.statusNewer = String(newerName);
        row.statusKind = set.kind;
        const String file = ArtGenGameFile(name, set.kind);
        const bool image = set.kind != kKindAudio;
        switch (status) {
            case ArtGenRowStatus::NotInGame:
                row.statusLine = Format("Not in the game. Accept all writes {}", file);
                break;
            case ArtGenRowStatus::Live:
                row.statusLine = image ? Format("In the game: {} (sprite '{}' in TextureLibrary); the other variants are one click away", file, name)
                                       : Format("In the game: {}; the other variants are one click away", file);
                break;
            case ArtGenRowStatus::Kept:
                row.statusLine = Format("Kept: {} is final", file);
                break;
            case ArtGenRowStatus::Replaced:
                row.statusLine = Format("Not in the game: set '{}' accepted the name '{}' later and owns {}", newerName, name, file);
                break;
        }
    }

    void ArtGenSetPanel::DrawReviewRow(ArtGenSession& session, const ArtGenSet& set, size_t index, int32_t columns, const ArtGenSetPanelSound& sound) {
        const auto& palette = GetImGuiPalette();
        const ArtGenSetJob& job = set.jobs[index];
        _rows[index].seenFrame = _frame;
        SyncRow(session, set, index);

        ImGui::TextColored(ColorOfState(palette, job.state), "%s", IconOfState(job.state));
        ImGui::SameLine();
        ImGui::TextUnformatted(job.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", job.subject.c_str());
        const ArtGenSet* newer = session.FindNewerOwner(set, job);
        const ArtGenRowStatus status = StatusOf(job, newer != nullptr);
        // A replaced row's variants are alternatives to nothing: the game holds another set's picture under its name
        const bool live = status == ArtGenRowStatus::Live;
        if (status == ArtGenRowStatus::Replaced) {
            char badge[kLabelSize];
            std::snprintf(badge, sizeof(badge), ICON_LC_REPLACE " replaced by %s", newer->name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(palette.warning, "%s", badge);
            ItemTooltip(kReplacedExplanation);
        } else if (live) {
            char badge[kLabelSize];
            const int32_t alternatives = AlternativesOf(job);
            std::snprintf(badge, sizeof(badge), ICON_LC_CHECK_CHECK " live, %d alternative%s", static_cast<int>(alternatives), alternatives == 1 ? "" : "s");
            ImGui::SameLine();
            ImGui::TextColored(palette.success, "%s", badge);
        } else if (status == ArtGenRowStatus::Kept) {
            ImGui::SameLine();
            ImGui::TextColored(palette.accent, ICON_LC_LOCK " kept");
        }
        RefreshStatusLine(_rows[index], set, job, status, newer);
        ImGui::TextDisabled("%s", _rows[index].statusLine.c_str());
        ItemTooltip(status == ArtGenRowStatus::Replaced ? kReplacedExplanation : _rows[index].statusLine.c_str());

        ImGui::AlignTextToFramePadding();
        switch (job.state) {
            case ArtGenSetJobState::Failed:
            case ArtGenSetJobState::Cancelled:
                ImGui::TextColored(job.state == ArtGenSetJobState::Failed ? palette.error : palette.textDisabled, "%s", job.error.c_str());
                ImGui::SameLine();
                if (IconTextButton(ICON_LC_ROTATE_CCW, "Retry", "Put this job back in the queue; its earlier images are dropped")) {
                    session.RetryJobs(StringView(set.name), StringView(job.name));
                }
                break;
            case ArtGenSetJobState::Pending:
                ImGui::TextDisabled("Waiting in the queue");
                break;
            case ArtGenSetJobState::Running:
                ImGui::TextDisabled("Making the images...");
                break;
            default:
                // A kept or replaced row's status line above already says what its variants are
                if (job.variants.empty()) {
                    ImGui::TextDisabled("No images");
                } else if (live) {
                    ImGui::TextDisabled("Click a variant to show it in the game");
                } else if (!job.accepted) {
                    ImGui::TextDisabled("%s", job.pick ? "Click another variant to change the pick" : "Click a variant to pick it");
                }
                if (live) {
                    ImGui::SameLine();
                    if (IconTextButton(ICON_LC_LOCK, "Keep", kKeepTip)) {
                        const auto kept = session.KeepSet(StringView(set.name), StringView(job.name));
                        _acceptFailed = !kept.has_value();
                        _acceptMessage = kept ? Format("Kept '{}'", job.name) : kept.error().message;
                    }
                }
                break;
        }

        for (size_t v = 0; v < job.variants.size(); ++v) {
            if (v % static_cast<size_t>(columns) != 0) {
                ImGui::SameLine();
            }
            DrawVariantCell(session, set, index, v, sound);
        }
    }

    void ArtGenSetPanel::DrawVariantCell(ArtGenSession& session, const ArtGenSet& set, size_t jobIndex, size_t variantIndex, const ArtGenSetPanelSound& sound) {
        const auto& palette = GetImGuiPalette();
        const ArtGenSetJob& job = set.jobs[jobIndex];
        const ArtGenSetVariant& variant = job.variants[variantIndex];
        const Thumb* thumb = FindThumb(_rows[jobIndex], variant.seed);
        const bool audio = StringView(variant.kind) == kKindAudio;
        const bool missing = thumb != nullptr && thumb->missing;

        ImGui::PushID(static_cast<int>(variantIndex));
        ImGui::BeginGroup();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        if (audio) {
            // The play button sits over the tile
            ImGui::SetNextItemAllowOverlap();
        }
        // A kept row is final and a replaced one has nothing to switch: its cells only show the variants
        const bool kept = job.accepted && job.accepted->kept;
        const bool replaced = session.FindNewerOwner(set, job) != nullptr;
        ImGui::BeginDisabled(kept || replaced);
        if (ImGui::InvisibleButton("##pick", ImVec2(kReviewThumbnailSize, kReviewThumbnailSize))) {
            ArtGenSwitch write;
            if (session.PickVariant(StringView(set.name), StringView(job.name), variant.seed, &write) == ArtGenSetEdit::WriteFailed) {
                _acceptFailed = true;
                _acceptMessage = Format("Could not show seed {} in the game: {}", variant.seed, write.error);
            } else if (IsLive(job)) {
                _acceptFailed = false;
                _acceptMessage.clear();
            }
        }
        ImGui::EndDisabled();
        const char* tip = IsLive(job) ? "Show this variant in the game" : "Pick this variant";
        if (replaced) {
            tip = "A newer set holds this name in the game";
        } else if (kept) {
            tip = "Kept: this row is final";
        }
        ItemTooltip(missing ? "The file is gone" : tip);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 corner(origin.x + kReviewThumbnailSize, origin.y + kReviewThumbnailSize);
        const ITexture* texture = thumb != nullptr ? thumb->texture.Get() : nullptr;
        if (texture != nullptr && texture->GetState() == ResourceState::Loaded) {
            const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
            DrawArtGenChecker(drawList, origin, kReviewThumbnailSize, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg));
            drawList->AddImage(id, origin, corner);
        } else {
            drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
            const char* icon = missing ? ICON_LC_IMAGE_OFF : (audio ? ICON_LC_AUDIO_LINES : ICON_LC_LOADER);
            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            const ImVec2 at(origin.x + (kReviewThumbnailSize - iconSize.x) * 0.5f, origin.y + (kReviewThumbnailSize - iconSize.y) * 0.5f);
            drawList->AddText(at, ImGui::GetColorU32(missing ? palette.warning : palette.textDisabled), icon);
        }
        if (job.pick && *job.pick == variant.seed) {
            drawList->AddRect(origin, corner, ImGui::GetColorU32(palette.accent), 0.0f, 0, kSelectedBorder);
        }

        if (audio) {
            const bool loaded = thumb != nullptr && thumb->clip && thumb->clip->GetState() == ResourceState::Loaded;
            const bool playing = sound.playing && _playingJob == static_cast<int32_t>(jobIndex) && _playingSeed == variant.seed;
            const float inset = ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + inset, origin.y + inset));
            ImGui::BeginDisabled(!loaded);
            if (IconButton(playing ? ICON_LC_SQUARE : ICON_LC_PLAY, playing ? "Stop" : "Listen (the editor's Ui bus)", "##play")) {
                if (playing) {
                    sound.stop();
                } else {
                    sound.play(*thumb->clip);
                    _playingJob = static_cast<int32_t>(jobIndex);
                    _playingSeed = variant.seed;
                }
            }
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(ImVec2(origin.x, corner.y + ImGui::GetStyle().ItemSpacing.y));
        }
        {
            ScopedMonoFont mono;
            ImGui::TextDisabled("%lld", static_cast<long long>(variant.seed));
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }

    void ArtGenSetPanel::DrawReviewFooter(ArtGenSession& session, const ArtGenSet& set) {
        const auto& palette = GetImGuiPalette();
        const int32_t total = static_cast<int32_t>(set.jobs.size());
        const int32_t picked = PickedCount(set);
        // A row a newer set replaced is neither live nor kept here: the game holds the other set's picture under its name
        int32_t live = 0;
        int32_t kept = 0;
        int32_t replacedRows = 0;
        for (const ArtGenSetJob& job : set.jobs) {
            const ArtGenRowStatus status = StatusOf(job, session.FindNewerOwner(set, job) != nullptr);
            live += status == ArtGenRowStatus::Live ? 1 : 0;
            kept += status == ArtGenRowStatus::Kept ? 1 : 0;
            replacedRows += status == ArtGenRowStatus::Replaced ? 1 : 0;
        }
        // What Keep all can still keep
        const int32_t keepable = live;
        // What Accept all needs (ArtGenSession::AcceptSet checks it again): the set finished, every job done, and for sounds a pick on every row
        // (an image row without a pick takes its first variant)
        const char* blocker = nullptr;
        if (IsActive(set)) {
            blocker = "The set is still running";
        } else if (CountJobs(set, ArtGenSetJobState::Done) != total) {
            blocker = "A job failed or was cancelled: retry it first";
        } else if (set.kind == kKindAudio && picked != total) {
            blocker = "Listen and pick a variant of every row first";
        } else if (kept + replacedRows == total) {
            blocker = "Every row is kept or replaced by a newer set";
        }

        ImGui::AlignTextToFramePadding();
        {
            ScopedMonoFont mono;
            if (live + kept + replacedRows > 0) {
                ImGui::Text("%d live, %d kept, %d replaced", static_cast<int>(live), static_cast<int>(kept), static_cast<int>(replacedRows));
            } else {
                ImGui::Text("%d / %d picked", static_cast<int>(picked), static_cast<int>(total));
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(blocker != nullptr);
        if (IconTextButton(ICON_LC_CHECK_CHECK, "Accept all", blocker != nullptr ? blocker : kAcceptAllTip)) {
            auto result = session.AcceptSet(StringView(set.name));
            _acceptFailed = !result.has_value();
            if (result) {
                const Json* accepted = FindJsonMember(*result, "accepted");
                int32_t written = 0;
                int32_t replaced = 0;
                if (accepted != nullptr && accepted->is_array()) {
                    for (const Json& row : *accepted) {
                        written += FindJsonMember(row, "as") != nullptr ? 1 : 0;
                        replaced += FindJsonMember(row, "replacedBy") != nullptr ? 1 : 0;
                    }
                }
                _acceptMessage = replaced > 0 ? Format("Accepted {} row(s) into the game; {} left as they are: a newer set holds their names", written, replaced)
                                              : Format("Accepted {} row(s) into the game", written);
            } else {
                _acceptMessage = result.error().message;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(keepable == 0);
        if (IconTextButton(ICON_LC_LOCK, "Keep all", kKeepAllTip)) {
            auto result = session.KeepSet(StringView(set.name));
            _acceptFailed = !result.has_value();
            _acceptMessage = result ? Format("Kept {} row(s)", keepable) : result.error().message;
        }
        ImGui::EndDisabled();
        if (set.kind != kKindAudio) {
            if (set.processing) {
                const ArtGenImageFrame& frame = set.processing->frame;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Frame %d x %d from %d x %d, fixed at the first Accept all: every row of the set is cut and scaled the same", static_cast<int>(frame.outWidth),
                                    static_cast<int>(frame.outHeight), static_cast<int>(frame.w), static_cast<int>(frame.h));
                ImGui::PopTextWrapPos();
            } else if (IsLegacyAccepted(set)) {
                ImGui::TextDisabled("Accepted before trimming: the files are full frames");
            }
        }
        if (!_acceptMessage.empty()) {
            ImGui::TextColored(_acceptFailed ? palette.error : palette.success, "%s", _acceptMessage.c_str());
        }
    }

}  // namespace BECore
