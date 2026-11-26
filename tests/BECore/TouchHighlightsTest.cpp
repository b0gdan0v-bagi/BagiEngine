// What the editor keeps of the touch record (TouchHighlights): the flashes that fade, the marks that stay, and what bounds both.

#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlTouches.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Widgets/Feedback/TouchHighlights.h>
#include <Widgets/Feedback/UIMotion.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kNode = 7;
        constexpr StringView kComponent = "SpriteRendererComponent";
        constexpr StringView kField = "spriteId";

        class TouchHighlightsTest : public ::testing::Test {
        protected:
            void SetUp() override {
                highlights.Connect(service.Touches());
            }

            /// A report of @p client, taken in at once
            void Touch(uint64_t node, StringView component = {}, StringView field = {}, const IntrusivePtr<ControlClient>& client = {}) {
                service.ReportTouch(client ? client.Get() : agent.Get(), node, component, field);
                highlights.Update(0.0f);
            }

            ControlService service;
            IntrusivePtr<ControlClient> agent = service.OpenLocalClient("agent");
            IntrusivePtr<ControlClient> other = service.OpenLocalClient("other");
            TouchHighlights highlights;
        };

    }  // namespace

    TEST_F(TouchHighlightsTest, ATouchedFieldFlashesWithItsComponentAndNodeAndIsMarked) {
        Touch(kNode, kComponent, kField);

        EXPECT_TRUE(highlights.HasFlashes());
        EXPECT_FLOAT_EQ(highlights.NodeAlpha(kNode), 1.0f);
        EXPECT_FLOAT_EQ(highlights.ComponentAlpha(kNode, kComponent), 1.0f);
        EXPECT_FLOAT_EQ(highlights.FieldAlpha(kNode, kComponent, kField), 1.0f);
        EXPECT_EQ(highlights.FindMark(kNode, kComponent, kField), agent.Get());

        EXPECT_EQ(highlights.NodeAlpha(kNode + 1), 0.0f);
        EXPECT_EQ(highlights.ComponentAlpha(kNode, "TransformComponent"), 0.0f);
        EXPECT_EQ(highlights.FieldAlpha(kNode, kComponent, "texturePath"), 0.0f);
        EXPECT_EQ(highlights.FindMark(kNode + 1, kComponent, kField), nullptr);
        EXPECT_EQ(highlights.FindMark(kNode, kComponent, "texturePath"), nullptr);
    }

    TEST_F(TouchHighlightsTest, AReportThatNamesNoFieldFlashesAndMarksNothing) {
        Touch(kNode);
        Touch(kNode + 1, kComponent);

        EXPECT_GT(highlights.NodeAlpha(kNode), 0.0f);
        EXPECT_EQ(highlights.ComponentAlpha(kNode, kComponent), 0.0f);
        EXPECT_GT(highlights.ComponentAlpha(kNode + 1, kComponent), 0.0f);
        EXPECT_EQ(highlights.FieldAlpha(kNode + 1, kComponent, kField), 0.0f);
        EXPECT_EQ(highlights.FindMark(kNode, {}, {}), nullptr);
        EXPECT_EQ(highlights.FindMark(kNode + 1, kComponent, {}), nullptr);
    }

    TEST_F(TouchHighlightsTest, WhatTheRecordHeldAtTheConnectIsOldNews) {
        service.ReportTouch(agent.Get(), kNode, kComponent, kField);

        highlights.Connect(service.Touches());
        highlights.Update(0.0f);

        EXPECT_FALSE(highlights.HasFlashes());
        EXPECT_EQ(highlights.FindMark(kNode, kComponent, kField), nullptr);
    }

    TEST_F(TouchHighlightsTest, AFlashFadesAndEndsWhileItsMarkStays) {
        Touch(kNode, kComponent, kField);

        highlights.Update(kTouchFlashSeconds * 0.25f);
        const float early = highlights.NodeAlpha(kNode);
        highlights.Update(kTouchFlashSeconds * 0.5f);
        const float late = highlights.NodeAlpha(kNode);

        EXPECT_LT(early, 1.0f);
        EXPECT_LT(late, early);
        EXPECT_GT(late, 0.0f);

        highlights.Update(kTouchFlashSeconds * 0.25f);

        EXPECT_FALSE(highlights.HasFlashes());
        EXPECT_EQ(highlights.NodeAlpha(kNode), 0.0f);
        EXPECT_EQ(highlights.FindMark(kNode, kComponent, kField), agent.Get());
    }

    TEST_F(TouchHighlightsTest, ANewReportStartsTheFlashAgain) {
        Touch(kNode, kComponent, kField);
        highlights.Update(kTouchFlashSeconds * 0.75f);

        Touch(kNode, kComponent, kField);

        EXPECT_FLOAT_EQ(highlights.NodeAlpha(kNode), 1.0f);
        highlights.Update(kTouchFlashSeconds * 0.5f);
        EXPECT_GT(highlights.NodeAlpha(kNode), 0.0f);
    }

    TEST_F(TouchHighlightsTest, ANodeFlashesAsStronglyAsItsFreshestReport) {
        Touch(kNode, kComponent, kField);
        highlights.Update(kTouchFlashSeconds * 0.75f);

        Touch(kNode, "TransformComponent", "position");

        EXPECT_FLOAT_EQ(highlights.NodeAlpha(kNode), 1.0f);
        EXPECT_LT(highlights.ComponentAlpha(kNode, kComponent), 1.0f);
    }

    TEST_F(TouchHighlightsTest, TheNewestClientTakesTheMarkAndADismissRemovesIt) {
        Touch(kNode, kComponent, kField);
        Touch(kNode, kComponent, "color");
        Touch(kNode, kComponent, kField, other);

        EXPECT_EQ(highlights.FindMark(kNode, kComponent, kField), other.Get());
        EXPECT_EQ(highlights.FindMark(kNode, kComponent, "color"), agent.Get());

        highlights.Dismiss(kNode, kComponent, kField);

        EXPECT_EQ(highlights.FindMark(kNode, kComponent, kField), nullptr);
        EXPECT_EQ(highlights.FindMark(kNode, kComponent, "color"), agent.Get());
        EXPECT_GT(highlights.FieldAlpha(kNode, kComponent, kField), 0.0f);  // the flash says that it was written, and runs on
    }

    TEST_F(TouchHighlightsTest, FullFlashesGiveAwayTheOneReportedLongestAgo) {
        for (uint64_t node = 1; node <= TouchHighlights::kMaxFlashes; ++node) {
            Touch(node);
        }
        Touch(1);  // reported again, so the second is now the oldest

        Touch(TouchHighlights::kMaxFlashes + 1);

        EXPECT_GT(highlights.NodeAlpha(1), 0.0f);
        EXPECT_EQ(highlights.NodeAlpha(2), 0.0f);
        EXPECT_GT(highlights.NodeAlpha(3), 0.0f);
        EXPECT_GT(highlights.NodeAlpha(TouchHighlights::kMaxFlashes + 1), 0.0f);
    }

    TEST_F(TouchHighlightsTest, FullMarksGiveAwayTheOneReportedLongestAgo) {
        for (uint64_t node = 1; node <= TouchHighlights::kMaxMarks + 1; ++node) {
            Touch(node, kComponent, kField);
        }

        EXPECT_EQ(highlights.FindMark(1, kComponent, kField), nullptr);
        EXPECT_EQ(highlights.FindMark(2, kComponent, kField), agent.Get());
        EXPECT_EQ(highlights.FindMark(TouchHighlights::kMaxMarks + 1, kComponent, kField), agent.Get());
    }

    TEST_F(TouchHighlightsTest, AResetForgetsEverythingAndReadsNoMore) {
        Touch(kNode, kComponent, kField);

        highlights.Reset();
        Touch(kNode, kComponent, kField);

        EXPECT_FALSE(highlights.HasFlashes());
        EXPECT_EQ(highlights.FindMark(kNode, kComponent, kField), nullptr);
    }

    TEST_F(TouchHighlightsTest, NothingIsAllocatedAtRestOrWhileAFlashStands) {
        constexpr float kFrameSeconds = kTouchFlashSeconds / 64.0f;
        const auto frames = [this] {
            for (int frame = 0; frame < 8; ++frame) {
                highlights.Update(kFrameSeconds);
                static_cast<void>(highlights.NodeAlpha(kNode));
                static_cast<void>(highlights.ComponentAlpha(kNode, kComponent));
                static_cast<void>(highlights.FieldAlpha(kNode, kComponent, kField));
                static_cast<void>(highlights.FindMark(kNode, kComponent, kField));
            }
        };
        ExpectNoAllocations(frames);
        Touch(kNode, kComponent, kField);

        ExpectNoAllocations(frames);

        EXPECT_GT(highlights.NodeAlpha(kNode), 0.0f);
    }

}  // namespace BECore::Tests
