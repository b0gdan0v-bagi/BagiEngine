#version 450

layout(push_constant) uniform PC {
    vec2 screenSize; // viewport width, height in pixels
} pc;

// A color of the engine is an sRGB value; on an sRGB attachment (kLinearize, set by the pipeline from the swapchain
// format) it is made linear first, because the attachment encodes it again on write. The exact sRGB transfer function.
layout(constant_id = 0) const bool kLinearize = false;

vec3 SrgbToLinear(vec3 c) {
    vec3 low  = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(c, vec3(0.04045)));
}

vec4 ToTarget(vec4 color) {
    return kLinearize ? vec4(SrgbToLinear(color.rgb), color.a) : color; // alpha is coverage, not a colour: untouched
}

layout(location = 0) in vec2 inPos;   // output pixels, the translation and the view transform already applied
layout(location = 1) in vec4 inColor; // r, g, b, a in [0, 1] (R8G8B8A8_UNORM)
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;

void main() {
    outColor = ToTarget(inColor);
    outUV    = inUV;

    // Convert pixel coords to Vulkan NDC: x in [-1,1], y in [-1,1] (y-down).
    vec2 ndc = (inPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
