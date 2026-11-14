#pragma once

#include <BECore/UI/UIBoxMesh.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The calls of the given kind whose box lies inside @p area grown by the feather of a round part (a part that is drawn as geometry spills half a feather past its rect)
    inline Vector<DrawCall> CallsIn(const RecordingRenderer& renderer, DrawKind kind, const Rect& area) {
        constexpr float kSlack = 2.0f * UIBoxMesh::kFeatherPixels;
        Vector<DrawCall> found;
        for (const DrawCall& call : renderer.calls) {
            if (call.kind == kind && call.rect.x >= area.x - kSlack && call.rect.y >= area.y - kSlack && call.rect.x + call.rect.w <= area.x + area.w + kSlack &&
                call.rect.y + call.rect.h <= area.y + area.h + kSlack) {
                found.push_back(call);
            }
        }
        return found;
    }

    /// A draw at scale 1 of a round part: one geometry call whose box is @p part grown by at most the feather, centered on it (so the shape is symmetric about the part), and
    /// not smaller than @p part shrunk by the feather. A square @p part gives a square box: the circle of a thumb
    inline void ExpectRoundPart(const DrawCall& call, const Rect& part) {
        constexpr float kFeather = UIBoxMesh::kFeatherPixels;
        constexpr float kCenterTolerance = 0.05f;
        ASSERT_EQ(call.kind, DrawKind::Geometry);
        EXPECT_FALSE(call.indices.empty());
        EXPECT_GE(call.rect.x, part.x - kFeather);
        EXPECT_GE(call.rect.y, part.y - kFeather);
        EXPECT_LE(call.rect.x + call.rect.w, part.x + part.w + kFeather);
        EXPECT_LE(call.rect.y + call.rect.h, part.y + part.h + kFeather);
        EXPECT_GE(call.rect.w, part.w - kFeather);
        EXPECT_GE(call.rect.h, part.h - kFeather);
        EXPECT_NEAR(call.rect.x + call.rect.w * 0.5f, part.x + part.w * 0.5f, kCenterTolerance);
        EXPECT_NEAR(call.rect.y + call.rect.h * 0.5f, part.y + part.h * 0.5f, kCenterTolerance);
        if (part.w == part.h) {
            EXPECT_NEAR(call.rect.w, call.rect.h, kCenterTolerance);
        }
    }

}  // namespace BECore::Tests
