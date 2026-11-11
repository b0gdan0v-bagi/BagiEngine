#pragma once

#include <BECore/Math/Mat4.h>
#include <BECore/Math/Vec4.h>
#include <BECore/Renderer/Gpu/GpuTypes.h>

namespace BECore {

    /// ConstantsSlot::Frame of the lit pipeline: what the whole 3D pass shares. The bytes go to the device as they are; the Slang shader declares the same fields in the same order.
    struct FrameConstants {
        Mat4 viewProjection;  ///< projection * view of the camera
        Vec4 lightDirection;  ///< unit, world space, pointing TO the light (the opposite of the way it travels); w = 0
        Vec4 lightColor;      ///< linear rgb times the intensity; w = 1
        Vec4 ambientColor;    ///< linear rgb times the intensity; w = 1
    };

    /// ConstantsSlot::Draw of the lit pipeline: what one instance owns
    struct DrawConstants {
        Mat4 model;  ///< model to world
        Vec4 tint;   ///< linear rgb and the alpha as 0 to 1
    };

    static_assert(sizeof(FrameConstants) == 112 && sizeof(DrawConstants) == 80, "the layout of the lit shader's constants");
    static_assert(sizeof(FrameConstants) % kConstantsAlignment == 0 && sizeof(DrawConstants) % kConstantsAlignment == 0, "a constant block is a multiple of kConstantsAlignment");
    static_assert(sizeof(FrameConstants) <= kMaxConstantsBytes && sizeof(DrawConstants) <= kMaxConstantsBytes, "a constant block fits kMaxConstantsBytes");
    static_assert(std::is_trivially_copyable_v<FrameConstants> && std::is_trivially_copyable_v<DrawConstants>, "the bytes are copied to the device");

    namespace Details {
        constexpr float kSrgbLinearCutoff = 0.04045f;
        constexpr float kSrgbLinearDivisor = 12.92f;
        constexpr float kSrgbOffset = 0.055f;
        constexpr float kSrgbScale = 1.055f;
        constexpr float kSrgbGamma = 2.4f;
        constexpr float kByteMax = 255.0f;
    }  // namespace Details

    /// An sRGB byte (0 to 255) as a linear float (0 to 1): the exact piecewise curve of the sRGB standard
    [[nodiscard]] inline float SrgbToLinear(uint8_t value) {
        const float encoded = static_cast<float>(value) / Details::kByteMax;
        return encoded <= Details::kSrgbLinearCutoff ? encoded / Details::kSrgbLinearDivisor : std::pow((encoded + Details::kSrgbOffset) / Details::kSrgbScale, Details::kSrgbGamma);
    }

    /// The linear rgb of @p color (sRGB bytes) and its alpha as 0 to 1 (alpha is not a color, it is not converted)
    [[nodiscard]] inline Vec4 ColorToLinear(const Color& color) {
        return {SrgbToLinear(color.r), SrgbToLinear(color.g), SrgbToLinear(color.b), static_cast<float>(color.a) / Details::kByteMax};
    }

    /// The share of a directional light a surface gets: `max(0, dot(normal, toLight))`, both unit and in the same space. The lit Slang shader
    /// must compute the same (slice 2).
    [[nodiscard]] constexpr float LambertTerm(const Vec3& normal, const Vec3& toLight) {
        return std::max(0.0f, Dot(normal, toLight));
    }

}  // namespace BECore
