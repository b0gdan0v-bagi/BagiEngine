#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    class ArtGenPresetForm;
    class ArtGenSession;
    struct ArtGenSet;

    /// A checkerboard over a square, so an image with a transparent background shows as transparent
    void DrawArtGenChecker(ImDrawList* drawList, ImVec2 origin, float size, ImU32 light, ImU32 dark);

    /// What the Set tab takes from the Generate form: the preset it queues with (by name when saved and unchanged, else as a draft, as Generate
    /// decides), and the count and seed of the form
    struct ArtGenSetPanelForm {
        const ArtGenPresetForm& preset;
        int32_t count = 0;
        Optional<int64_t> seed;
    };

    /**
     * @brief The Set tab of the Generate panel: the subjects field, Queue, and the sets with their progress.
     *
     * ImGui only: every decision (the lines, the queue, the cancel, the retry) is the session's and is the same call the control API makes
     * (src/Widgets/ArtGen/Sets/CLAUDE.md). Owned by `ArtGenWidget`, which draws the tab bar.
     */
    class ArtGenSetPanel {
    public:
        /// The Set tab
        void DrawQueue(ArtGenSession& session, const ArtGenSetPanelForm& form);

    private:
        void DrawSetEntry(ArtGenSession& session, const ArtGenSet& set);
        void DrawJobRows(ArtGenSession& session, const ArtGenSet& set);

        char _subjects[4096] = {};
        char _setName[65] = {};
        String _error;
        String _queued;
    };

}  // namespace BECore
