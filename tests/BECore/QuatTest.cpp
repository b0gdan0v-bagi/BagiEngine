#include <BECore/Math/Quat.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <gtest/gtest.h>
#include <numbers>

namespace BECore::Tests {

    static_assert(Quat{} == Quat::Identity());
    static_assert(Quat::Identity().Rotate(Vec3(1.0f, 2.0f, 3.0f)) == Vec3(1.0f, 2.0f, 3.0f));
    static_assert(Conjugate(Quat(1.0f, 2.0f, 3.0f, 4.0f)) == Quat(-1.0f, -2.0f, -3.0f, 4.0f));

    namespace {

        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kHalfPi = kPi * 0.5f;
        constexpr float kTolerance = 1e-5f;

        constexpr Vec3 kAxisX{1.0f, 0.0f, 0.0f};
        constexpr Vec3 kAxisY{0.0f, 1.0f, 0.0f};
        constexpr Vec3 kAxisZ{0.0f, 0.0f, 1.0f};

        struct StepVisitor {
            eastl::vector<eastl::string> names;

            bool Visit(const char* name, float& value) {
                names.push_back(name);
                value += 1.0f;
                return true;
            }
        };

        void ExpectVecNear(const Vec3& actual, const Vec3& expected, float tolerance = kTolerance) {
            EXPECT_NEAR(actual.x, expected.x, tolerance);
            EXPECT_NEAR(actual.y, expected.y, tolerance);
            EXPECT_NEAR(actual.z, expected.z, tolerance);
        }

        void ExpectQuatNear(const Quat& actual, const Quat& expected, float tolerance = kTolerance) {
            EXPECT_NEAR(actual.x, expected.x, tolerance);
            EXPECT_NEAR(actual.y, expected.y, tolerance);
            EXPECT_NEAR(actual.z, expected.z, tolerance);
            EXPECT_NEAR(actual.w, expected.w, tolerance);
        }

        Quat Negated(const Quat& q) {
            return {-q.x, -q.y, -q.z, -q.w};
        }

    }  // namespace

    TEST(QuatTest, CompileTimeChecks) {}

    TEST(QuatTest, DefaultIsIdentity) {
        const Quat q;
        EXPECT_EQ(q, Quat(0.0f, 0.0f, 0.0f, 1.0f));
        ExpectVecNear(q.Rotate(Vec3(1.0f, 2.0f, 3.0f)), Vec3(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(q.ToMat4(), Mat4::Identity());
    }

    TEST(QuatTest, FromAxisAngleIsUnit) {
        const Quat q = Quat::FromAxisAngle(Vec3(1.0f, 2.0f, 3.0f), 0.9f);
        EXPECT_NEAR(Dot(q, q), 1.0f, kTolerance);
        // The axis does not need to be unit
        ExpectQuatNear(Quat::FromAxisAngle(Vec3(0.0f, 5.0f, 0.0f), 0.9f), Quat::FromAxisAngle(kAxisY, 0.9f));
    }

    TEST(QuatTest, FromAxisAngleOfAZeroAxisIsIdentity) {
        EXPECT_EQ(Quat::FromAxisAngle(Vec3{}, 1.0f), Quat::Identity());
    }

    TEST(QuatTest, QuarterTurnAboutYTurnsXToMinusZ) {
        const Quat q = Quat::FromAxisAngle(kAxisY, kHalfPi);
        ExpectVecNear(q.Rotate(kAxisX), -kAxisZ);
        ExpectVecNear(q.Rotate(kAxisZ), kAxisX);
        ExpectVecNear(q.Rotate(kAxisY), kAxisY);
    }

    TEST(QuatTest, QuarterTurnsAreRightHanded) {
        ExpectVecNear(Quat::FromAxisAngle(kAxisX, kHalfPi).Rotate(kAxisY), kAxisZ);
        ExpectVecNear(Quat::FromAxisAngle(kAxisZ, kHalfPi).Rotate(kAxisX), kAxisY);
    }

    TEST(QuatTest, RotateMatchesTheMatrix) {
        const Vec3 axes[] = {kAxisX, kAxisY, kAxisZ, Vec3(1.0f, 1.0f, 0.0f), Vec3(-1.0f, 2.0f, 0.5f), Vec3(0.3f, -0.7f, 1.0f)};
        const float angles[] = {0.0f, 0.4f, kHalfPi, 2.5f, kPi, -1.3f, 5.0f};
        const Vec3 v{1.0f, -2.0f, 3.0f};
        for (const Vec3& axis : axes) {
            for (const float angle : angles) {
                const Quat q = Quat::FromAxisAngle(axis, angle);
                ExpectVecNear(q.Rotate(v), q.ToMat4().TransformDirection(v), 1e-4f);
            }
        }
    }

    TEST(QuatTest, ToMat4MatchesTheAxisRotationMatrices) {
        const float angle = 0.8f;
        const Mat4 byX = Quat::FromAxisAngle(kAxisX, angle).ToMat4();
        const Mat4 byY = Quat::FromAxisAngle(kAxisY, angle).ToMat4();
        const Mat4 byZ = Quat::FromAxisAngle(kAxisZ, angle).ToMat4();
        for (size_t row = 0; row < Mat4::kDimension; ++row) {
            for (size_t column = 0; column < Mat4::kDimension; ++column) {
                EXPECT_NEAR(byX(row, column), Mat4::RotationX(angle)(row, column), kTolerance);
                EXPECT_NEAR(byY(row, column), Mat4::RotationY(angle)(row, column), kTolerance);
                EXPECT_NEAR(byZ(row, column), Mat4::RotationZ(angle)(row, column), kTolerance);
            }
        }
    }

    TEST(QuatTest, ProductAppliesTheRightOperandFirst) {
        const Quat aboutY = Quat::FromAxisAngle(kAxisY, kHalfPi);
        const Quat aboutX = Quat::FromAxisAngle(kAxisX, kHalfPi);
        // X about Y first: (1,0,0) to (0,0,-1); then about X: (0,0,-1) to (0,1,0)
        ExpectVecNear((aboutX * aboutY).Rotate(kAxisX), kAxisY);
        // X about X first: unchanged; then about Y: to (0,0,-1)
        ExpectVecNear((aboutY * aboutX).Rotate(kAxisX), -kAxisZ);
        // The same order as the matrices
        const Vec3 v{1.0f, 2.0f, 3.0f};
        ExpectVecNear((aboutX * aboutY).Rotate(v), (aboutX.ToMat4() * aboutY.ToMat4()).TransformDirection(v), 1e-4f);
    }

    TEST(QuatTest, ConjugateIsTheInverse) {
        const Quat q = Quat::FromAxisAngle(Vec3(1.0f, -2.0f, 0.5f), 1.1f);
        ExpectQuatNear(q * Conjugate(q), Quat::Identity());
        ExpectQuatNear(Conjugate(q) * q, Quat::Identity());
        const Vec3 v{4.0f, 5.0f, -6.0f};
        ExpectVecNear(Conjugate(q).Rotate(q.Rotate(v)), v, 1e-4f);
    }

    TEST(QuatTest, NormalizedHasUnitLength) {
        const Quat q = Quat(1.0f, 2.0f, 3.0f, 4.0f).Normalized();
        EXPECT_NEAR(Dot(q, q), 1.0f, kTolerance);
        EXPECT_NEAR(q.x / q.w, 0.25f, kTolerance);
    }

    TEST(QuatTest, NormalizedZeroIsIdentity) {
        EXPECT_EQ((Quat{0.0f, 0.0f, 0.0f, 0.0f}).Normalized(), Quat::Identity());
    }

    TEST(QuatTest, DotOfEqualUnitQuaternionsIsOne) {
        const Quat q = Quat::FromAxisAngle(kAxisZ, 0.3f);
        EXPECT_NEAR(Dot(q, q), 1.0f, kTolerance);
        EXPECT_NEAR(Dot(q, Negated(q)), -1.0f, kTolerance);
        EXPECT_NEAR(Dot(Quat::Identity(), Quat::FromAxisAngle(kAxisX, kPi)), 0.0f, kTolerance);
    }

    TEST(QuatTest, SlerpEndpoints) {
        const Quat from = Quat::FromAxisAngle(kAxisY, 0.2f);
        const Quat to = Quat::FromAxisAngle(kAxisY, 2.0f);
        ExpectQuatNear(Slerp(from, to, 0.0f), from);
        ExpectQuatNear(Slerp(from, to, 1.0f), to);
    }

    TEST(QuatTest, SlerpMiddleHalvesTheAngle) {
        const Quat middle = Slerp(Quat::Identity(), Quat::FromAxisAngle(kAxisY, kHalfPi), 0.5f);
        ExpectQuatNear(middle, Quat::FromAxisAngle(kAxisY, kHalfPi * 0.5f));
        EXPECT_NEAR(Dot(middle, middle), 1.0f, kTolerance);
    }

    TEST(QuatTest, SlerpHasConstantAngularSpeed) {
        const Quat from = Quat::FromAxisAngle(kAxisX, 0.1f);
        const Quat to = Quat::FromAxisAngle(kAxisX, 1.7f);
        for (const float t : {0.1f, 0.25f, 0.6f, 0.9f}) {
            ExpectQuatNear(Slerp(from, to, t), Quat::FromAxisAngle(kAxisX, 0.1f + 1.6f * t));
        }
    }

    TEST(QuatTest, SlerpTakesTheShortestPath) {
        const Quat from = Quat::Identity();
        const Quat to = Quat::FromAxisAngle(kAxisY, kHalfPi);
        // -to is the same rotation: the result is the same rotation as with `to` (equal up to the sign)
        const Quat direct = Slerp(from, to, 0.5f);
        const Quat viaNegated = Slerp(from, Negated(to), 0.5f);
        ExpectVecNear(viaNegated.Rotate(kAxisX), direct.Rotate(kAxisX));
        // Not the long way round: half of a quarter turn, not half of three quarters
        ExpectVecNear(viaNegated.Rotate(kAxisX), Quat::FromAxisAngle(kAxisY, kHalfPi * 0.5f).Rotate(kAxisX));
        ExpectQuatNear(Slerp(from, Negated(to), 1.0f), to);
    }

    TEST(QuatTest, SlerpOfNearlyEqualQuaternionsStaysUnitAndBetween) {
        const Quat from = Quat::FromAxisAngle(kAxisZ, 0.5f);
        const Quat to = Quat::FromAxisAngle(kAxisZ, 0.5001f);
        const Quat middle = Slerp(from, to, 0.5f);
        EXPECT_NEAR(Dot(middle, middle), 1.0f, kTolerance);
        ExpectQuatNear(middle, Quat::FromAxisAngle(kAxisZ, 0.50005f), 1e-4f);
        ExpectQuatNear(Slerp(from, from, 0.7f), from);
    }

    TEST(QuatTest, VisitPropertiesVisitsXYZWInOrder) {
        Quat q{1.0f, 2.0f, 3.0f, 4.0f};
        StepVisitor visitor;
        EXPECT_TRUE(q.VisitProperties(visitor));
        ASSERT_EQ(visitor.names.size(), 4u);
        EXPECT_EQ(visitor.names[0], "x");
        EXPECT_EQ(visitor.names[3], "w");
        EXPECT_EQ(q, Quat(2.0f, 3.0f, 4.0f, 5.0f));
    }

    TEST(QuatTest, XmlRoundTrip) {
        const Quat original = Quat::FromAxisAngle(Vec3(1.0f, 2.0f, 3.0f), 0.75f);
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("q"));
        original.Serialize(serializer);
        serializer.EndObject();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(serializer.SaveToString()));
        ASSERT_TRUE(deserializer.BeginObject("q"));
        Quat loaded;
        loaded.Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_EQ(loaded, original);
    }

}  // namespace BECore::Tests
