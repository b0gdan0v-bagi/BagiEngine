#pragma once

#include <BECore/Animation/ScopedTween.h>
#include <BECore/Animation/Timeline.h>
#include <EASTL/optional.h>
#include <Match3/Game/CascadeTrace.h>

namespace BECore {
    class TweenManager;
}

namespace BECore::Match3 {

    /// How long the parts of the animation of one swap take, in seconds. Powers of two, so a fake clock in a test gives exact sums
    struct BoardAnimationSettings {
        float swapDuration = 0.125f;   ///< the two gems trade places (and trade back for a rejected swap)
        float clearDuration = 0.125f;  ///< the matched gems shrink away
        float fallBase = 0.0625f;      ///< a fall or a refill lasts this much plus @c fallPerRow for every row it covers
        float fallPerRow = 0.03125f;
    };

    /// A gem on the move. The position is the top-left corner of its cell in cell units (column, row; the row is negative above the board)
    struct GemSprite {
        GemType gem = GemType::None;
        Vec2 position;
        float scale = 1.0f;  ///< 1: the size of a gem on its cell, 0: gone (the matched gems end here)
    };

    /**
     * @brief The part of @p rect that lies inside @p board, or nothing when they share no area (a rect that only touches the edge counts as
     *        outside). Axis-aligned, in any one unit: scene units for the component. The refilled gems start above the board, and the renderer's
     *        clip rect works in output pixels, so the component clips the rects of those gems with this.
     */
    [[nodiscard]] eastl::optional<Rect> ClipToBoard(const Rect& rect, const Rect& board);

    /**
     * @brief The tweens of ONE swap, as one `Timeline` played on a `TweenManager`: the swap slides, then per round of the cascade the matched gems
     *        shrink, the gems above fall and the refilled gems drop in from above the board. A rejected swap slides and slides back.
     *
     * The class knows no scene, no renderer and no model: it gets the board BEFORE the swap and the `SwapTrace` the model recorded
     * (`Match3GameModel::GetLastSwap()`), builds one sprite per gem by replaying the trace over a grid of sprite indices, and the timeline writes
     * into the sprites by index. The sprites are the whole result: the owner draws them while `IsRunning()` and the model's own board otherwise.
     * `TweenManager::Play` gives the timeline the game time of the manager, so a test drives it with `Advance(dt)`.
     *
     * Lifetime: the setters capture `this`; the `ScopedTween` member cancels the run when this object goes (rule 8 of `Animation/CLAUDE.md`), so
     * keep it as a member of the owner. Not copyable and not movable for that reason. Main thread only.
     */
    class BoardAnimation {
    public:
        BoardAnimation() = default;
        explicit BoardAnimation(BoardAnimationSettings settings) : _settings(settings) {}

        BoardAnimation(const BoardAnimation&) = delete;
        BoardAnimation& operator=(const BoardAnimation&) = delete;
        BoardAnimation(BoardAnimation&&) = delete;
        BoardAnimation& operator=(BoardAnimation&&) = delete;

        /// Plays the swap and the cascade of @p trace; @p before is the board before the swap. Replaces a run in progress (no setter call from the old one afterwards)
        void Start(TweenManager& tweens, const Match3Board& before, const SwapTrace& trace);

        /// Plays the two gems of @p a and @p b sliding to each other's cell and back (a swap that made no match). @p board is the board, which does not change. A cell outside the board starts nothing
        void StartRejected(TweenManager& tweens, const Match3Board& board, Cell a, Cell b);

        /// Stops the run where it is (no value is set) and drops the sprites; the owner then draws the model board
        void Finish();

        /**
         * @brief Called once per cleared gem, at the moment the clear of its cascade round starts (the gems begin to shrink). The sink gets the cell
         *        the gem sat on and its type. It does not fire for a swap that clears nothing, nor for a run that `Finish` or a second `Start`
         *        cancelled before that moment; it is read when the callback fires, so it may be set after `Start`. An empty function is none.
         */
        void SetClearedSink(eastl::function<void(Cell, GemType)> sink) {
            _clearedSink = std::move(sink);
        }

        /// True from `Start` until the last `Advance` of the run (or `Finish`)
        [[nodiscard]] bool IsRunning() const;

        /**
         * @brief The gems of the run, in a stable order. While running they sit where the timeline put them. After the run has ended on its own
         *        they keep the last frame (every gem on its cell, the matched ones at scale 0) until `Finish` or the next `Start`, which is how a
         *        test reads the exact end state. Empty before the first run and after `Finish`. Draw them only while `IsRunning()`.
         */
        [[nodiscard]] const eastl::vector<GemSprite>& GetSprites() const {
            return _sprites;
        }

        [[nodiscard]] const BoardAnimationSettings& GetSettings() const {
            return _settings;
        }

    private:
        /// The sprite index on every cell of the board; -1 for no sprite. It follows the gems while the timeline is built
        using SpriteGrid = eastl::array<int, static_cast<size_t>(kBoardSize) * static_cast<size_t>(kBoardSize)>;

        SpriteGrid BeginRun(const Match3Board& before);
        void AddSwap(Timeline& timeline, SpriteGrid& grid, Cell a, Cell b);
        void AddCascadeStep(Timeline& timeline, SpriteGrid& grid, const CascadeStep& step);
        void Play(TweenManager& tweens, Timeline&& timeline);

        BoardAnimationSettings _settings;
        eastl::function<void(Cell, GemType)> _clearedSink;
        eastl::vector<GemSprite> _sprites;  // built before the timeline plays and never resized while it does: the setters write by index
        ScopedTween _play;
    };

}  // namespace BECore::Match3
