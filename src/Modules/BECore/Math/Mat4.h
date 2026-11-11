#pragma once

#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <cmath>
#include <cstddef>

namespace BECore {

    /**
     * @brief A 4x4 float matrix: column-major in memory, column vectors, `a * b` applies `b` first.
     *
     * The 16 floats are in column order (`Data()[12..14]` hold the translation), so the bytes go to a shader as they are.
     * World space is right-handed, Y up; view space looks down -Z; clip space has depth 0 to 1 and Y up (`Math/CLAUDE.md`).
     * A default-constructed matrix is the identity. Angles are radians.
     */
    class Mat4 {
    public:
        static constexpr size_t kDimension = 4;
        static constexpr size_t kElementCount = kDimension * kDimension;

        constexpr Mat4() = default;

        [[nodiscard]] static constexpr Mat4 Identity() {
            return {};
        }

        /// The four columns in order (the order of the memory)
        [[nodiscard]] static constexpr Mat4 FromColumns(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3) {
            Mat4 result;
            result.SetColumn(0, c0);
            result.SetColumn(1, c1);
            result.SetColumn(2, c2);
            result.SetColumn(3, c3);
            return result;
        }

        [[nodiscard]] constexpr bool operator==(const Mat4&) const = default;

        /// The element of @p row and @p column (0 to 3)
        [[nodiscard]] constexpr float operator()(size_t row, size_t column) const {
            return _elements[column * kDimension + row];
        }

        [[nodiscard]] constexpr float& operator()(size_t row, size_t column) {
            return _elements[column * kDimension + row];
        }

        [[nodiscard]] constexpr Vec4 Column(size_t column) const {
            return {(*this)(0, column), (*this)(1, column), (*this)(2, column), (*this)(3, column)};
        }

        /// The 16 floats in column order, ready for a constant buffer
        [[nodiscard]] constexpr const float* Data() const {
            return _elements;
        }

        [[nodiscard]] constexpr Mat4 operator*(const Mat4& other) const {
            Mat4 result;
            for (size_t row = 0; row < kDimension; ++row) {
                for (size_t column = 0; column < kDimension; ++column) {
                    float sum = 0.0f;
                    for (size_t k = 0; k < kDimension; ++k) {
                        sum += (*this)(row, k) * other(k, column);
                    }
                    result(row, column) = sum;
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Vec4 operator*(const Vec4& v) const {
            return {(*this)(0, 0) * v.x + (*this)(0, 1) * v.y + (*this)(0, 2) * v.z + (*this)(0, 3) * v.w, (*this)(1, 0) * v.x + (*this)(1, 1) * v.y + (*this)(1, 2) * v.z + (*this)(1, 3) * v.w,
                    (*this)(2, 0) * v.x + (*this)(2, 1) * v.y + (*this)(2, 2) * v.z + (*this)(2, 3) * v.w, (*this)(3, 0) * v.x + (*this)(3, 1) * v.y + (*this)(3, 2) * v.z + (*this)(3, 3) * v.w};
        }

        /// @p point with w = 1, divided by the resulting w (so a projection gives the point after the perspective divide; an affine matrix leaves w at 1)
        [[nodiscard]] constexpr Vec3 TransformPoint(const Vec3& point) const {
            const Vec4 result = *this * Vec4(point, 1.0f);
            return result.w != 0.0f ? result.XYZ() / result.w : result.XYZ();
        }

        /// @p direction with w = 0: the translation does not move it
        [[nodiscard]] constexpr Vec3 TransformDirection(const Vec3& direction) const {
            return (*this * Vec4(direction, 0.0f)).XYZ();
        }

        [[nodiscard]] constexpr Mat4 Transposed() const {
            Mat4 result;
            for (size_t row = 0; row < kDimension; ++row) {
                for (size_t column = 0; column < kDimension; ++column) {
                    result(row, column) = (*this)(column, row);
                }
            }
            return result;
        }

        /**
         * @brief The inverse (Gauss-Jordan with row pivoting in double), or `nullopt` for a singular matrix.
         *
         * Singular means a pivot below `kSingularTolerance` times the largest element, which is what a determinant near zero looks like
         * without depending on the overall scale of the matrix (a uniform scale of 0.01 is still invertible).
         */
        [[nodiscard]] std::optional<Mat4> Inverse() const {
            double a[kDimension][kDimension];
            double inverse[kDimension][kDimension];
            double largest = 0.0;
            for (size_t row = 0; row < kDimension; ++row) {
                for (size_t column = 0; column < kDimension; ++column) {
                    a[row][column] = static_cast<double>((*this)(row, column));
                    inverse[row][column] = row == column ? 1.0 : 0.0;
                    largest = std::max(largest, std::abs(a[row][column]));
                }
            }
            if (largest == 0.0) {
                return std::nullopt;
            }

            for (size_t column = 0; column < kDimension; ++column) {
                size_t pivot = column;
                for (size_t row = column + 1; row < kDimension; ++row) {
                    if (std::abs(a[row][column]) > std::abs(a[pivot][column])) {
                        pivot = row;
                    }
                }
                if (std::abs(a[pivot][column]) <= kSingularTolerance * largest) {
                    return std::nullopt;
                }
                if (pivot != column) {
                    for (size_t k = 0; k < kDimension; ++k) {
                        std::swap(a[pivot][k], a[column][k]);
                        std::swap(inverse[pivot][k], inverse[column][k]);
                    }
                }
                const double pivotInverse = 1.0 / a[column][column];
                for (size_t k = 0; k < kDimension; ++k) {
                    a[column][k] *= pivotInverse;
                    inverse[column][k] *= pivotInverse;
                }
                for (size_t row = 0; row < kDimension; ++row) {
                    if (row == column) {
                        continue;
                    }
                    const double factor = a[row][column];
                    for (size_t k = 0; k < kDimension; ++k) {
                        a[row][k] -= factor * a[column][k];
                        inverse[row][k] -= factor * inverse[column][k];
                    }
                }
            }

            Mat4 result;
            for (size_t row = 0; row < kDimension; ++row) {
                for (size_t column = 0; column < kDimension; ++column) {
                    result(row, column) = static_cast<float>(inverse[row][column]);
                }
            }
            return result;
        }

        [[nodiscard]] static constexpr Mat4 Translation(const Vec3& offset) {
            Mat4 result;
            result(0, 3) = offset.x;
            result(1, 3) = offset.y;
            result(2, 3) = offset.z;
            return result;
        }

        [[nodiscard]] static constexpr Mat4 Scale(const Vec3& factors) {
            Mat4 result;
            result(0, 0) = factors.x;
            result(1, 1) = factors.y;
            result(2, 2) = factors.z;
            return result;
        }

        /// Counter-clockwise seen from +X looking at the origin (right-handed): +Y turns to +Z
        [[nodiscard]] static Mat4 RotationX(float radians) {
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            Mat4 result;
            result(1, 1) = c;
            result(1, 2) = -s;
            result(2, 1) = s;
            result(2, 2) = c;
            return result;
        }

        /// Right-handed: +Z turns to +X, so +X turns to -Z
        [[nodiscard]] static Mat4 RotationY(float radians) {
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            Mat4 result;
            result(0, 0) = c;
            result(0, 2) = s;
            result(2, 0) = -s;
            result(2, 2) = c;
            return result;
        }

        /// Right-handed: +X turns to +Y
        [[nodiscard]] static Mat4 RotationZ(float radians) {
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            Mat4 result;
            result(0, 0) = c;
            result(0, 1) = -s;
            result(1, 0) = s;
            result(1, 1) = c;
            return result;
        }

        /**
         * @brief The view matrix of a camera at @p eye looking at @p target: the target lands on the -Z axis at the distance of the eye.
         *
         * @p up is the world's up (+Y for the engine), and must not be parallel to the view direction; the result is degenerate when it is
         * (or when @p eye equals @p target).
         */
        [[nodiscard]] static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
            const Vec3 forward = (target - eye).Normalized();
            const Vec3 side = Cross(forward, up).Normalized();
            const Vec3 cameraUp = Cross(side, forward);
            Mat4 result;
            result(0, 0) = side.x;
            result(0, 1) = side.y;
            result(0, 2) = side.z;
            result(0, 3) = -Dot(side, eye);
            result(1, 0) = cameraUp.x;
            result(1, 1) = cameraUp.y;
            result(1, 2) = cameraUp.z;
            result(1, 3) = -Dot(cameraUp, eye);
            result(2, 0) = -forward.x;
            result(2, 1) = -forward.y;
            result(2, 2) = -forward.z;
            result(2, 3) = Dot(forward, eye);
            return result;
        }

        /**
         * @brief A perspective projection from view space (looking down -Z) to clip space: depth 0 at @p nearPlane and 1 at @p farPlane
         * after the divide by w, Y up.
         *
         * @param fovYRadians the full vertical field of view, in (0, pi)
         * @param aspect width divided by height
         * @param nearPlane distance of the near plane, positive
         * @param farPlane distance of the far plane, greater than @p nearPlane
         */
        [[nodiscard]] static Mat4 Perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
            const float focal = 1.0f / std::tan(fovYRadians * 0.5f);
            const float depthRange = farPlane - nearPlane;
            Mat4 result;
            result(0, 0) = focal / aspect;
            result(1, 1) = focal;
            result(2, 2) = -farPlane / depthRange;
            result(2, 3) = -farPlane * nearPlane / depthRange;
            result(3, 2) = -1.0f;
            result(3, 3) = 0.0f;
            return result;
        }

        /// A box in view space (looking down -Z; @p nearPlane and @p farPlane are distances along it) to x and y in -1..1 and depth in 0..1
        [[nodiscard]] static constexpr Mat4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
            Mat4 result;
            result(0, 0) = 2.0f / (right - left);
            result(0, 3) = -(right + left) / (right - left);
            result(1, 1) = 2.0f / (top - bottom);
            result(1, 3) = -(top + bottom) / (top - bottom);
            result(2, 2) = -1.0f / (farPlane - nearPlane);
            result(2, 3) = -nearPlane / (farPlane - nearPlane);
            return result;
        }

    private:
        /// A pivot smaller than this fraction of the largest element makes `Inverse()` give up
        static constexpr double kSingularTolerance = 1e-9;

        constexpr void SetColumn(size_t column, const Vec4& value) {
            (*this)(0, column) = value.x;
            (*this)(1, column) = value.y;
            (*this)(2, column) = value.z;
            (*this)(3, column) = value.w;
        }

        float _elements[kElementCount] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    };

}  // namespace BECore
