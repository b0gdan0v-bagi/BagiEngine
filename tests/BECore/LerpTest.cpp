#include <BECore/Animation/Lerp.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    static_assert(Tweenable<float>);
    static_assert(Tweenable<Vec2>);
    static_assert(Tweenable<Color>);
    static_assert(!Tweenable<eastl::string>);

    TEST(LerpTest, CompileTimeChecks) {}

    TEST(LerpTest, FloatEndpointsAndMiddle) {
        EXPECT_EQ(Lerp(10.0f, 20.0f, 0.0f), 10.0f);
        EXPECT_EQ(Lerp(10.0f, 20.0f, 0.5f), 15.0f);
        EXPECT_EQ(Lerp(10.0f, 20.0f, 1.0f), 20.0f);
        // A pair where from + (to - from) is not exactly to in float
        EXPECT_EQ(Lerp(0.1f, 0.7f, 1.0f), 0.7f);
        EXPECT_EQ(Lerp(0.1f, 0.7f, 0.0f), 0.1f);
    }

    TEST(LerpTest, FloatExtrapolates) {
        EXPECT_FLOAT_EQ(Lerp(10.0f, 20.0f, 1.5f), 25.0f);
        EXPECT_FLOAT_EQ(Lerp(10.0f, 20.0f, -0.5f), 5.0f);
    }

    TEST(LerpTest, Vec2EndpointsAndMiddle) {
        const Vec2 from{0.0f, 10.0f};
        const Vec2 to{4.0f, -10.0f};
        EXPECT_EQ(Lerp(from, to, 0.0f), from);
        EXPECT_EQ(Lerp(from, to, 0.5f), Vec2(2.0f, 0.0f));
        EXPECT_EQ(Lerp(from, to, 1.0f), to);
        EXPECT_EQ(Lerp(from, to, 1.5f), Vec2(6.0f, -20.0f));
    }

    TEST(LerpTest, ColorEndpointsAndMiddle) {
        const Color from{0, 100, 255, 10};
        const Color to{255, 100, 0, 10};
        const Color start = Lerp(from, to, 0.0f);
        const Color end = Lerp(from, to, 1.0f);
        EXPECT_EQ(start.r, 0);
        EXPECT_EQ(start.g, 100);
        EXPECT_EQ(start.b, 255);
        EXPECT_EQ(start.a, 10);
        EXPECT_EQ(end.r, 255);
        EXPECT_EQ(end.g, 100);
        EXPECT_EQ(end.b, 0);
        EXPECT_EQ(end.a, 10);
    }

    TEST(LerpTest, ColorRoundsToNearestHalfAwayFromZero) {
        // 0 to 255 at 0.5 is 127.5, which rounds up to 128; 255 to 0 at 0.5 is the same 127.5
        const Color up = Lerp(Color{0, 0, 0, 0}, Color{255, 1, 255, 255}, 0.5f);
        EXPECT_EQ(up.r, 128);
        EXPECT_EQ(up.g, 1);  // 0.5 rounds up to 1
        EXPECT_EQ(up.b, 128);
        EXPECT_EQ(up.a, 128);
        const Color down = Lerp(Color{255, 255, 255, 255}, Color{0, 0, 0, 0}, 0.5f);
        EXPECT_EQ(down.r, 128);
        // 10 to 13 at 0.4 is 11.2, which rounds down
        EXPECT_EQ(Lerp(Color{10, 0, 0, 0}, Color{13, 0, 0, 0}, 0.4f).r, 11);
    }

    TEST(LerpTest, ColorIsClampedWhenTIsOutsideZeroToOne) {
        const Color from{100, 200, 50, 255};
        const Color to{200, 250, 0, 255};
        const Color over = Lerp(from, to, 1.5f);
        EXPECT_EQ(over.r, 250);
        EXPECT_EQ(over.g, 255);  // 275 clamps to 255, no wrap to 19
        EXPECT_EQ(over.b, 0);    // -25 clamps to 0, no wrap to 231
        EXPECT_EQ(over.a, 255);
        const Color under = Lerp(from, to, -0.5f);
        EXPECT_EQ(under.r, 50);
        EXPECT_EQ(under.g, 175);
        EXPECT_EQ(under.b, 75);
        EXPECT_EQ(Lerp(Color{0, 0, 0, 0}, Color{100, 100, 100, 100}, -1.0f).r, 0);
    }

    TEST(LerpTest, Vec2Operators) {
        const Vec2 a{1.0f, 2.0f};
        const Vec2 b{3.0f, -4.0f};
        EXPECT_EQ(Vec2{}, Vec2(0.0f, 0.0f));
        EXPECT_EQ(a + b, Vec2(4.0f, -2.0f));
        EXPECT_EQ(a - b, Vec2(-2.0f, 6.0f));
        EXPECT_EQ(a * 2.0f, Vec2(2.0f, 4.0f));
        EXPECT_TRUE(a == Vec2(1.0f, 2.0f));
        EXPECT_FALSE(a == b);
        EXPECT_NE(a, b);
    }

}  // namespace BECore::Tests
