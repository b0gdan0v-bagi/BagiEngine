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
        constexpr float kJobIndent = 16.0f;
        // Squares behind a thumbnail, so a transparent background (a cut-out sprite) shows as transparent
        constexpr float kCheckerCell = 10.0f;
        constexpr size_t kLabelSize = 160;
        // A review thumbnail is smaller than the Generate grid's, so a row of four fits the panel
        constexpr float kReviewThumbnailSize = 112.0f;
        constexpr float kSelectedBorder = 3.0f;
        constexpr StringView kKindAudio = "audio";

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

        const bool canQueue = !form.preset.IsEmpty() && _subjects[0] != '\0' && nameValid;
        ImGui::BeginDisabled(!canQueue);
        if (IconTextButton(ICON_LC_LIST_PLUS, "Queue",
                           "Make every subject with the form's preset, count and seed, one job after another; a set draws one seed for all its jobs when the form's is random")) {
            _error.clear();
            _queued.clear();
            auto subjects = ParseArtGenSubjects(StringView(_subjects));
            if (subjects) {
                ArtGenSetRequest request;
                request.subjects = std::move(*subjects);
                request.count = form.count;
                request.seed = form.seed;
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
        if (!_error.empty()) {
            ImGui::TextColored(palette.error, "%s", _error.c_str());
        }
        if (!_queued.empty()) {
            ImGui::TextColored(palette.success, "%s", _queued.c_str());
        }
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
        ImGui::Text("%s %s", set.kind == "audio" ? ICON_LC_AUDIO_LINES : ICON_LC_IMAGE, set.name.c_str());
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
        if (IconTextButton(ICON_LC_EYE, "Review", "Look at the variants of every job, pick one per job and put the picks into the game")) {
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

        const ImGuiStyle& style = ImGui::GetStyle();
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + (_acceptMessage.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing());
        ImGui::BeginChild("##reviewRows", ImVec2(0.0f, -footerHeight));
        // Every row has the height of a full one, so the clipper knows where row n is without drawing the rows before it
        const float inner = ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2.0f;
        const int32_t columns = std::max(1, static_cast<int32_t>((inner + style.ItemSpacing.x) / (kReviewThumbnailSize + style.ItemSpacing.x)));
        const int32_t thumbRows = (std::max(set->count, 1) + columns - 1) / columns;
        const float cellHeight = kReviewThumbnailSize + style.ItemSpacing.y + ImGui::GetTextLineHeightWithSpacing();
        const float rowHeight = style.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + static_cast<float>(thumbRows) * cellHeight;

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
        if (job.accepted) {
            ImGui::SameLine();
            ImGui::TextColored(palette.success, ICON_LC_CHECK_CHECK " accepted as %s", job.accepted->as.c_str());
        }

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
                ImGui::TextDisabled("%s", job.variants.empty() ? "No images" : (job.pick ? "Click another variant to change the pick" : "Click a variant to pick it"));
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
        if (ImGui::InvisibleButton("##pick", ImVec2(kReviewThumbnailSize, kReviewThumbnailSize))) {
            session.PickVariant(StringView(set.name), StringView(job.name), variant.seed);
        }
        ItemTooltip(missing ? "The file is gone" : "Pick this variant");

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
        // What Accept all needs (ArtGenSession::AcceptSet checks it again): the set finished, every job done and picked
        const char* blocker = nullptr;
        if (IsActive(set)) {
            blocker = "The set is still running";
        } else if (CountJobs(set, ArtGenSetJobState::Done) != total) {
            blocker = "A job failed or was cancelled: retry it first";
        } else if (picked != total) {
            blocker = "Pick a variant of every row first";
        }

        ImGui::AlignTextToFramePadding();
        {
            ScopedMonoFont mono;
            ImGui::Text("%d / %d picked", static_cast<int>(picked), static_cast<int>(total));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(blocker != nullptr);
        if (IconTextButton(ICON_LC_CHECK_CHECK, "Accept all", blocker != nullptr ? blocker : "Put the picked variant of every row into the game under the row's name")) {
            auto result = session.AcceptSet(StringView(set.name));
            _acceptFailed = !result.has_value();
            if (result) {
                const Json* accepted = FindJsonMember(*result, "accepted");
                const size_t rows = accepted != nullptr && accepted->is_array() ? accepted->size() : 0;
                _acceptMessage = Format("Accepted {} row(s) into the game", rows);
            } else {
                _acceptMessage = result.error().message;
            }
        }
        ImGui::EndDisabled();
        if (!_acceptMessage.empty()) {
            ImGui::TextColored(_acceptFailed ? palette.error : palette.success, "%s", _acceptMessage.c_str());
        }
    }

}  // namespace BECore
