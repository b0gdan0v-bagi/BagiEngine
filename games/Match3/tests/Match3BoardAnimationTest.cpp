#include <BECore/Animation/TweenManager.h>
#include <Common/Printers.h>
#include <Match3/Components/BoardAnimation.h>
#include <Match3/Game/Match3GameModel.h>
#include <Match3/Game/MatchFinder.h>

/**
 * Coverage of BoardAnimation without a scene or a renderer: a TweenManager of the test, driven by Advance(dt) with frame times that
 * are multiples of 1/32 s (the default durations are too), so the sums are exact:
 *   - at t = 0 the sprites sit where the board before the swap had its gems, mid-way they lie strictly between, after the swap clip the
 *     two gems have traded places exactly;
 *   - at the end every cell of the board after the swap has exactly one visible sprite of the right gem, the matched ones are at scale 0,
 *     also when one huge dt leaves the same state (rule 12 of Animation/CLAUDE.md);
 *   - Finish, a second Start and the destruction of the object cancel the run and leave no tween behind;
 *   - the rejected swap trades the gems and trades them back; the clip helper cuts a rect against the board.
 */
namespace BECore::Match3::Tests {

    namespace {

        constexpr float kFrame = 1.0f / 32.0f;
        constexpr float kHugeStep = 100.0f;
        constexpr int kMaxFrames = 1000;
        constexpr uint32_t kSeeds[] = {0u, 1u, 7u, 42u, 123u, 9999u};

        bool SameVec(const Vec2& a, const Vec2& b) {
            return a.x == b.x && a.y == b.y;
        }

        /// The gems of the sprites that are visible (scale above zero) and sit exactly on @p cell
        eastl::vector<GemType> VisibleOn(const eastl::vector<GemSprite>& sprites, Cell cell) {
            eastl::vector<GemType> gems;
            for (const GemSprite& sprite : sprites) {
                if (sprite.scale > 0.0f && SameVec(sprite.position, Vec2{static_cast<float>(cell.col), static_cast<float>(cell.row)})) {
                    gems.push_back(sprite.gem);
                }
            }
            return gems;
        }

        /// Every cell has exactly one visible sprite, of the gem the board holds there, and no visible sprite is anywhere else on the board
        void ExpectSpritesShow(const eastl::vector<GemSprite>& sprites, const Match3Board& board, const char* when) {
            int expected = 0;
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    const eastl::vector<GemType> gems = VisibleOn(sprites, Cell{col, row});
                    if (board.Get(col, row) == GemType::None) {
                        EXPECT_TRUE(gems.empty()) << when << " cell " << static_cast<int>(col) << "," << static_cast<int>(row);
                        continue;
                    }
                    ++expected;
                    ASSERT_EQ(gems.size(), 1u) << when << " cell " << static_cast<int>(col) << "," << static_cast<int>(row);
                    EXPECT_EQ(gems[0], board.Get(col, row)) << when << " cell " << static_cast<int>(col) << "," << static_cast<int>(row);
                }
            }
            int visible = 0;
            for (const GemSprite& sprite : sprites) {
                visible += (sprite.scale > 0.0f && sprite.position.y >= 0.0f) ? 1 : 0;  // the refilled gems wait above the board
            }
            EXPECT_EQ(visible, expected) << when;
        }

        /// The length of the timeline `BoardAnimation` builds for @p trace, from its settings
        float ExpectedDuration(const SwapTrace& trace, const BoardAnimationSettings& s) {
            float total = s.swapDuration;
            for (const CascadeStep& step : trace.steps) {
                total += s.clearDuration;
                float longest = 0.0f;
                for (const GemFall& fall : step.falls) {
                    longest = eastl::max(longest, s.fallBase + s.fallPerRow * static_cast<float>(fall.toRow - fall.fromRow));
                }
                for (const GemRefill& refill : step.refills) {
                    longest = eastl::max(longest, s.fallBase + s.fallPerRow * static_cast<float>(refill.toRow - refill.fromRow));
                }
                total += longest;
            }
            return total;
        }

        int RemovedCount(const SwapTrace& trace) {
            int count = 0;
            for (const CascadeStep& step : trace.steps) {
                count += static_cast<int>(step.removed.size());
            }
            return count;
        }

        /// A model after one committed swap: the board before, the model (which holds the board after and the trace)
        struct Swapped {
            Match3Board before;
            Match3GameModel model;
        };

        Swapped MakeSwapped(uint32_t seed, size_t swapIndex = 0) {
            Swapped swapped;
            swapped.model.Reset(seed);
            swapped.before = swapped.model.GetBoard();
            const auto [a, b] = swapped.model.GetLegalSwaps()[swapIndex];
            EXPECT_TRUE(swapped.model.TrySwap(a, b));
            return swapped;
        }

        bool FindRejectedSwap(const Match3GameModel& model, Cell& a, Cell& b) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col + 1 < kBoardSize; ++col) {
                    a = Cell{col, row};
                    b = Cell{static_cast<int8_t>(col + 1), row};
                    if (!model.WouldSwapMatch(a, b)) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// Frames of kFrame until the run ends; the number of frames it took
        int RunToTheEnd(TweenManager& tweens, const BoardAnimation& animation) {
            int frames = 0;
            while (animation.IsRunning() && frames < kMaxFrames) {
                tweens.Advance(kFrame);
                ++frames;
            }
            return frames;
        }

    }  // namespace

    TEST(Match3BoardAnimationTest, SpritesStartWhereTheBoardBeforeTheSwapHadItsGems) {
        for (const uint32_t seed : kSeeds) {
            const Swapped swapped = MakeSwapped(seed);
            TweenManager tweens;
            BoardAnimation animation;
            animation.Start(tweens, swapped.before, *swapped.model.GetLastSwap());

            EXPECT_TRUE(animation.IsRunning());
            EXPECT_EQ(tweens.GetActiveCount(), 1u);
            ExpectSpritesShow(animation.GetSprites(), swapped.before, "at t = 0");
        }
    }

    TEST(Match3BoardAnimationTest, TheSwapSlidesAndTheTwoGemsEndOnEachOthersCell) {
        const Swapped swapped = MakeSwapped(1);
        const SwapTrace& trace = *swapped.model.GetLastSwap();
        TweenManager tweens;
        BoardAnimation animation;
        animation.Start(tweens, swapped.before, trace);
        // The sprites are made in row-major order of a full board, so the index of a sprite is the index of its cell
        const int spriteA = trace.a.Index();
        const int spriteB = trace.b.Index();
        const Vec2 cellA{static_cast<float>(trace.a.col), static_cast<float>(trace.a.row)};
        const Vec2 cellB{static_cast<float>(trace.b.col), static_cast<float>(trace.b.row)};

        tweens.Advance(2 * kFrame);  // half of the swap clip (0.125 s), QuadInOut is 0.5 there
        const GemSprite& movingA = animation.GetSprites()[static_cast<size_t>(spriteA)];
        const GemSprite& movingB = animation.GetSprites()[static_cast<size_t>(spriteB)];
        EXPECT_FALSE(SameVec(movingA.position, cellA));
        EXPECT_FALSE(SameVec(movingA.position, cellB));
        EXPECT_FALSE(SameVec(movingB.position, cellA));
        EXPECT_FALSE(SameVec(movingB.position, cellB));
        EXPECT_GE(movingA.position.x, eastl::min(cellA.x, cellB.x));
        EXPECT_LE(movingA.position.x, eastl::max(cellA.x, cellB.x));
        EXPECT_GE(movingA.position.y, eastl::min(cellA.y, cellB.y));
        EXPECT_LE(movingA.position.y, eastl::max(cellA.y, cellB.y));

        tweens.Advance(3 * kFrame);  // 5/32 s: past the swap clip, before the matched gems have shrunk much
        EXPECT_TRUE(SameVec(animation.GetSprites()[static_cast<size_t>(spriteA)].position, cellB));
        EXPECT_TRUE(SameVec(animation.GetSprites()[static_cast<size_t>(spriteB)].position, cellA));
    }

    TEST(Match3BoardAnimationTest, MatchedGemsShrinkStrictlyBetweenOneAndZero) {
        const Swapped swapped = MakeSwapped(1);
        const SwapTrace& trace = *swapped.model.GetLastSwap();
        ASSERT_FALSE(trace.steps.empty());
        TweenManager tweens;
        BoardAnimation animation;
        animation.Start(tweens, swapped.before, trace);

        tweens.Advance(6 * kFrame);  // 0.1875 s: halfway through the clear clip of the first round
        // After the swap the removed cells hold the gems the swap moved there; find the sprite on each removed cell
        for (const Cell c : trace.steps[0].removed) {
            bool found = false;
            for (const GemSprite& sprite : animation.GetSprites()) {
                if (SameVec(sprite.position, Vec2{static_cast<float>(c.col), static_cast<float>(c.row)}) && sprite.scale < 1.0f) {
                    EXPECT_GT(sprite.scale, 0.0f);
                    EXPECT_LT(sprite.scale, 1.0f);
                    found = true;
                }
            }
            EXPECT_TRUE(found) << static_cast<int>(c.col) << "," << static_cast<int>(c.row);
        }
    }

    TEST(Match3BoardAnimationTest, EveryCellEndsWithOneVisibleSpriteOfTheRightGem) {
        int swapsChecked = 0;
        for (const uint32_t seed : kSeeds) {
            Match3GameModel start;
            start.Reset(seed);
            const size_t count = start.GetLegalSwaps().size();
            for (size_t i = 0; i < count; i += 3) {
                const Swapped swapped = MakeSwapped(seed, i);
                const SwapTrace& trace = *swapped.model.GetLastSwap();
                TweenManager tweens;
                BoardAnimation animation;
                animation.Start(tweens, swapped.before, trace);

                const int frames = RunToTheEnd(tweens, animation);
                EXPECT_FALSE(animation.IsRunning());
                EXPECT_EQ(tweens.GetActiveCount(), 0u);
                EXPECT_EQ(static_cast<float>(frames) * kFrame, ExpectedDuration(trace, animation.GetSettings())) << "seed " << seed << " swap " << i;
                ExpectSpritesShow(animation.GetSprites(), swapped.model.GetBoard(), "at the end");

                int gone = 0;
                for (const GemSprite& sprite : animation.GetSprites()) {
                    EXPECT_TRUE(sprite.scale == 0.0f || sprite.scale == 1.0f);
                    gone += sprite.scale == 0.0f ? 1 : 0;
                }
                EXPECT_EQ(gone, RemovedCount(trace));
                ++swapsChecked;
            }
        }
        EXPECT_GT(swapsChecked, 20);
    }

    TEST(Match3BoardAnimationTest, OneHugeStepLeavesTheSameEndStateAsManySmallOnes) {
        for (const uint32_t seed : kSeeds) {
            const Swapped swapped = MakeSwapped(seed);
            const SwapTrace& trace = *swapped.model.GetLastSwap();

            TweenManager slowTweens;
            BoardAnimation slow;
            slow.Start(slowTweens, swapped.before, trace);
            RunToTheEnd(slowTweens, slow);

            TweenManager fastTweens;
            BoardAnimation fast;
            fast.Start(fastTweens, swapped.before, trace);
            fastTweens.Advance(kHugeStep);

            EXPECT_FALSE(fast.IsRunning());
            ASSERT_EQ(fast.GetSprites().size(), slow.GetSprites().size());
            for (size_t i = 0; i < fast.GetSprites().size(); ++i) {
                EXPECT_EQ(fast.GetSprites()[i].gem, slow.GetSprites()[i].gem) << i;
                EXPECT_TRUE(SameVec(fast.GetSprites()[i].position, slow.GetSprites()[i].position)) << i;
                EXPECT_EQ(fast.GetSprites()[i].scale, slow.GetSprites()[i].scale) << i;
            }
            ExpectSpritesShow(fast.GetSprites(), swapped.model.GetBoard(), "after one huge step");
        }
    }

    TEST(Match3BoardAnimationTest, ADurationFromTheSettingsChangesTheLengthOfTheRun) {
        const Swapped swapped = MakeSwapped(1);
        const SwapTrace& trace = *swapped.model.GetLastSwap();
        const BoardAnimationSettings slower{.swapDuration = 0.25f, .clearDuration = 0.25f, .fallBase = 0.125f, .fallPerRow = 0.0625f};
        TweenManager tweens;
        BoardAnimation animation(slower);
        animation.Start(tweens, swapped.before, trace);

        const int frames = RunToTheEnd(tweens, animation);
        EXPECT_EQ(static_cast<float>(frames) * kFrame, ExpectedDuration(trace, slower));
        ExpectSpritesShow(animation.GetSprites(), swapped.model.GetBoard(), "with slower settings");
    }

    TEST(Match3BoardAnimationTest, FinishStopsTheRunAndDropsTheSprites) {
        const Swapped swapped = MakeSwapped(1);
        TweenManager tweens;
        BoardAnimation animation;
        animation.Start(tweens, swapped.before, *swapped.model.GetLastSwap());
        tweens.Advance(2 * kFrame);
        ASSERT_TRUE(animation.IsRunning());

        animation.Finish();
        EXPECT_FALSE(animation.IsRunning());
        EXPECT_TRUE(animation.GetSprites().empty());
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        tweens.Advance(kHugeStep);  // nothing writes into the dropped sprites
        EXPECT_TRUE(animation.GetSprites().empty());
    }

    TEST(Match3BoardAnimationTest, DestroyingTheObjectMidWayCancelsTheTween) {
        const Swapped swapped = MakeSwapped(1);
        TweenManager tweens;
        {
            BoardAnimation animation;
            animation.Start(tweens, swapped.before, *swapped.model.GetLastSwap());
            tweens.Advance(2 * kFrame);
            EXPECT_EQ(tweens.GetActiveCount(), 1u);
        }
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        tweens.Advance(kHugeStep);  // a setter that still pointed at the dead object would crash under a sanitizer
    }

    TEST(Match3BoardAnimationTest, ASecondStartReplacesTheFirst) {
        const Swapped first = MakeSwapped(1, 0);
        const Swapped second = MakeSwapped(1, 1);
        TweenManager tweens;
        BoardAnimation animation;
        animation.Start(tweens, first.before, *first.model.GetLastSwap());
        tweens.Advance(4 * kFrame);

        animation.Start(tweens, second.before, *second.model.GetLastSwap());
        EXPECT_EQ(tweens.GetActiveCount(), 1u);
        ExpectSpritesShow(animation.GetSprites(), second.before, "right after the second Start");

        RunToTheEnd(tweens, animation);
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        ExpectSpritesShow(animation.GetSprites(), second.model.GetBoard(), "at the end of the second run");
    }

    TEST(Match3BoardAnimationTest, ARejectedSwapSlidesAndSlidesBack) {
        Match3GameModel model;
        model.Reset(1);
        Cell a;
        Cell b;
        ASSERT_TRUE(FindRejectedSwap(model, a, b));
        const Match3Board board = model.GetBoard();
        const Vec2 cellA{static_cast<float>(a.col), static_cast<float>(a.row)};
        const Vec2 cellB{static_cast<float>(b.col), static_cast<float>(b.row)};

        TweenManager tweens;
        BoardAnimation animation;
        animation.StartRejected(tweens, board, a, b);
        EXPECT_TRUE(animation.IsRunning());
        ExpectSpritesShow(animation.GetSprites(), board, "at t = 0");

        tweens.Advance(4 * kFrame);  // exactly the end of the slide (0.125 s), the slide back has not begun
        EXPECT_TRUE(SameVec(animation.GetSprites()[static_cast<size_t>(a.Index())].position, cellB));
        EXPECT_TRUE(SameVec(animation.GetSprites()[static_cast<size_t>(b.Index())].position, cellA));

        const int frames = RunToTheEnd(tweens, animation);
        EXPECT_EQ(static_cast<float>(4 + frames) * kFrame, 2.0f * animation.GetSettings().swapDuration);
        ExpectSpritesShow(animation.GetSprites(), board, "after the slide back");
        EXPECT_TRUE(SameVec(animation.GetSprites()[static_cast<size_t>(a.Index())].position, cellA));
        EXPECT_TRUE(SameVec(animation.GetSprites()[static_cast<size_t>(b.Index())].position, cellB));
    }

    TEST(Match3BoardAnimationTest, ClipToBoardCutsARectAgainstTheBoard) {
        const Rect board{100.0f, 80.0f, 512.0f, 512.0f};

        const auto inside = ClipToBoard(Rect{164.0f, 144.0f, 64.0f, 64.0f}, board);
        ASSERT_TRUE(inside.has_value());
        EXPECT_EQ(inside->x, 164.0f);
        EXPECT_EQ(inside->y, 144.0f);
        EXPECT_EQ(inside->w, 64.0f);
        EXPECT_EQ(inside->h, 64.0f);

        // A gem half above the top edge keeps its lower half
        const auto above = ClipToBoard(Rect{164.0f, 48.0f, 64.0f, 64.0f}, board);
        ASSERT_TRUE(above.has_value());
        EXPECT_EQ(above->y, 80.0f);
        EXPECT_EQ(above->h, 32.0f);
        EXPECT_EQ(above->x, 164.0f);
        EXPECT_EQ(above->w, 64.0f);

        // Fully above, fully beside, and only touching the edge: nothing
        EXPECT_FALSE(ClipToBoard(Rect{164.0f, -64.0f, 64.0f, 64.0f}, board).has_value());
        EXPECT_FALSE(ClipToBoard(Rect{164.0f, 16.0f, 64.0f, 64.0f}, board).has_value());
        EXPECT_FALSE(ClipToBoard(Rect{612.0f, 144.0f, 64.0f, 64.0f}, board).has_value());
        EXPECT_FALSE(ClipToBoard(Rect{164.0f, 144.0f, 0.0f, 0.0f}, board).has_value());
    }

}  // namespace BECore::Match3::Tests
