#version 450

layout(push_constant) uniform PC {
    vec4 dstRect;
    vec4 srcUV;
    vec2 screenSize;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(tex, inUV);
}
