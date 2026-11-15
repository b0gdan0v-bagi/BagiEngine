#version 450

layout(push_constant) uniform PC {
    vec4 rect;
    vec4 color;
    vec2 screenSize;
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

layout(location = 0) out vec4 outColor;

void main() {
    outColor = ToTarget(pc.color);
}
