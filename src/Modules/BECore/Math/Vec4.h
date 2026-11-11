#pragma once

#include <BECore/Math/Vec3.h>
#include <cmath>

namespace BECore {

    /// Four floats: a homogeneous point (`w` = 1) or direction (`w` = 0), a clip-space position, or a color in linear floats
    struct Vec4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        constexpr Vec4() = default;
        constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
        constexpr Vec4(const Vec3& xyz, float w_) : x(xyz.x), y(xyz.y), z(xyz.z), w(w_) {}

        [[nodiscard]] constexpr bool operator==(const Vec4&) const = default;

        /// The first three components; `w` is dropped, not divided by
        [[nodiscard]] constexpr Vec3 XYZ() const {
            return {x, y, z};
        }

        [[nodiscard]] constexpr Vec4 operator+(const Vec4& other) const {
            return {x + other.x, y + other.y, z + other.z, w + other.w};
        }

        [[nodiscard]] constexpr Vec4 operator-(const Vec4& other) const {
            return {x - other.x, y - other.y, z - other.z, w - other.w};
        }

        [[nodiscard]] constexpr Vec4 operator-() const {
            return {-x, -y, -z, -w};
        }

        [[nodiscard]] constexpr Vec4 operator*(float scale) const {
            return {x * scale, y * scale, z * scale, w * scale};
        }

        [[nodiscard]] constexpr Vec4 operator/(float divisor) const {
            return {x / divisor, y / divisor, z / divisor, w / divisor};
        }

        [[nodiscard]] constexpr float LengthSquared() const {
            return x * x + y * y + z * z + w * w;
        }

        [[nodiscard]] float Length() const {
            return std::sqrt(LengthSquared());
        }

        /// The unit vector with the same direction; a zero vector stays zero (no NaN)
        [[nodiscard]] Vec4 Normalized() const {
            const float length = Length();
            return length > 0.0f ? *this / length : Vec4{};
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

    [[nodiscard]] constexpr Vec4 operator*(float scale, const Vec4& v) {
        return v * scale;
    }

    [[nodiscard]] constexpr float Dot(const Vec4& a, const Vec4& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    /// Per component, not clamped, like the other `Lerp` overloads (`Animation/Lerp.h`)
    [[nodiscard]] inline Vec4 Lerp(const Vec4& from, const Vec4& to, float t) {
        return {std::lerp(from.x, to.x, t), std::lerp(from.y, to.y, t), std::lerp(from.z, to.z, t), std::lerp(from.w, to.w, t)};
    }

}  // namespace BECore
