#include "ArtGenSetPanel.h"

#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
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

}  // namespace BECore
