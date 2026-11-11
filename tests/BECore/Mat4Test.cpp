#include <BECore/Math/Mat4.h>
#include <gtest/gtest.h>
#include <numbers>

namespace BECore::Tests {

    static_assert(Mat4{} == Mat4::Identity());
    static_assert(Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f)).TransformPoint(Vec3(1.0f, 1.0f, 1.0f)) == Vec3(2.0f, 3.0f, 4.0f));
    static_assert(Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f)).TransformDirection(Vec3(1.0f, 1.0f, 1.0f)) == Vec3(1.0f, 1.0f, 1.0f));

    namespace {

        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kHalfPi = kPi * 0.5f;
        constexpr float kTolerance = 1e-5f;

        constexpr Vec3 kAxisX{1.0f, 0.0f, 0.0f};
        constexpr Vec3 kAxisY{0.0f, 1.0f, 0.0f};
        constexpr Vec3 kAxisZ{0.0f, 0.0f, 1.0f};

        void ExpectVecNear(const Vec3& actual, const Vec3& expected, float tolerance = kTolerance) {
            EXPECT_NEAR(actual.x, expected.x, tolerance);
            EXPECT_NEAR(actual.y, expected.y, tolerance);
            EXPECT_NEAR(actual.z, expected.z, tolerance);
        }

        void ExpectMatNear(const Mat4& actual, const Mat4& expected, float tolerance = kTolerance) {
            for (size_t row = 0; row < Mat4::kDimension; ++row) {
                for (size_t column = 0; column < Mat4::kDimension; ++column) {
                    EXPECT_NEAR(actual(row, column), expected(row, column), tolerance) << "row " << row << ", column " << column;
                }
            }
        }

        /// A point through a matrix, with the divide by w
        Vec3 Project(const Mat4& matrix, const Vec3& point) {
            return matrix.TransformPoint(point);
        }

        Mat4 TestTrs() {
            return Mat4::Translation(Vec3(3.0f, -2.0f, 5.0f)) * Mat4::RotationY(0.7f) * Mat4::RotationX(-0.4f) * Mat4::Scale(Vec3(2.0f, 0.5f, 3.0f));
        }

    }  // namespace

    TEST(Mat4Test, CompileTimeChecks) {}

    TEST(Mat4Test, DefaultIsIdentity) {
        const Mat4 m;
        for (size_t row = 0; row < Mat4::kDimension; ++row) {
            for (size_t column = 0; column < Mat4::kDimension; ++column) {
                EXPECT_EQ(m(row, column), row == column ? 1.0f : 0.0f);
            }
        }
    }

    TEST(Mat4Test, IdentityLeavesVectorsAlone) {
        const Vec4 v{1.0f, -2.0f, 3.0f, 0.5f};
        EXPECT_EQ(Mat4::Identity() * v, v);
        EXPECT_EQ(Mat4::Identity() * TestTrs(), TestTrs());
        EXPECT_EQ(TestTrs() * Mat4::Identity(), TestTrs());
    }

    TEST(Mat4Test, DataIsColumnMajor) {
        const Mat4 t = Mat4::Translation(Vec3(7.0f, 8.0f, 9.0f));
        const float* data = t.Data();
        EXPECT_EQ(data[0], 1.0f);
        EXPECT_EQ(data[5], 1.0f);
        EXPECT_EQ(data[10], 1.0f);
        EXPECT_EQ(data[12], 7.0f);
        EXPECT_EQ(data[13], 8.0f);
        EXPECT_EQ(data[14], 9.0f);
        EXPECT_EQ(data[15], 1.0f);

        // Element (row 1, column 2) is the 10th float, not the 7th
        Mat4 m;
        m(1, 2) = 42.0f;
        EXPECT_EQ(m.Data()[2 * 4 + 1], 42.0f);
        EXPECT_EQ(m.Column(2), Vec4(0.0f, 42.0f, 1.0f, 0.0f));
    }

    TEST(Mat4Test, FromColumnsFillsTheMemoryInOrder) {
        const Mat4 m = Mat4::FromColumns({1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}, {9.0f, 10.0f, 11.0f, 12.0f}, {13.0f, 14.0f, 15.0f, 16.0f});
        for (size_t i = 0; i < Mat4::kElementCount; ++i) {
            EXPECT_EQ(m.Data()[i], static_cast<float>(i + 1));
        }
        EXPECT_EQ(m(0, 1), 5.0f);
        EXPECT_EQ(m(3, 0), 4.0f);
    }

    TEST(Mat4Test, TranslationMovesPointsNotDirections) {
        const Mat4 t = Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(t.TransformPoint(Vec3(1.0f, 1.0f, 1.0f)), Vec3(2.0f, 3.0f, 4.0f));
        EXPECT_EQ(t.TransformDirection(Vec3(1.0f, 1.0f, 1.0f)), Vec3(1.0f, 1.0f, 1.0f));
        EXPECT_EQ(t * Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(1.0f, 2.0f, 3.0f, 1.0f));
        EXPECT_EQ(t * Vec4(0.0f, 0.0f, 0.0f, 0.0f), Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    }

    TEST(Mat4Test, ScaleScalesEachAxis) {
        const Mat4 s = Mat4::Scale(Vec3(2.0f, 3.0f, -4.0f));
        EXPECT_EQ(s.TransformPoint(Vec3(1.0f, 1.0f, 1.0f)), Vec3(2.0f, 3.0f, -4.0f));
    }

    TEST(Mat4Test, RotationsAreRightHanded) {
        // A quarter turn about an axis takes the next axis (X, Y, Z cyclic) to the one after it
        ExpectVecNear(Mat4::RotationX(kHalfPi).TransformDirection(kAxisY), kAxisZ);
        ExpectVecNear(Mat4::RotationY(kHalfPi).TransformDirection(kAxisZ), kAxisX);
        ExpectVecNear(Mat4::RotationZ(kHalfPi).TransformDirection(kAxisX), kAxisY);
        // +X about +Y goes to -Z
        ExpectVecNear(Mat4::RotationY(kHalfPi).TransformDirection(kAxisX), -kAxisZ);
        // The axis of a rotation stays put
        ExpectVecNear(Mat4::RotationX(1.234f).TransformDirection(kAxisX), kAxisX);
        ExpectVecNear(Mat4::RotationY(1.234f).TransformDirection(kAxisY), kAxisY);
        ExpectVecNear(Mat4::RotationZ(1.234f).TransformDirection(kAxisZ), kAxisZ);
    }

    TEST(Mat4Test, RotationKeepsLength) {
        const Vec3 v{1.0f, 2.0f, 3.0f};
        EXPECT_NEAR(Mat4::RotationX(0.3f).TransformDirection(v).Length(), v.Length(), kTolerance);
        EXPECT_NEAR((Mat4::RotationZ(2.0f) * Mat4::RotationY(-1.0f)).TransformDirection(v).Length(), v.Length(), kTolerance);
    }

    TEST(Mat4Test, ProductAppliesTheRightOperandFirst) {
        const Mat4 translate = Mat4::Translation(Vec3(10.0f, 0.0f, 0.0f));
        const Mat4 rotate = Mat4::RotationY(kHalfPi);
        // T * R: rotate (1,0,0) to (0,0,-1), then translate
        ExpectVecNear((translate * rotate).TransformPoint(kAxisX), Vec3(10.0f, 0.0f, -1.0f));
        // R * T: translate to (11,0,0), then rotate
        ExpectVecNear((rotate * translate).TransformPoint(kAxisX), Vec3(0.0f, 0.0f, -11.0f));
    }

    TEST(Mat4Test, ProductIsAssociativeWithVectors) {
        const Mat4 a = Mat4::RotationX(0.5f);
        const Mat4 b = Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f));
        const Vec4 v{0.5f, -1.0f, 2.0f, 1.0f};
        const Vec4 viaProduct = (a * b) * v;
        const Vec4 stepwise = a * (b * v);
        EXPECT_NEAR(viaProduct.x, stepwise.x, kTolerance);
        EXPECT_NEAR(viaProduct.y, stepwise.y, kTolerance);
        EXPECT_NEAR(viaProduct.z, stepwise.z, kTolerance);
        EXPECT_NEAR(viaProduct.w, stepwise.w, kTolerance);
    }

    TEST(Mat4Test, Transposed) {
        const Mat4 m = TestTrs();
        const Mat4 t = m.Transposed();
        for (size_t row = 0; row < Mat4::kDimension; ++row) {
            for (size_t column = 0; column < Mat4::kDimension; ++column) {
                EXPECT_EQ(t(row, column), m(column, row));
            }
        }
        EXPECT_EQ(t.Transposed(), m);
        // The translation of a transposed translation moves to the bottom row
        EXPECT_EQ(Mat4::Translation(Vec3(7.0f, 8.0f, 9.0f)).Transposed().Data()[3], 7.0f);
    }

    TEST(Mat4Test, LookAtPutsTheTargetOnMinusZAtTheDistanceOfTheEye) {
        const Vec3 eye{3.0f, 4.0f, 5.0f};
        const Vec3 target{1.0f, 2.0f, 0.0f};
        const Mat4 view = Mat4::LookAt(eye, target, kAxisY);
        const float distance = (target - eye).Length();
        ExpectVecNear(view.TransformPoint(target), Vec3(0.0f, 0.0f, -distance));
        ExpectVecNear(view.TransformPoint(eye), Vec3(0.0f, 0.0f, 0.0f));
    }

    TEST(Mat4Test, LookAtDownMinusZFromTheOriginIsIdentity) {
        ExpectMatNear(Mat4::LookAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), kAxisY), Mat4::Identity());
    }

    TEST(Mat4Test, LookAtKeepsUpUpAndRightRight) {
        // Camera at the origin looking along +X: world up stays view up, world +Z (to its right) is view +X, world +X is straight ahead
        const Mat4 view = Mat4::LookAt(Vec3(0.0f, 0.0f, 0.0f), kAxisX, kAxisY);
        ExpectVecNear(view.TransformDirection(kAxisY), kAxisY);
        ExpectVecNear(view.TransformDirection(kAxisZ), kAxisX);
        ExpectVecNear(view.TransformDirection(kAxisX), -kAxisZ);
    }

    TEST(Mat4Test, LookAtIsARigidTransform) {
        const Mat4 view = Mat4::LookAt(Vec3(3.0f, 4.0f, 5.0f), Vec3(-1.0f, 0.0f, 2.0f), kAxisY);
        const Vec3 a{1.0f, 2.0f, 3.0f};
        const Vec3 b{-4.0f, 0.5f, 2.0f};
        EXPECT_NEAR((view.TransformPoint(a) - view.TransformPoint(b)).Length(), (a - b).Length(), 1e-4f);
        const std::optional<Mat4> inverse = view.Inverse();
        ASSERT_TRUE(inverse.has_value());
        ExpectMatNear(*inverse * view, Mat4::Identity(), 1e-4f);
    }

    TEST(Mat4Test, PerspectiveMapsNearToDepthZeroAndFarToDepthOne) {
        const float nearPlane = 0.5f;
        const float farPlane = 50.0f;
        const Mat4 projection = Mat4::Perspective(1.0f, 1.5f, nearPlane, farPlane);
        EXPECT_NEAR(Project(projection, Vec3(0.0f, 0.0f, -nearPlane)).z, 0.0f, kTolerance);
        EXPECT_NEAR(Project(projection, Vec3(0.0f, 0.0f, -farPlane)).z, 1.0f, kTolerance);
        // Depth grows with distance and stays inside 0..1 between the planes
        const float middle = Project(projection, Vec3(0.0f, 0.0f, -10.0f)).z;
        EXPECT_GT(middle, 0.0f);
        EXPECT_LT(middle, 1.0f);
        EXPECT_GT(Project(projection, Vec3(0.0f, 0.0f, -20.0f)).z, middle);
        // The point in front of the near plane is outside (negative depth), the one beyond the far plane too (greater than 1)
        EXPECT_LT(Project(projection, Vec3(0.0f, 0.0f, -0.25f)).z, 0.0f);
        EXPECT_GT(Project(projection, Vec3(0.0f, 0.0f, -100.0f)).z, 1.0f);
    }

    TEST(Mat4Test, PerspectiveClipWIsTheDistance) {
        const Mat4 projection = Mat4::Perspective(1.0f, 1.0f, 0.1f, 100.0f);
        EXPECT_FLOAT_EQ((projection * Vec4(1.0f, 2.0f, -7.0f, 1.0f)).w, 7.0f);
    }

    TEST(Mat4Test, PerspectiveYIsUp) {
        const Mat4 projection = Mat4::Perspective(1.0f, 1.0f, 0.1f, 100.0f);
        EXPECT_GT(Project(projection, Vec3(0.0f, 0.5f, -10.0f)).y, 0.0f);
        EXPECT_LT(Project(projection, Vec3(0.0f, -0.5f, -10.0f)).y, 0.0f);
        EXPECT_GT(Project(projection, Vec3(0.5f, 0.0f, -10.0f)).x, 0.0f);
    }

    TEST(Mat4Test, PerspectiveFrustumEdgeIsOne) {
        const float fovY = 1.2f;
        const float aspect = 2.0f;
        const float nearPlane = 0.3f;
        const Mat4 projection = Mat4::Perspective(fovY, aspect, nearPlane, 100.0f);
        const float halfHeight = nearPlane * std::tan(fovY * 0.5f);
        const float halfWidth = halfHeight * aspect;
        const Vec3 corner = Project(projection, Vec3(halfWidth, halfHeight, -nearPlane));
        EXPECT_NEAR(corner.x, 1.0f, kTolerance);
        EXPECT_NEAR(corner.y, 1.0f, kTolerance);
        const Vec3 oppositeCorner = Project(projection, Vec3(-halfWidth, -halfHeight, -nearPlane));
        EXPECT_NEAR(oppositeCorner.x, -1.0f, kTolerance);
        EXPECT_NEAR(oppositeCorner.y, -1.0f, kTolerance);
        // The same edge further away: the frustum is a pyramid
        const float distance = 25.0f;
        const Vec3 farEdge = Project(projection, Vec3(distance * halfWidth / nearPlane, 0.0f, -distance));
        EXPECT_NEAR(farEdge.x, 1.0f, kTolerance);
    }

    TEST(Mat4Test, OrthographicMapsItsBoxToTheClipVolume) {
        const Mat4 projection = Mat4::Orthographic(-2.0f, 6.0f, -1.0f, 3.0f, 1.0f, 11.0f);
        ExpectVecNear(Project(projection, Vec3(-2.0f, -1.0f, -1.0f)), Vec3(-1.0f, -1.0f, 0.0f));
        ExpectVecNear(Project(projection, Vec3(6.0f, 3.0f, -11.0f)), Vec3(1.0f, 1.0f, 1.0f));
        ExpectVecNear(Project(projection, Vec3(2.0f, 1.0f, -6.0f)), Vec3(0.0f, 0.0f, 0.5f));
        EXPECT_FLOAT_EQ((projection * Vec4(0.0f, 0.0f, -5.0f, 1.0f)).w, 1.0f);
    }

    TEST(Mat4Test, InverseOfTranslationAndScale) {
        const std::optional<Mat4> inverse = (Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f)) * Mat4::Scale(Vec3(2.0f, 4.0f, 8.0f))).Inverse();
        ASSERT_TRUE(inverse.has_value());
        ExpectVecNear(inverse->TransformPoint(Vec3(3.0f, 6.0f, 11.0f)), Vec3(1.0f, 1.0f, 1.0f));
    }

    TEST(Mat4Test, InverseOfTrsTimesItselfIsIdentity) {
        const Mat4 m = TestTrs();
        const std::optional<Mat4> inverse = m.Inverse();
        ASSERT_TRUE(inverse.has_value());
        ExpectMatNear(m * *inverse, Mat4::Identity(), 1e-4f);
        ExpectMatNear(*inverse * m, Mat4::Identity(), 1e-4f);
    }

    TEST(Mat4Test, InverseOfPerspectiveTimesItselfIsIdentity) {
        const Mat4 m = Mat4::Perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        const std::optional<Mat4> inverse = m.Inverse();
        ASSERT_TRUE(inverse.has_value());
        ExpectMatNear(m * *inverse, Mat4::Identity(), 1e-4f);
        // A clip-space point goes back to the view-space point it came from
        const Vec3 viewPoint{1.0f, -2.0f, -8.0f};
        ExpectVecNear(inverse->TransformPoint(Project(m, viewPoint)), viewPoint, 1e-3f);
    }

    TEST(Mat4Test, InverseOfOrthographicTimesItselfIsIdentity) {
        const Mat4 m = Mat4::Orthographic(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 50.0f);
        const std::optional<Mat4> inverse = m.Inverse();
        ASSERT_TRUE(inverse.has_value());
        ExpectMatNear(m * *inverse, Mat4::Identity(), 1e-4f);
    }

    TEST(Mat4Test, InverseNeedsPivoting) {
        // A zero in the first pivot: a swap of two axes plus a translation
        const Mat4 m = Mat4::FromColumns({0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {5.0f, 6.0f, 7.0f, 1.0f});
        const std::optional<Mat4> inverse = m.Inverse();
        ASSERT_TRUE(inverse.has_value());
        ExpectMatNear(m * *inverse, Mat4::Identity());
    }

    TEST(Mat4Test, InverseOfASmallUniformScaleExists) {
        const Mat4 m = Mat4::Scale(Vec3(0.01f, 0.01f, 0.01f));
        const std::optional<Mat4> inverse = m.Inverse();
        ASSERT_TRUE(inverse.has_value());
        ExpectVecNear(inverse->TransformPoint(Vec3(0.01f, 0.02f, 0.03f)), Vec3(1.0f, 2.0f, 3.0f), 1e-4f);
    }

    TEST(Mat4Test, SingularMatricesHaveNoInverse) {
        EXPECT_FALSE(Mat4::Scale(Vec3(0.0f, 1.0f, 1.0f)).Inverse().has_value());
        EXPECT_FALSE(Mat4::Scale(Vec3(1.0f, 1.0f, 0.0f)).Inverse().has_value());
        // Two equal columns
        const Mat4 flat = Mat4::FromColumns({1.0f, 2.0f, 3.0f, 0.0f}, {1.0f, 2.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
        EXPECT_FALSE(flat.Inverse().has_value());
        const Mat4 zero = Mat4::FromColumns({}, {}, {}, {});
        EXPECT_FALSE(zero.Inverse().has_value());
    }

}  // namespace BECore::Tests
