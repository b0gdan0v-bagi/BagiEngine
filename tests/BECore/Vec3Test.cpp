#include <BECore/Animation/Lerp.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    static_assert(Tweenable<Vec3>);
    static_assert(Vec3(1.0f, 2.0f, 3.0f) + Vec3(4.0f, 5.0f, 6.0f) == Vec3(5.0f, 7.0f, 9.0f));
    static_assert(Cross(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)) == Vec3(0.0f, 0.0f, 1.0f));

    namespace {

        constexpr Vec3 kAxisX{1.0f, 0.0f, 0.0f};
        constexpr Vec3 kAxisY{0.0f, 1.0f, 0.0f};
        constexpr Vec3 kAxisZ{0.0f, 0.0f, 1.0f};

        /// Visits every float of a compound with its name, adds a fixed step and records the names in the order of the visit
        struct StepVisitor {
            eastl::vector<String> names;

            bool Visit(const char* name, float& value) {
                names.push_back(name);
                value += 1.0f;
                return true;
            }
        };

    }  // namespace

    TEST(Vec3Test, CompileTimeChecks) {}

    TEST(Vec3Test, DefaultIsZero) {
        const Vec3 v;
        EXPECT_EQ(v, Vec3(0.0f, 0.0f, 0.0f));
    }

    TEST(Vec3Test, Arithmetic) {
        const Vec3 a{1.0f, 2.0f, 3.0f};
        const Vec3 b{4.0f, -5.0f, 6.0f};
        EXPECT_EQ(a + b, Vec3(5.0f, -3.0f, 9.0f));
        EXPECT_EQ(a - b, Vec3(-3.0f, 7.0f, -3.0f));
        EXPECT_EQ(-a, Vec3(-1.0f, -2.0f, -3.0f));
        EXPECT_EQ(a * 2.0f, Vec3(2.0f, 4.0f, 6.0f));
        EXPECT_EQ(2.0f * a, Vec3(2.0f, 4.0f, 6.0f));
        EXPECT_EQ(b / 2.0f, Vec3(2.0f, -2.5f, 3.0f));
    }

    TEST(Vec3Test, DotProduct) {
        EXPECT_FLOAT_EQ(Dot(Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, -5.0f, 6.0f)), 12.0f);
        EXPECT_FLOAT_EQ(Dot(kAxisX, kAxisY), 0.0f);
        EXPECT_FLOAT_EQ(Dot(kAxisZ, kAxisZ), 1.0f);
    }

    TEST(Vec3Test, CrossProductIsRightHanded) {
        EXPECT_EQ(Cross(kAxisX, kAxisY), kAxisZ);
        EXPECT_EQ(Cross(kAxisY, kAxisZ), kAxisX);
        EXPECT_EQ(Cross(kAxisZ, kAxisX), kAxisY);
        // Anti-commutative, and zero for parallel vectors
        EXPECT_EQ(Cross(kAxisY, kAxisX), -kAxisZ);
        EXPECT_EQ(Cross(kAxisX, kAxisX * 3.0f), Vec3{});
        // Perpendicular to both operands
        const Vec3 a{1.0f, 2.0f, 3.0f};
        const Vec3 b{-2.0f, 0.5f, 4.0f};
        const Vec3 c = Cross(a, b);
        EXPECT_NEAR(Dot(c, a), 0.0f, 1e-5f);
        EXPECT_NEAR(Dot(c, b), 0.0f, 1e-5f);
    }

    TEST(Vec3Test, Length) {
        const Vec3 v{2.0f, 3.0f, 6.0f};
        EXPECT_FLOAT_EQ(v.LengthSquared(), 49.0f);
        EXPECT_FLOAT_EQ(v.Length(), 7.0f);
    }

    TEST(Vec3Test, NormalizedHasUnitLengthAndKeepsDirection) {
        const Vec3 n = Vec3(2.0f, 3.0f, 6.0f).Normalized();
        EXPECT_FLOAT_EQ(n.Length(), 1.0f);
        EXPECT_FLOAT_EQ(n.x, 2.0f / 7.0f);
        EXPECT_FLOAT_EQ(n.y, 3.0f / 7.0f);
        EXPECT_FLOAT_EQ(n.z, 6.0f / 7.0f);
    }

    TEST(Vec3Test, NormalizedZeroStaysZero) {
        const Vec3 n = Vec3{}.Normalized();
        EXPECT_EQ(n, Vec3{});
        EXPECT_FALSE(std::isnan(n.x) || std::isnan(n.y) || std::isnan(n.z));
    }

    TEST(Vec3Test, Lerp) {
        const Vec3 from{0.0f, 10.0f, -4.0f};
        const Vec3 to{4.0f, -10.0f, 4.0f};
        EXPECT_EQ(Lerp(from, to, 0.0f), from);
        EXPECT_EQ(Lerp(from, to, 0.5f), Vec3(2.0f, 0.0f, 0.0f));
        EXPECT_EQ(Lerp(from, to, 1.0f), to);
        EXPECT_EQ(Lerp(from, to, 1.5f), Vec3(6.0f, -20.0f, 8.0f));
    }

    TEST(Vec3Test, VisitPropertiesVisitsXYZInOrder) {
        Vec3 v{1.0f, 2.0f, 3.0f};
        StepVisitor visitor;
        EXPECT_TRUE(v.VisitProperties(visitor));
        ASSERT_EQ(visitor.names.size(), 3u);
        EXPECT_EQ(visitor.names[0], "x");
        EXPECT_EQ(visitor.names[1], "y");
        EXPECT_EQ(visitor.names[2], "z");
        EXPECT_EQ(v, Vec3(2.0f, 3.0f, 4.0f));
    }

    TEST(Vec3Test, XmlRoundTrip) {
        const Vec3 original{1.5f, -2.25f, 1000.0f};
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("v"));
        original.Serialize(serializer);
        serializer.EndObject();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(serializer.SaveToString()));
        ASSERT_TRUE(deserializer.BeginObject("v"));
        Vec3 loaded;
        loaded.Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_EQ(loaded, original);
    }

}  // namespace BECore::Tests
