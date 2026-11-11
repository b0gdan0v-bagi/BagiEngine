#pragma once

#include <cmath>

namespace BECore {

    /// A point or a vector of three floats: the world of the 3D renderer is right-handed, Y up (`Math/CLAUDE.md`)
    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vec3() = default;
        constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

        [[nodiscard]] constexpr bool operator==(const Vec3&) const = default;

        [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const {
            return {x + other.x, y + other.y, z + other.z};
        }

        [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const {
            return {x - other.x, y - other.y, z - other.z};
        }

        [[nodiscard]] constexpr Vec3 operator-() const {
            return {-x, -y, -z};
        }

        [[nodiscard]] constexpr Vec3 operator*(float scale) const {
            return {x * scale, y * scale, z * scale};
        }

        [[nodiscard]] constexpr Vec3 operator/(float divisor) const {
            return {x / divisor, y / divisor, z / divisor};
        }

        [[nodiscard]] constexpr float LengthSquared() const {
            return x * x + y * y + z * z;
        }

        [[nodiscard]] float Length() const {
            return std::sqrt(LengthSquared());
        }

        /// The unit vector with the same direction; a zero vector stays zero (no NaN)
        [[nodiscard]] Vec3 Normalized() const {
            const float length = Length();
            return length > 0.0f ? *this / length : Vec3{};
        }

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("x", x);
            s.WriteAttribute("y", y);
            s.WriteAttribute("z", z);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("x", x);
            d.ReadAttribute("y", y);
            d.ReadAttribute("z", z);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("x", x);
            changed |= v.Visit("y", y);
            changed |= v.Visit("z", z);
            return changed;
        }
    };

    [[nodiscard]] constexpr Vec3 operator*(float scale, const Vec3& v) {
        return v * scale;
    }

    [[nodiscard]] constexpr float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    /// Right-handed: `Cross(X, Y) == Z`
    [[nodiscard]] constexpr Vec3 Cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    /// Per component, not clamped, like the other `Lerp` overloads (`Animation/Lerp.h`), so a `Vec3` is `Tweenable`
    [[nodiscard]] inline Vec3 Lerp(const Vec3& from, const Vec3& to, float t) {
        return {std::lerp(from.x, to.x, t), std::lerp(from.y, to.y, t), std::lerp(from.z, to.z, t)};
    }

}  // namespace BECore
