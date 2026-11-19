#include <BECore/Math/IVec2.h>
#include <Common/MathTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    static_assert(std::is_trivially_copyable_v<IVec2>);
    static_assert(!std::is_convertible_v<Vec2, IVec2> && !std::is_convertible_v<IVec2, Vec2>);
    static_assert(IVec2(1, 2) + IVec2(3, 4) == IVec2(4, 6));
    static_assert(IVec2(1, 2) * 3 == IVec2(3, 6));
    static_assert(-IVec2(1, -2) == IVec2(-1, 2));
    static_assert(IVec2(Vec2(2.9f, -2.9f)) == IVec2(2, -2));

    namespace {}  // namespace

    TEST(IVec2Test, DefaultIsZero) {
        const IVec2 v;
        EXPECT_EQ(v, IVec2(0, 0));
    }

    TEST(IVec2Test, Arithmetic) {
        const IVec2 a{1, 2};
        const IVec2 b{4, -5};
        EXPECT_EQ(a + b, IVec2(5, -3));
        EXPECT_EQ(a - b, IVec2(-3, 7));
        EXPECT_EQ(-a, IVec2(-1, -2));
        EXPECT_EQ(a * 3, IVec2(3, 6));
        EXPECT_EQ(3 * a, IVec2(3, 6));
        EXPECT_NE(a, b);
    }

    TEST(IVec2Test, CompoundAssignment) {
        IVec2 v{1, 2};
        v += IVec2(10, 20);
        EXPECT_EQ(v, IVec2(11, 22));
        v -= IVec2(1, 2);
        EXPECT_EQ(v, IVec2(10, 20));
        v *= -2;
        EXPECT_EQ(v, IVec2(-20, -40));
    }

    TEST(IVec2Test, ConversionsAreExplicit) {
        EXPECT_EQ(Vec2(IVec2(3, -4)), Vec2(3.0f, -4.0f));
        EXPECT_EQ(IVec2(Vec2(3.7f, -4.7f)), IVec2(3, -4));
    }

    TEST(IVec2Test, VisitPropertiesVisitsXYInOrder) {
        IVec2 v{1, 2};
        StepVisitor<int32_t> visitor;
        EXPECT_TRUE(v.VisitProperties(visitor));
        ASSERT_EQ(visitor.names.size(), 2u);
        EXPECT_EQ(visitor.names[0], "x");
        EXPECT_EQ(visitor.names[1], "y");
        EXPECT_EQ(v, IVec2(2, 3));
    }

    TEST(IVec2Test, XmlRoundTrip) {
        const IVec2 original{-7, 1000000};
        const auto loaded = RoundTripThroughXml(original);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(*loaded, original);
    }

}  // namespace BECore::Tests
