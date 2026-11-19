#include "ArtGenSetPanel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <Widgets/ArtGen/Reference/ArtGenReferencePanel.h>
#include <Widgets/ArtGen/Sets/ArtGenSetStore.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <cstdio>

namespace BECore {

    namespace {

        // The widths of the fields of the Set tab, in ems (the font size), so they follow the editor's font scale
        constexpr float kSubjectWidthInEm = 24.0f;
        constexpr float kNameWidthInEm = 11.0f;
        constexpr float kSetNameWidthInEm = 16.0f;
        constexpr float kCountWidthInEm = 9.0f;
        constexpr float kSeedWidthInEm = 12.0f;
        // A field is never narrower than this, in pixels, however small the panel
        constexpr float kMinFieldWidth = 1.0f;
        constexpr const char* kQueueTip = "Make every subject with the preset above and this set's count and seed, one job after another; a set draws one seed for all its jobs when random";
        constexpr const char* kSingleRunNote = "A single run is in work on the Generate tab; the set starts after it";
        constexpr const char* kSetDraftNote =
            "A set is made into artgen/out/<set>/ (drafts, git ignores them). Accept all, and a click on a variant of a live row, write into the game: assets/artgen/ and "
            "config/TextureLibrary.xml, which are in git.";
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
        // The list of sets shows this many lines at most, then scrolls
        constexpr size_t kSetListLines = 5;
        // The rows of the selected set are never squeezed below this many text lines by a tall form above them: the tab scrolls as a whole then
        constexpr float kRowsMinLines = 12.0f;
        // The frame line of an image set under the footer's buttons wraps to about this many lines
        constexpr float kFrameLineCount = 2.0f;
        // Squares behind a thumbnail, so a transparent background (a cut-out sprite) shows as transparent
        constexpr float kCheckerCell = 10.0f;
        constexpr size_t kLabelSize = 160;
        constexpr size_t kTipSize = 256;
        // A row's thumbnail is smaller than the Generate grid's, so a row of four fits the panel
        constexpr float kThumbnailSize = 112.0f;
        // The verdict's mark: a square of this many ems in the thumbnail's top-right corner, over a backing of the window colour
        constexpr float kVerdictMarkEm = 1.5f;
        constexpr float kVerdictMarkBackingAlpha = 0.7f;
        constexpr float kVerdictMarkRounding = 3.0f;

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

        /// `Active|Complete  d/t done[, n failed][, n cancelled]`, the state of a set in a line
        void FormatSetSummary(const ArtGenSet& set, char* out, size_t size) {
            const int32_t failed = CountJobs(set, ArtGenSetJobState::Failed);
            const int32_t cancelled = CountJobs(set, ArtGenSetJobState::Cancelled);
            int length =
                std::snprintf(out, size, "%s  %d/%d done", IsActive(set) ? "Active" : "Complete", static_cast<int>(CountJobs(set, ArtGenSetJobState::Done)), static_cast<int>(set.jobs.size()));
            if (failed > 0 && length > 0 && static_cast<size_t>(length) < size) {
                length += std::snprintf(out + length, size - static_cast<size_t>(length), ", %d failed", static_cast<int>(failed));
            }
            if (cancelled > 0 && length > 0 && static_cast<size_t>(length) < size) {
                std::snprintf(out + length, size - static_cast<size_t>(length), ", %d cancelled", static_cast<int>(cancelled));
            }
        }

        /// The line a successful More variants leaves: what was queued, and the rows left out with the reason
        String DescribeMore(const Json& reply) {
            const Optional<int64_t> count = GetJsonInt(reply, "count");
            const Optional<int64_t> seed = GetJsonInt(reply, "seed");
            String message = Format("Queued {} more per row on seed {}", count ? *count : 0, seed ? *seed : 0);
            const Json* skipped = FindJsonMember(reply, "skipped");
            if (skipped == nullptr || !skipped->is_array() || skipped->empty()) {
                return message;
            }
            String left;
            for (const Json& row : *skipped) {
                const Optional<String> job = GetJsonString(row, "job");
                const Optional<String> reason = GetJsonString(row, "reason");
                if (job && reason) {
                    left.append(left.empty() ? "" : ", ").append(Format("{} ({})", *job, *reason));
                }
            }
            return Format("{}; left out: {}", message, left);
        }

        /// The tooltip of an enabled More variants: how many rows run, and that the others are left out
        void FormatMoreTip(const ArtGenSet& set, int32_t count, char* out, size_t size) {
            int32_t running = 0;
            for (const ArtGenSetJob& job : set.jobs) {
                running += ArtGenMoreSkipReason(job, count) == nullptr ? 1 : 0;
            }
            std::snprintf(
                out, size,
                "Make %d more variant(s) of each of %d row(s) on a fresh seed and add them to the ones they have. Kept, failed and cancelled rows are left out; the pick and the game stay as they are",
                static_cast<int>(count), static_cast<int>(running));
        }

        /// The width of a field that is followed by @p label on the same line, in the room that is left
        float LabelledFieldWidth(float widthInEm, const char* label) {
            const float room = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemInnerSpacing.x;
            return ArtGenFieldWidth(widthInEm, ImGui::GetFontSize(), room);
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

    const char* ArtGenVerdictIcon(ArtGenVerdictKind kind) {
        switch (kind) {
            case ArtGenVerdictKind::Passed:
                return ICON_LC_CIRCLE_CHECK;
            case ArtGenVerdictKind::Failed:
                return ICON_LC_TRIANGLE_ALERT;
            case ArtGenVerdictKind::Unreadable:
                return ICON_LC_CIRCLE_HELP;
            default:
                return nullptr;
        }
    }

    void DrawArtGenVerdictMark(ImDrawList* drawList, ImVec2 origin, float size, const ArtGenVerdict& verdict) {
        const char* icon = ArtGenVerdictIcon(verdict.kind);
        if (icon == nullptr) {
            return;
        }
        const auto& palette = GetImGuiPalette();
        const ImVec4& color = verdict.kind == ArtGenVerdictKind::Passed ? palette.success : (verdict.kind == ArtGenVerdictKind::Failed ? palette.error : palette.textDisabled);
        const float side = ImGui::GetFontSize() * kVerdictMarkEm;
        const ImVec2 min(origin.x + size - side, origin.y);
        const ImVec2 max(origin.x + size, origin.y + side);
        const ImVec4 backing(palette.windowBg.x, palette.windowBg.y, palette.windowBg.z, kVerdictMarkBackingAlpha);
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(backing), kVerdictMarkRounding);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        drawList->AddText(ImVec2(min.x + (side - iconSize.x) * 0.5f, min.y + (side - iconSize.y) * 0.5f), ImGui::GetColorU32(color), icon);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max)) {
            ImGui::SetTooltip("%s", verdict.tip.c_str());
        }
    }

    float ArtGenFieldWidth(float widthInEm, float fontSize, float available) {
        return std::max(std::min(widthInEm * fontSize, available), kMinFieldWidth);
    }

    ArtGenRowWidths ArtGenRowFieldWidths(float available, float fontSize) {
        const float subject = kSubjectWidthInEm * fontSize;
        const float name = kNameWidthInEm * fontSize;
        const float shrink = std::min(1.0f, std::max(available, kMinFieldWidth) / (subject + name));
        return ArtGenRowWidths{std::max(subject * shrink, kMinFieldWidth), std::max(name * shrink, kMinFieldWidth)};
    }

    Optional<ArtGenReference> ArtGenQueueReference(const Optional<ArtGenReference>& slot, bool makesImages) {
        return makesImages ? slot : Optional<ArtGenReference>();
    }

    void FormatArtGenReferenceSummary(const ArtGenReference& reference, char* out, size_t size) {
        const StringView file = ArtGenReferenceFileName(StringView(reference.image.data(), reference.image.size()));
        const double weight = reference.weight ? *reference.weight : ArtGenReferenceDefaultWeight(StringView(reference.mode.data(), reference.mode.size()));
        std::snprintf(out, size, "%.*s (%s, weight %.2f)", static_cast<int>(file.size()), file.data(), reference.mode.c_str(), weight);
    }

    void FormatArtGenQueueReference(const Optional<ArtGenReference>& slot, bool makesImages, char* out, size_t size) {
        if (!makesImages) {
            std::snprintf(out, size, "No reference: a sound takes none.");
        } else if (!slot) {
            std::snprintf(out, size, "No reference: the preset's own reference applies, if it has one. Use as reference fills the slot.");
        } else {
            char summary[kLabelSize];
            FormatArtGenReferenceSummary(*slot, summary, sizeof(summary));
            std::snprintf(out, size, "Reference of every job: %s", summary);
        }
    }

    void ArtGenSetPanel::DrawSubjectRows() {
        if (_subjectRows.empty()) {
            _subjectRows.emplace_back();
        }
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonWidth = ImGui::GetFrameHeight();
        const float inputs = ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x * 2.0f;
        const ArtGenRowWidths widths = ArtGenRowFieldWidths(inputs, ImGui::GetFontSize());
        const bool onlyRow = _subjectRows.size() == 1;
        size_t remove = _subjectRows.size();
        for (size_t i = 0; i < _subjectRows.size(); ++i) {
            ArtGenSubjectRow& row = _subjectRows[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(widths.subject);
            if (ImGui::InputTextWithHint("##subject", "Subject", row.subject, sizeof(row.subject))) {
                ArtGenRowSubjectEdited(row);
            }
            ItemTooltip("What to make. A row with an empty subject is skipped");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(widths.name);
            if (ImGui::InputTextWithHint("##name", "Name", row.name, sizeof(row.name))) {
                ArtGenRowNameEdited(row);
            }
            ItemTooltip("The file and sprite name of the job: 1 to 64 of A-Z a-z 0-9 _ -. Follows the subject until you type here; empty it to follow again");
            ImGui::SameLine();
            if (IconButton(onlyRow ? ICON_LC_ERASER : ICON_LC_TRASH_2, onlyRow ? "Clear the row" : "Remove the row", "##remove")) {
                if (onlyRow) {
                    row = ArtGenSubjectRow();
                } else {
                    remove = i;
                }
            }
            ImGui::PopID();
        }
        if (remove < _subjectRows.size()) {
            _subjectRows.erase(_subjectRows.begin() + static_cast<ptrdiff_t>(remove));
        }
        ImGui::BeginDisabled(_subjectRows.size() >= static_cast<size_t>(kMaxSetJobs));
        if (IconTextButton(ICON_LC_PLUS, "Add", "Add a row for one more subject")) {
            _subjectRows.emplace_back();
        }
        ImGui::EndDisabled();
    }

    void ArtGenSetPanel::DrawQueueForm(ArtGenSession& session, const ArtGenSetPanelForm& form) {
        const auto& palette = GetImGuiPalette();
        DrawSubjectRows();
        char referenceLine[kLabelSize];
        FormatArtGenQueueReference(session.GetReferenceSlot(), form.makesImages, referenceLine, sizeof(referenceLine));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", referenceLine);
        ImGui::PopTextWrapPos();
        ImGui::SetNextItemWidth(LabelledFieldWidth(kSetNameWidthInEm, "Set name"));
        ImGui::InputTextWithHint("Set name", "<preset>-<date> when empty", _setName, sizeof(_setName));
        ItemTooltip("The folder under artgen/out/ that keeps the set; 1 to 64 of A-Z a-z 0-9 _ -");
        const bool nameValid = _setName[0] == '\0' || IsValidSpriteName(StringView(_setName));
        if (!nameValid) {
            ImGui::TextColored(palette.warning, "1 to 64 of A-Z a-z 0-9 _ -");
        }
        {
            ScopedMonoFont mono;
            ImGui::SetNextItemWidth(LabelledFieldWidth(kCountWidthInEm, "Count"));
            ImGui::InputInt("Count", &_count);
        }
        _count = std::clamp(_count, 1, kArtGenSetMaxCount);
        ItemTooltip("Variants made for every job of this set");
        ImGui::Checkbox("Random seed", &_randomSeed);
        ItemTooltip("Off: the same first seed for every job of the set, the one below");
        if (!_randomSeed) {
            ImGui::SameLine();
            ScopedMonoFont mono;
            ImGui::SetNextItemWidth(ArtGenFieldWidth(kSeedWidthInEm, ImGui::GetFontSize(), ImGui::GetContentRegionAvail().x));
            ImGui::InputScalar("##setSeed", ImGuiDataType_S64, &_seed);
            _seed = std::max<int64_t>(_seed, 0);
        }

        // A queued set clears the rows, so the button is off until the next subject is typed: it says so
        const char* blocker = ArtGenQueueBlocker(!form.preset.IsEmpty(), _subjectRows, nameValid);
        ImGui::BeginDisabled(blocker != nullptr);
        if (IconTextButton(ICON_LC_LIST_PLUS, "Queue", blocker != nullptr ? blocker : kQueueTip)) {
            _error.clear();
            _queued.clear();
            auto subjects = ArtGenSubjectsFromRows(_subjectRows);
            if (subjects) {
                ArtGenSetRequest request;
                request.subjects = std::move(*subjects);
                request.count = _count;
                request.seed = _randomSeed ? Optional<int64_t>() : Optional<int64_t>(_seed);
                request.name = String(_setName);
                request.reference = ArtGenQueueReference(session.GetReferenceSlot(), form.makesImages);
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
                    _selectedSet = *queued;
                    _subjectRows.clear();
                    _setName[0] = '\0';
                } else {
                    _error = queued.error();
                }
            } else {
                _error = subjects.error();
            }
        }
        ImGui::EndDisabled();
        if (blocker != nullptr) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", blocker);
        }
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
    }

    void ArtGenSetPanel::ResolveSelection(const ArtGenSession& session) {
        const ArtGenSet* selected = _selectedSet.empty() ? nullptr : session.FindSet(StringView(_selectedSet));
        if (selected == nullptr) {
            selected = &session.GetSets().back();
        }
        if (StringView(_selectedSet) != StringView(selected->name)) {
            _selectedSet = selected->name;
        }
    }

    void ArtGenSetPanel::DrawSetList(const ArtGenSession& session) {
        const auto& palette = GetImGuiPalette();
        const ImGuiStyle& style = ImGui::GetStyle();
        const auto& sets = session.GetSets();
        const float lines = static_cast<float>(std::min(sets.size(), kSetListLines));
        ImGui::BeginChild("##setList", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * lines + style.WindowPadding.y * 2.0f), ImGuiChildFlags_Borders);
        for (size_t i = sets.size(); i-- > 0;) {
            const ArtGenSet& set = sets[i];
            ImGui::PushID(set.name.c_str());
            if (ImGui::Selectable("##select", StringView(set.name) == StringView(_selectedSet))) {
                _selectedSet = set.name;
            }
            // The text is drawn over the item so its id does not change with the progress it shows
            const ImVec2 min = ImGui::GetItemRectMin();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(min, ImGui::GetItemRectMax(), true);
            ImVec2 at(min.x + style.ItemInnerSpacing.x, min.y);
            const auto put = [&](const char* text, ImU32 color) {
                drawList->AddText(at, color, text);
                at.x += ImGui::CalcTextSize(text).x + style.ItemSpacing.x;
            };
            char name[kLabelSize];
            std::snprintf(name, sizeof(name), "%s %s", set.kind == kKindAudio ? ICON_LC_AUDIO_LINES : ICON_LC_IMAGE, set.name.c_str());
            put(name, ImGui::GetColorU32(ImGuiCol_Text));
            put(set.preset.c_str(), ImGui::GetColorU32(palette.textDisabled));
            char summary[kLabelSize];
            FormatSetSummary(set, summary, sizeof(summary));
            const bool failed = CountJobs(set, ArtGenSetJobState::Failed) > 0;
            put(summary, ImGui::GetColorU32(failed ? palette.error : (IsActive(set) ? palette.accent : palette.success)));
            drawList->PopClipRect();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void ArtGenSetPanel::DrawSetHeader(ArtGenSession& session, const ArtGenSet& set) {
        const bool active = IsActive(set);
        const int32_t failedOrCancelled = CountJobs(set, ArtGenSetJobState::Failed) + CountJobs(set, ArtGenSetJobState::Cancelled);

        ImGui::Text("%s %s", set.kind == kKindAudio ? ICON_LC_AUDIO_LINES : ICON_LC_IMAGE, set.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", set.preset.c_str());
        if (set.reference) {
            // The file name is the label, the mode and weight the tooltip: a set is judged by what it was made from
            char summary[kLabelSize];
            FormatArtGenReferenceSummary(*set.reference, summary, sizeof(summary));
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", ICON_LC_IMAGE_PLUS, summary);
            ItemTooltip(set.reference->image.c_str());
        }

        const ArtGenRun& run = session.GetRun();
        if (StringView(run.setName) == StringView(set.name) && (run.state == ArtGenRunState::Queued || run.state == ArtGenRunState::Running)) {
            DrawRunProgress(run);
        }

        ImGui::BeginDisabled(!active);
        if (IconTextButton(ICON_LC_X, "Cancel set", active ? "Cancel every waiting job and the one in work; the images already made stay" : "Nothing is waiting or in work in this set")) {
            session.CancelSet(StringView(set.name));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(failedOrCancelled == 0);
        if (IconTextButton(ICON_LC_ROTATE_CCW, "Retry",
                           failedOrCancelled > 0 ? "Put the failed and cancelled jobs back in the queue; their earlier images are dropped" : "No job of this set failed or was cancelled")) {
            session.RetryJobs(StringView(set.name));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        {
            ScopedMonoFont mono;
            ImGui::SetNextItemWidth(ArtGenFieldWidth(kCountWidthInEm, ImGui::GetFontSize(), ImGui::GetContentRegionAvail().x));
            ImGui::InputInt("##moreCount", &_moreCount);
        }
        _moreCount = std::clamp(_moreCount, 1, kArtGenSetMaxCount);
        ItemTooltip("How many more variants each row gets");
        ImGui::SameLine();
        const char* blocker = ArtGenMoreBlocker(set, _moreCount);
        char tip[kTipSize];
        if (blocker == nullptr) {
            FormatMoreTip(set, _moreCount, tip, sizeof(tip));
        }
        ImGui::BeginDisabled(blocker != nullptr);
        if (IconTextButton(ICON_LC_COPY_PLUS, "More variants", blocker != nullptr ? blocker : tip)) {
            auto reply = session.MoreVariants(StringView(set.name), _moreCount);
            _acceptFailed = !reply.has_value();
            _acceptMessage = reply ? DescribeMore(*reply) : reply.error().message;
        }
        ImGui::EndDisabled();
    }

    ArtGenSetPanel::~ArtGenSetPanel() {
        DropThumbnails();
    }

    const ArtGenSetPanel::Thumb* ArtGenSetPanel::FindThumb(const RowThumbs& row, int64_t seed) {
        const auto it = std::find_if(row.thumbs.begin(), row.thumbs.end(), [&](const Thumb& thumb) { return thumb.seed == seed; });
        return it == row.thumbs.end() ? nullptr : &*it;
    }

    void ArtGenSetPanel::EvictThumb(Thumb& thumb) {
        // The pictures of a row are seen a few times: keeping them in the cache would hold every texture of a set until the editor closes
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

    const ArtGenVerdict* ArtGenSetPanel::VerdictOf(size_t jobIndex, int64_t seed) const {
        if (jobIndex >= _rows.size()) {
            return nullptr;
        }
        const Thumb* thumb = FindThumb(_rows[jobIndex], seed);
        return thumb != nullptr ? &thumb->verdict : nullptr;
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
                    // A name AbsoluteInSet refuses (`..`) gives an empty path, which reads as no verdict
                    if (!variant.provenance.empty()) {
                        thumb.verdict = ReadArtGenVerdict(AbsoluteInSet(root, StringView(set.name), StringView(variant.provenance)));
                    }
                }
            }
            row.thumbs.push_back(std::move(thumb));
        }
    }

    void ArtGenSetPanel::DrawSetTab(ArtGenSession& session, const ArtGenSetPanelForm& form, const ArtGenSetPanelSound& sound) {
        DrawQueueForm(session, form);
        if (session.GetSets().empty()) {
            if (sound.playing) {
                sound.stop();
            }
            DropThumbnails();
            _selectedSet.clear();
            EmptyState(ICON_LC_LIST_CHECKS, "No sets yet: fill in the subjects above and press Queue");
            return;
        }
        ResolveSelection(session);
        DrawSetList(session);
        // A click in the list moved the selection
        ResolveSelection(session);
        const ArtGenSet* set = session.FindSet(StringView(_selectedSet));
        DrawSetHeader(session, *set);
        DrawSetRows(session, *set, sound);
        DrawSetFooter(session, *set);
    }

    void ArtGenSetPanel::DrawSetRows(ArtGenSession& session, const ArtGenSet& set, const ArtGenSetPanelSound& sound) {
        if (StringView(_loadedSet) != StringView(set.name)) {
            if (sound.playing) {
                sound.stop();
            }
            DropThumbnails();
            _loadedSet = set.name;
            _acceptMessage.clear();
        }
        _rows.resize(set.jobs.size());
        ++_frame;
        // The one place a frame asks who owns each row's name: the rows, their cells and the footer use these answers
        _newerOwners.resize(set.jobs.size());
        _liveRows = 0;
        _keptRows = 0;
        _replacedRows = 0;
        for (size_t i = 0; i < set.jobs.size(); ++i) {
            const ArtGenSetJob& job = set.jobs[i];
            _newerOwners[i] = session.FindNewerOwner(set, job);
            // A row a newer set replaced is neither live nor kept: the game holds the other set's picture under its name
            const ArtGenRowStatus status = StatusOf(job, _newerOwners[i] != nullptr);
            _liveRows += status == ArtGenRowStatus::Live ? 1 : 0;
            _keptRows += status == ArtGenRowStatus::Kept ? 1 : 0;
            _replacedRows += status == ArtGenRowStatus::Replaced ? 1 : 0;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const bool frameLine = set.kind != kKindAudio && (set.processing || IsLegacyAccepted(set));
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + (_acceptMessage.empty() ? 0.0f : lineHeight) + (frameLine ? lineHeight * kFrameLineCount : 0.0f);
        const float rowsHeight = std::max(ImGui::GetContentRegionAvail().y - footerHeight, lineHeight * kRowsMinLines);
        ImGui::BeginChild("##setRows", ImVec2(0.0f, rowsHeight));
        // Every row has the height of a full one, so the clipper knows where row n is without drawing the rows before it
        const float inner = ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2.0f;
        const int32_t columns = std::max(1, static_cast<int32_t>((inner + style.ItemSpacing.x) / (kThumbnailSize + style.ItemSpacing.x)));
        // The most variants any row has, so the rows stay equal after More variants; a Done row's note takes a line
        size_t mostVariants = 1;
        bool notes = false;
        for (const ArtGenSetJob& job : set.jobs) {
            mostVariants = std::max(mostVariants, job.variants.size());
            notes = notes || (job.state == ArtGenSetJobState::Done && !job.error.empty());
        }
        const int32_t thumbRows = (static_cast<int32_t>(mostVariants) + columns - 1) / columns;
        const float cellHeight = kThumbnailSize + style.ItemSpacing.y + ImGui::GetTextLineHeightWithSpacing();
        // The header line (frame height: it can hold the cancel button) and the hint line with its buttons, the status line and the note, then the thumbnails
        const float rowHeight = style.WindowPadding.y * 2.0f + ImGui::GetFrameHeightWithSpacing() * 2.0f + lineHeight * (notes ? 2.0f : 1.0f) + static_cast<float>(thumbRows) * cellHeight;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(set.jobs.size()), rowHeight + style.ItemSpacing.y);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImGui::PushID(i);
                ImGui::BeginChild("##row", ImVec2(0.0f, rowHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                DrawJobRow(session, set, static_cast<size_t>(i), columns, sound);
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

    void ArtGenSetPanel::DrawJobRow(ArtGenSession& session, const ArtGenSet& set, size_t index, int32_t columns, const ArtGenSetPanelSound& sound) {
        const auto& palette = GetImGuiPalette();
        const ArtGenSetJob& job = set.jobs[index];
        _rows[index].seenFrame = _frame;
        SyncRow(session, set, index);

        ImGui::AlignTextToFramePadding();
        if (job.state == ArtGenSetJobState::Pending || job.state == ArtGenSetJobState::Running) {
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
        const ArtGenSet* newer = _newerOwners[index];
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
                    const char* keepBlocker = ArtGenKeepBlocker(set);
                    ImGui::BeginDisabled(keepBlocker != nullptr);
                    if (IconTextButton(ICON_LC_LOCK, "Keep", keepBlocker != nullptr ? keepBlocker : kKeepTip)) {
                        const auto kept = session.KeepSet(StringView(set.name), StringView(job.name));
                        _acceptFailed = !kept.has_value();
                        _acceptMessage = kept ? Format("Kept '{}'", job.name) : kept.error().message;
                    }
                    ImGui::EndDisabled();
                }
                // The sprite in the game under the row's name, not the draft: a replaced row's name belongs to another set
                if (set.kind != kKindAudio && job.accepted && status != ArtGenRowStatus::Replaced) {
                    ImGui::SameLine();
                    if (IconButton(ICON_LC_IMAGE_PLUS, kArtGenUseAsReferenceTip, "##referenceSprite")) {
                        session.SetReferenceSlot(ArtGenReferenceOfSprite(StringView(job.accepted->as)));
                    }
                }
                // The note of a more-run that did not finish: the row stays Done with the variants it had
                if (!job.error.empty()) {
                    ImGui::TextDisabled("%s", job.error.c_str());
                }
                break;
        }

        for (size_t v = 0; v < job.variants.size(); ++v) {
            if (v % static_cast<size_t>(columns) != 0) {
                ImGui::SameLine();
            }
            DrawVariantCell(session, set, index, v, newer != nullptr, sound);
        }
    }

    void ArtGenSetPanel::DrawVariantCell(ArtGenSession& session, const ArtGenSet& set, size_t jobIndex, size_t variantIndex, bool replaced, const ArtGenSetPanelSound& sound) {
        const auto& palette = GetImGuiPalette();
        const ArtGenSetJob& job = set.jobs[jobIndex];
        const ArtGenSetVariant& variant = job.variants[variantIndex];
        const Thumb* thumb = FindThumb(_rows[jobIndex], variant.seed);
        const bool audio = StringView(variant.kind) == kKindAudio;
        const bool missing = thumb != nullptr && thumb->missing;

        ImGui::PushID(static_cast<int>(variantIndex));
        ImGui::BeginGroup();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        // A picture that is on disk can be a reference; its button sits over the tile, like the sound's play button
        const bool offerReference = !audio && thumb != nullptr && !thumb->missing && !thumb->path.empty();
        if (audio || offerReference) {
            ImGui::SetNextItemAllowOverlap();
        }
        // A kept row is final and a replaced one has nothing to switch: its cells only show the variants
        const bool kept = job.accepted && job.accepted->kept;
        ImGui::BeginDisabled(kept || replaced);
        if (ImGui::InvisibleButton("##pick", ImVec2(kThumbnailSize, kThumbnailSize))) {
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
        const ImVec2 corner(origin.x + kThumbnailSize, origin.y + kThumbnailSize);
        const ITexture* texture = thumb != nullptr ? thumb->texture.Get() : nullptr;
        if (texture != nullptr && texture->GetState() == ResourceState::Loaded) {
            const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
            DrawArtGenChecker(drawList, origin, kThumbnailSize, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg));
            drawList->AddImage(id, origin, corner);
        } else {
            drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
            const char* icon = missing ? ICON_LC_IMAGE_OFF : (audio ? ICON_LC_AUDIO_LINES : ICON_LC_LOADER);
            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            const ImVec2 at(origin.x + (kThumbnailSize - iconSize.x) * 0.5f, origin.y + (kThumbnailSize - iconSize.y) * 0.5f);
            drawList->AddText(at, ImGui::GetColorU32(missing ? palette.warning : palette.textDisabled), icon);
        }
        if (job.pick && *job.pick == variant.seed) {
            drawList->AddRect(origin, corner, ImGui::GetColorU32(palette.accent), 0.0f, 0, kSelectedBorder);
        }

        if (offerReference && ImGui::IsMouseHoveringRect(origin, corner)) {
            const float inset = ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + inset, origin.y + inset));
            if (IconButton(ICON_LC_IMAGE_PLUS, kArtGenUseAsReferenceTip, "##reference")) {
                if (auto reference = ArtGenReferenceOfVariant(StringView(session.GetGameRoot()), StringView(variant.kind), StringView(thumb->path))) {
                    session.SetReferenceSlot(std::move(*reference));
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x, corner.y + ImGui::GetStyle().ItemSpacing.y));
        }
        if (!audio && thumb != nullptr) {
            DrawArtGenVerdictMark(drawList, origin, kThumbnailSize, thumb->verdict);
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

    void ArtGenSetPanel::DrawSetFooter(ArtGenSession& session, const ArtGenSet& set) {
        const auto& palette = GetImGuiPalette();
        const int32_t total = static_cast<int32_t>(set.jobs.size());
        const int32_t picked = PickedCount(set);
        // The counts DrawSetRows made this frame, which runs before the footer
        const int32_t live = _liveRows;
        const int32_t kept = _keptRows;
        const int32_t replacedRows = _replacedRows;
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
        const char* keepBlocker = ArtGenKeepBlocker(set);
        ImGui::BeginDisabled(keepable == 0 || keepBlocker != nullptr);
        if (IconTextButton(ICON_LC_LOCK, "Keep all", keepBlocker != nullptr ? keepBlocker : kKeepAllTip)) {
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
                ImGui::TextDisabled("Not trimmed: the files are full frames");
            }
        }
        if (!_acceptMessage.empty()) {
            ImGui::TextColored(_acceptFailed ? palette.error : palette.success, "%s", _acceptMessage.c_str());
        }
    }

}  // namespace BECore
