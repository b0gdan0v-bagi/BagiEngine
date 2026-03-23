#version 450

layout(push_constant) uniform PC {
    vec4 dstRect;    // x, y, w, h in pixels
    vec4 srcUV;      // u0, v0, u1, v1 in [0, 1]
    vec2 screenSize; // viewport width, height in pixels
} pc;

layout(location = 0) out vec2 outUV;

void main() {
    // Generate a quad (2 triangles, 6 vertices) from gl_VertexIndex.
    // Unit corners in CCW order:
    //   0--2,3
    //   | / |
    //  1,4--5
    const vec2 corners[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );

    vec2 corner   = corners[gl_VertexIndex];
    vec2 pixelPos = pc.dstRect.xy + corner * pc.dstRect.zw;

    // Interpolate UV from srcUV rect (u0,v0) to (u1,v1)
    outUV = vec2(
        mix(pc.srcUV.x, pc.srcUV.z, corner.x),
        mix(pc.srcUV.y, pc.srcUV.w, corner.y)
    );

    // Convert pixel coords to Vulkan NDC: x in [-1,1], y in [-1,1] (y-down).
    vec2 ndc = (pixelPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
