#include "BoardAnimation.h"

#include <BECore/Animation/TweenManager.h>

namespace BECore::Match3 {

    eastl::optional<Rect> ClipToBoard(const Rect& rect, const Rect& board) {
        const float left = eastl::max(rect.x, board.x);
        const float top = eastl::max(rect.y, board.y);
        const float right = eastl::min(rect.x + rect.w, board.x + board.w);
        const float bottom = eastl::min(rect.y + rect.h, board.y + board.h);
        if (right <= left || bottom <= top) {
            return eastl::nullopt;
        }
        return Rect{left, top, right - left, bottom - top};
    }

    namespace {

        constexpr int kNoSprite = -1;

        Vec2 CellPosition(Cell c) {
            return Vec2{static_cast<float>(c.col), static_cast<float>(c.row)};
        }

        /// The first tween of a group starts after what came before, the others with it
        template <Tweenable T>
        void AddToGroup(Timeline& timeline, bool& firstOfGroup, T from, T to, const TweenSettings& settings, eastl::function<void(const T&)> onValue) {
            if (firstOfGroup) {
                timeline.Append<T>(from, to, settings, std::move(onValue));
                firstOfGroup = false;
            } else {
                timeline.Join<T>(from, to, settings, std::move(onValue));
            }
        }

    }  // namespace

    bool BoardAnimation::IsRunning() const {
        const auto& handle = _play.Get();
        return handle && !handle->IsFinished();
    }

    void BoardAnimation::Finish() {
        _play.Reset();
        _sprites.clear();
    }

    BoardAnimation::SpriteGrid BoardAnimation::BeginRun(const Match3Board& before) {
        Finish();
        SpriteGrid grid;
        grid.fill(kNoSprite);
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                const GemType gem = before.Get(col, row);
                if (gem != GemType::None) {
                    grid[Cell{col, row}.Index()] = static_cast<int>(_sprites.size());
                    _sprites.push_back(GemSprite{gem, CellPosition(Cell{col, row}), 1.0f});
                }
            }
        }
        return grid;
    }

    void BoardAnimation::AddSwap(Timeline& timeline, SpriteGrid& grid, Cell a, Cell b) {
        if (!a.IsInside() || !b.IsInside()) {
            return;
        }
        const int spriteA = grid[a.Index()];
        const int spriteB = grid[b.Index()];
        const TweenSettings settings{.duration = _settings.swapDuration, .ease = EaseType::QuadInOut};
        bool first = true;
        if (spriteA != kNoSprite) {
            AddToGroup<Vec2>(timeline, first, CellPosition(a), CellPosition(b), settings, [this, spriteA](const Vec2& position) { _sprites[spriteA].position = position; });
        }
        if (spriteB != kNoSprite) {
            AddToGroup<Vec2>(timeline, first, CellPosition(b), CellPosition(a), settings, [this, spriteB](const Vec2& position) { _sprites[spriteB].position = position; });
        }
        eastl::swap(grid[a.Index()], grid[b.Index()]);
    }

    void BoardAnimation::AddCascadeStep(Timeline& timeline, SpriteGrid& grid, const CascadeStep& step) {
        eastl::vector<int> clearedSprites;
        eastl::vector<eastl::pair<Cell, GemType>> cleared;
        for (const Cell c : step.removed) {
            const int sprite = grid[c.Index()];
            grid[c.Index()] = kNoSprite;
            if (sprite != kNoSprite) {
                clearedSprites.push_back(sprite);
                cleared.emplace_back(c, _sprites[sprite].gem);
            }
        }
        if (!cleared.empty()) {
            // Before the shrink tweens: it fires when the round before has ended, at the moment these gems start to shrink
            timeline.AppendCallback([this, cleared = std::move(cleared)]() {
                if (_clearedSink) {
                    for (const auto& [cell, gem] : cleared) {
                        _clearedSink(cell, gem);
                    }
                }
            });
        }
        const TweenSettings clear{.duration = _settings.clearDuration, .ease = EaseType::QuadIn};
        bool first = true;
        for (const int sprite : clearedSprites) {
            AddToGroup<float>(timeline, first, 1.0f, 0.0f, clear, [this, sprite](const float& scale) { _sprites[sprite].scale = scale; });
        }

        // One group: the gems that kept their place and the refilled ones all start together, each lasts as long as its distance asks
        first = true;
        const auto addFall = [this, &timeline, &first](int sprite, int fromRow, int toRow) {
            const TweenSettings settings{.duration = _settings.fallBase + _settings.fallPerRow * static_cast<float>(toRow - fromRow), .ease = EaseType::QuadIn};
            AddToGroup<float>(timeline, first, static_cast<float>(fromRow), static_cast<float>(toRow), settings, [this, sprite](const float& row) { _sprites[sprite].position.y = row; });
        };
        for (const GemFall& fall : step.falls) {
            const int sprite = grid[Cell{fall.col, fall.fromRow}.Index()];
            grid[Cell{fall.col, fall.fromRow}.Index()] = kNoSprite;
            grid[Cell{fall.col, fall.toRow}.Index()] = sprite;
            if (sprite != kNoSprite) {
                addFall(sprite, fall.fromRow, fall.toRow);
            }
        }
        for (const GemRefill& refill : step.refills) {
            const int sprite = static_cast<int>(_sprites.size());
            _sprites.push_back(GemSprite{refill.gem, Vec2{static_cast<float>(refill.col), static_cast<float>(refill.fromRow)}, 1.0f});
            grid[Cell{refill.col, refill.toRow}.Index()] = sprite;
            addFall(sprite, refill.fromRow, refill.toRow);
        }
    }

    void BoardAnimation::Play(TweenManager& tweens, Timeline&& timeline) {
        _play.Reset(tweens.Play(std::move(timeline)));
    }

    void BoardAnimation::Start(TweenManager& tweens, const Match3Board& before, const SwapTrace& trace) {
        SpriteGrid grid = BeginRun(before);
        Timeline timeline;
        AddSwap(timeline, grid, trace.a, trace.b);
        for (const CascadeStep& step : trace.steps) {
            AddCascadeStep(timeline, grid, step);
        }
        Play(tweens, std::move(timeline));
    }

    void BoardAnimation::StartRejected(TweenManager& tweens, const Match3Board& board, Cell a, Cell b) {
        if (!a.IsInside() || !b.IsInside()) {
            return;  // nothing to slide
        }
        SpriteGrid grid = BeginRun(board);
        Timeline timeline;
        AddSwap(timeline, grid, a, b);
        AddSwap(timeline, grid, a, b);  // the same two sprites, now on each other's cell: back
        Play(tweens, std::move(timeline));
    }

}  // namespace BECore::Match3
