#include <BECore/Animation/Lerp.h>
#include <BECore/Math/Vec4.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    static_assert(Tweenable<Vec4>);
    static_assert(Vec4(Vec3(1.0f, 2.0f, 3.0f), 4.0f) == Vec4(1.0f, 2.0f, 3.0f, 4.0f));
    static_assert(Vec4(1.0f, 2.0f, 3.0f, 4.0f).XYZ() == Vec3(1.0f, 2.0f, 3.0f));

    namespace {

        struct StepVisitor {
            eastl::vector<eastl::string> names;

            bool Visit(const char* name, float& value) {
                names.push_back(name);
                value += 1.0f;
                return true;
            }
        };

    }  // namespace

    TEST(Vec4Test, CompileTimeChecks) {}

    TEST(Vec4Test, DefaultIsZero) {
        const Vec4 v;
        EXPECT_EQ(v, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    }

    TEST(Vec4Test, FromVec3KeepsW) {
        const Vec4 point{Vec3(1.0f, 2.0f, 3.0f), 1.0f};
        const Vec4 direction{Vec3(1.0f, 2.0f, 3.0f), 0.0f};
        EXPECT_EQ(point.w, 1.0f);
        EXPECT_EQ(direction.w, 0.0f);
        EXPECT_EQ(point.XYZ(), Vec3(1.0f, 2.0f, 3.0f));
    }

    TEST(Vec4Test, XyzDropsWWithoutDividing) {
        EXPECT_EQ(Vec4(2.0f, 4.0f, 6.0f, 2.0f).XYZ(), Vec3(2.0f, 4.0f, 6.0f));
    }

    TEST(Vec4Test, Arithmetic) {
        const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
        const Vec4 b{4.0f, -5.0f, 6.0f, 0.0f};
        EXPECT_EQ(a + b, Vec4(5.0f, -3.0f, 9.0f, 4.0f));
        EXPECT_EQ(a - b, Vec4(-3.0f, 7.0f, -3.0f, 4.0f));
        EXPECT_EQ(-a, Vec4(-1.0f, -2.0f, -3.0f, -4.0f));
        EXPECT_EQ(a * 2.0f, Vec4(2.0f, 4.0f, 6.0f, 8.0f));
        EXPECT_EQ(2.0f * a, Vec4(2.0f, 4.0f, 6.0f, 8.0f));
        EXPECT_EQ(a / 2.0f, Vec4(0.5f, 1.0f, 1.5f, 2.0f));
    }

    TEST(Vec4Test, DotAndLength) {
        const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
        EXPECT_FLOAT_EQ(Dot(a, Vec4(2.0f, 0.0f, -1.0f, 0.5f)), 1.0f);
        EXPECT_FLOAT_EQ(a.LengthSquared(), 30.0f);
        EXPECT_FLOAT_EQ(Vec4(2.0f, 2.0f, 2.0f, 2.0f).Length(), 4.0f);
    }

    TEST(Vec4Test, NormalizedHasUnitLength) {
        EXPECT_FLOAT_EQ(Vec4(1.0f, 2.0f, 3.0f, 4.0f).Normalized().Length(), 1.0f);
    }

    TEST(Vec4Test, NormalizedZeroStaysZero) {
        const Vec4 n = Vec4{}.Normalized();
        EXPECT_EQ(n, Vec4{});
        EXPECT_FALSE(std::isnan(n.x) || std::isnan(n.y) || std::isnan(n.z) || std::isnan(n.w));
    }

    TEST(Vec4Test, Lerp) {
        const Vec4 from{0.0f, 10.0f, -4.0f, 1.0f};
        const Vec4 to{4.0f, -10.0f, 4.0f, 3.0f};
        EXPECT_EQ(Lerp(from, to, 0.0f), from);
        EXPECT_EQ(Lerp(from, to, 0.5f), Vec4(2.0f, 0.0f, 0.0f, 2.0f));
        EXPECT_EQ(Lerp(from, to, 1.0f), to);
    }

    TEST(Vec4Test, VisitPropertiesVisitsXYZWInOrder) {
        Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
        StepVisitor visitor;
        EXPECT_TRUE(v.VisitProperties(visitor));
        ASSERT_EQ(visitor.names.size(), 4u);
        EXPECT_EQ(visitor.names[0], "x");
        EXPECT_EQ(visitor.names[1], "y");
        EXPECT_EQ(visitor.names[2], "z");
        EXPECT_EQ(visitor.names[3], "w");
        EXPECT_EQ(v, Vec4(2.0f, 3.0f, 4.0f, 5.0f));
    }

    TEST(Vec4Test, XmlRoundTrip) {
        const Vec4 original{1.5f, -2.25f, 1000.0f, 0.125f};
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("v"));
        original.Serialize(serializer);
        serializer.EndObject();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(serializer.SaveToString()));
        ASSERT_TRUE(deserializer.BeginObject("v"));
        Vec4 loaded;
        loaded.Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_EQ(loaded, original);
    }

}  // namespace BECore::Tests
