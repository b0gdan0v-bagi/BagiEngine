#version 450

// The vertex shader of imgui_impl_vulkan with one change: the vertex colour is turned from an sRGB value (what ImGui
// and its theme mean) into a linear one, because the swapchain is an sRGB attachment and encodes it again on write.
// The inputs, the push constants and the output struct are the stock ones: the stock fragment shader stays.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor; // r, g, b, a in [0, 1] (R8G8B8A8_UNORM), sRGB values

layout(push_constant) uniform uPushConstant {
    vec2 uScale;
    vec2 uTranslate;
} pc;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out struct {
    vec4 Color;
    vec2 UV;
} Out;

// The exact sRGB transfer function (piecewise), not pow(x, 2.2)
vec3 SrgbToLinear(vec3 c) {
    vec3 low  = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(c, vec3(0.04045)));
}

void main() {
    Out.Color = vec4(SrgbToLinear(aColor.rgb), aColor.a); // alpha is coverage, not a colour: untouched
    Out.UV = aUV;
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0.0, 1.0);
}
