#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    class ArtGenPresetForm;
    class ArtGenSession;
    struct ArtGenSet;

    /// The width of the accent frame around the selected variant, in the Generate grid and in the rows of the Set tab
    inline constexpr float kSelectedBorder = 3.0f;

    /// A checkerboard over a square, so an image with a transparent background shows as transparent
    void DrawArtGenChecker(ImDrawList* drawList, ImVec2 origin, float size, ImU32 light, ImU32 dark);

    /// The widths of the Subject and Name fields of a row, in pixels
    struct ArtGenRowWidths {
        float subject = 0.0f;
        float name = 0.0f;
    };

    /// A field of @p widthInEm ems of a font of @p fontSize, cut to the @p available room and never below one pixel
    [[nodiscard]] float ArtGenFieldWidth(float widthInEm, float fontSize, float available);

    /// The Subject and Name fields of a row: a fixed number of ems each, both shrunk in proportion when @p available (what is left of the row
    /// after the remove button) is less than the two
    [[nodiscard]] ArtGenRowWidths ArtGenRowFieldWidths(float available, float fontSize);

    /// What the Set tab shares with the Generate tab: the preset it queues with (by name when saved and unchanged, else as a draft, as Generate
    /// decides). The count and seed are the Set tab's own
    struct ArtGenSetPanelForm {
        const ArtGenPresetForm& preset;
    };

    /// How the Set tab plays a sound variant: the widget's own player, so one sound plays at a time
    struct ArtGenSetPanelSound {
        FunctionRef<void(const AudioClip&)> play;
        FunctionRef<void()> stop;
        bool playing = false;  // the Set tab's sound is the one that plays now
    };

    /**
     * @brief The Set tab of the Generate panel: the subject rows, Queue, the list of sets and the rows of the selected one.
     *
     * ImGui only: every decision (the lines, the queue, the cancel, the retry, the pick and switch, Accept all, Keep) is the session's and is the same call the
     * control API makes (src/Widgets/ArtGen/Sets/CLAUDE.md). Owned by `ArtGenWidget`, which draws the tab bar.
     */
    class ArtGenSetPanel {
    public:
        ~ArtGenSetPanel();

        /// The Set tab: the subject rows and Queue, the list of sets, and one row per job of the selected set
        void DrawSetTab(ArtGenSession& session, const ArtGenSetPanelForm& form, const ArtGenSetPanelSound& sound);

        /// Evicts every picture and clip the rows loaded (the tab is hidden, the set changed, the panel dies)
        void DropThumbnails();

    private:
        /// One variant of a row: its picture, or its clip for a sound. A file that is gone is kept as a gap
        struct Thumb {
            int64_t seed = 0;
            String path;
            bool missing = false;
            ResourceHandle<ITexture> texture;
            ResourceHandle<AudioClip> clip;
        };

        /// What the panel holds for one job; it lives only while the row is on screen
        struct RowThumbs {
            uint64_t seenFrame = 0;
            Vector<Thumb> thumbs;
            // The row's status line and what it was made from
            String statusLine;
            String statusName;
            String statusNewer;
            String statusKind;
            ArtGenRowStatus statusKey = ArtGenRowStatus::NotInGame;
            bool statusValid = false;
        };

        /// The list of subject rows with Add; a row is removed after the loop, never inside it
        void DrawSubjectRows();
        /// The subject rows, the set's fields and Queue
        void DrawQueueForm(ArtGenSession& session, const ArtGenSetPanelForm& form);
        /// Keeps `_selectedSet` naming a set that exists: the newest when it names none
        void ResolveSelection(const ArtGenSession& session);
        /// The compact list of sets, newest first; a click selects
        void DrawSetList(const ArtGenSession& session);
        /// The selected set: its progress and its buttons
        void DrawSetHeader(ArtGenSession& session, const ArtGenSet& set);
        /// One row per job of @p set in a child, the clipper showing only the rows on screen
        void DrawSetRows(ArtGenSession& session, const ArtGenSet& set, const ArtGenSetPanelSound& sound);
        /// Makes the row's "in the game or not, and which file" line again when its status, names or kind changed
        static void RefreshStatusLine(RowThumbs& row, const ArtGenSet& set, const ArtGenSetJob& job, ArtGenRowStatus status, const ArtGenSet* newer);
        void DrawJobRow(ArtGenSession& session, const ArtGenSet& set, size_t index, int32_t columns, const ArtGenSetPanelSound& sound);
        void DrawVariantCell(ArtGenSession& session, const ArtGenSet& set, size_t jobIndex, size_t variantIndex, const ArtGenSetPanelSound& sound);
        void DrawSetFooter(ArtGenSession& session, const ArtGenSet& set);
        /// Loads what the row's variants need and evicts what a Retry took away
        void SyncRow(const ArtGenSession& session, const ArtGenSet& set, size_t index);
        static const Thumb* FindThumb(const RowThumbs& row, int64_t seed);
        static void EvictThumb(Thumb& thumb);
        static void EvictRow(RowThumbs& row);

        Vector<ArtGenSubjectRow> _subjectRows;  // at least one; grows only on Add
        char _setName[65] = {};
        int32_t _count = kArtGenSetDefaultCount;
        bool _randomSeed = true;
        int64_t _seed = 0;
        String _error;
        String _queued;

        String _selectedSet;    // the set the rows show, by name; a name copied only when the selection changes
        String _loadedSet;      // the set `_rows` belongs to
        String _acceptMessage;  // the last Accept all, switch or Keep: its answer or its error, shown under the rows
        bool _acceptFailed = false;
        Vector<RowThumbs> _rows;  // by job index
        uint64_t _frame = 0;
        int64_t _playingSeed = 0;
        int32_t _playingJob = -1;
    };

}  // namespace BECore
