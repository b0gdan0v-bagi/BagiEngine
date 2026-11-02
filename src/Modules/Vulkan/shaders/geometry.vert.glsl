#version 450

layout(push_constant) uniform PC {
    vec2 screenSize; // viewport width, height in pixels
} pc;

layout(location = 0) in vec2 inPos;   // output pixels, the translation and the view transform already applied
layout(location = 1) in vec4 inColor; // r, g, b, a in [0, 1] (R8G8B8A8_UNORM)
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;

void main() {
    outColor = inColor;
    outUV    = inUV;

    // Convert pixel coords to Vulkan NDC: x in [-1,1], y in [-1,1] (y-down).
    vec2 ndc = (inPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
