#pragma once

#include <BECore/Math/Mat4.h>
#include <BECore/Math/Vec3.h>
#include <cmath>

namespace BECore {

    /**
     * @brief A rotation as a unit quaternion `{x, y, z, w}` (`w` is the scalar part), Hamilton product, right-handed.
     *
     * `a * b` applies `b` first, like `Mat4`. A default-constructed quaternion is the identity. `q` and `-q` are the same rotation but not
     * equal (`operator==` compares the components). There is no Euler-angle constructor: the order of the three angles is the scene's decision.
     */
    struct Quat {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        constexpr Quat() = default;
        constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

        [[nodiscard]] static constexpr Quat Identity() {
            return {};
        }

        /// A turn of @p radians about @p axis (counter-clockwise looking against the axis, right-handed); a zero axis gives the identity
        [[nodiscard]] static Quat FromAxisAngle(const Vec3& axis, float radians) {
            const Vec3 unitAxis = axis.Normalized();
            if (unitAxis == Vec3{}) {
                return {};
            }
            const float half = radians * 0.5f;
            const float s = std::sin(half);
            return {unitAxis.x * s, unitAxis.y * s, unitAxis.z * s, std::cos(half)};
        }

        [[nodiscard]] constexpr bool operator==(const Quat&) const = default;

        /// Hamilton product: the rotation `other` first, then `*this`
        [[nodiscard]] constexpr Quat operator*(const Quat& other) const {
            return {w * other.x + x * other.w + y * other.z - z * other.y, w * other.y - x * other.z + y * other.w + z * other.x, w * other.z + x * other.y - y * other.x + z * other.w,
                    w * other.w - x * other.x - y * other.y - z * other.z};
        }

        /// The unit quaternion of the same rotation; a zero quaternion gives the identity
        [[nodiscard]] Quat Normalized() const {
            const float length = std::sqrt(x * x + y * y + z * z + w * w);
            if (length <= 0.0f) {
                return {};
            }
            return {x / length, y / length, z / length, w / length};
        }

        /// @p v turned by this (unit) quaternion; the same as `ToMat4().TransformDirection(v)`, without building the matrix
        [[nodiscard]] constexpr Vec3 Rotate(const Vec3& v) const {
            const Vec3 axisPart{x, y, z};
            const Vec3 t = Cross(axisPart, v) * 2.0f;
            return v + t * w + Cross(axisPart, t);
        }

        /// The rotation matrix of this (unit) quaternion, with no translation
        [[nodiscard]] constexpr Mat4 ToMat4() const {
            Mat4 result;
            result(0, 0) = 1.0f - 2.0f * (y * y + z * z);
            result(0, 1) = 2.0f * (x * y - z * w);
            result(0, 2) = 2.0f * (x * z + y * w);
            result(1, 0) = 2.0f * (x * y + z * w);
            result(1, 1) = 1.0f - 2.0f * (x * x + z * z);
            result(1, 2) = 2.0f * (y * z - x * w);
            result(2, 0) = 2.0f * (x * z - y * w);
            result(2, 1) = 2.0f * (y * z + x * w);
            result(2, 2) = 1.0f - 2.0f * (x * x + y * y);
            return result;
        }

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("x", x);
            s.WriteAttribute("y", y);
            s.WriteAttribute("z", z);
            s.WriteAttribute("w", w);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("x", x);
            d.ReadAttribute("y", y);
            d.ReadAttribute("z", z);
            d.ReadAttribute("w", w);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("x", x);
            changed |= v.Visit("y", y);
            changed |= v.Visit("z", z);
            changed |= v.Visit("w", w);
            return changed;
        }
    };

    [[nodiscard]] constexpr float Dot(const Quat& a, const Quat& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    /// The inverse rotation of a unit quaternion
    [[nodiscard]] constexpr Quat Conjugate(const Quat& q) {
        return {-q.x, -q.y, -q.z, q.w};
    }

    inline constexpr float kSlerpLinearThreshold = 0.9995f;  ///< above this dot product two quaternions are so close that the sine of the angle between them is unreliable

    /**
     * @brief The rotation a fraction @p t of the way from @p from to @p to along the shortest arc (constant angular speed).
     *
     * `q` and `-q` are one rotation, so when the dot product is negative @p to is negated first (the long way round is never taken). Nearly equal
     * quaternions use the normalized linear interpolation. @p t is not clamped. Both inputs must be unit quaternions.
     */
    [[nodiscard]] inline Quat Slerp(const Quat& from, const Quat& to, float t) {
        float cosAngle = Dot(from, to);
        Quat target = to;
        if (cosAngle < 0.0f) {
            cosAngle = -cosAngle;
            target = {-to.x, -to.y, -to.z, -to.w};
        }

        float fromWeight = 1.0f - t;
        float toWeight = t;
        if (cosAngle <= kSlerpLinearThreshold) {
            const float angle = std::acos(cosAngle);
            const float sinAngle = std::sin(angle);
            fromWeight = std::sin((1.0f - t) * angle) / sinAngle;
            toWeight = std::sin(t * angle) / sinAngle;
        }
        return Quat{from.x * fromWeight + target.x * toWeight, from.y * fromWeight + target.y * toWeight, from.z * fromWeight + target.z * toWeight, from.w * fromWeight + target.w * toWeight}
            .Normalized();
    }

}  // namespace BECore
