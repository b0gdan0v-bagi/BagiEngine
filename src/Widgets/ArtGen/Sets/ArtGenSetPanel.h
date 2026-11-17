#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
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

    /// How the Review tab plays a sound variant: the widget's own player, so one sound plays at a time
    struct ArtGenSetPanelSound {
        FunctionRef<void(const AudioClip&)> play;
        FunctionRef<void()> stop;
        bool playing = false;  // the review's sound is the one that plays now
    };

    /**
     * @brief The Set and Review tabs of the Generate panel: the subjects field, Queue, the sets with their progress, and the review of one set.
     *
     * ImGui only: every decision (the lines, the queue, the cancel, the retry, the pick, Accept all) is the session's and is the same call the
     * control API makes (src/Widgets/ArtGen/Sets/CLAUDE.md). Owned by `ArtGenWidget`, which draws the tab bar.
     */
    class ArtGenSetPanel {
    public:
        ~ArtGenSetPanel();

        /// The Set tab
        void DrawQueue(ArtGenSession& session, const ArtGenSetPanelForm& form);
        /// The Review tab: one row per job of the set chosen with Review
        void DrawReview(ArtGenSession& session, const ArtGenSetPanelSound& sound);

        /// True once after Review was pressed on a set: the widget shows the Review tab
        [[nodiscard]] bool TakeReviewRequest();
        /// Evicts every picture and clip the review loaded (the tab is hidden, the set changed, the panel dies)
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

        /// What the review holds for one job; it lives only while the row is on screen
        struct RowThumbs {
            uint64_t seenFrame = 0;
            Vector<Thumb> thumbs;
        };

        void DrawSetEntry(ArtGenSession& session, const ArtGenSet& set);
        void DrawJobRows(ArtGenSession& session, const ArtGenSet& set);
        void DrawReviewRow(ArtGenSession& session, const ArtGenSet& set, size_t index, int32_t columns, const ArtGenSetPanelSound& sound);
        void DrawVariantCell(ArtGenSession& session, const ArtGenSet& set, size_t jobIndex, size_t variantIndex, const ArtGenSetPanelSound& sound);
        void DrawReviewFooter(ArtGenSession& session, const ArtGenSet& set);
        /// Loads what the row's variants need and evicts what a Retry took away
        void SyncRow(const ArtGenSession& session, const ArtGenSet& set, size_t index);
        static const Thumb* FindThumb(const RowThumbs& row, int64_t seed);
        static void EvictThumb(Thumb& thumb);
        static void EvictRow(RowThumbs& row);

        char _subjects[4096] = {};
        char _setName[65] = {};
        String _error;
        String _queued;

        String _reviewSet;  // the set the Review tab shows, by name
        String _loadedSet;  // the set `_rows` belongs to
        bool _openReview = false;
        String _acceptMessage;
        bool _acceptFailed = false;
        Vector<RowThumbs> _rows;  // by job index
        uint64_t _frame = 0;
        int64_t _playingSeed = 0;
        int32_t _playingJob = -1;
    };

}  // namespace BECore
