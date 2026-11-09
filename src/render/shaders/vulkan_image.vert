#version 450

// Packed 128-byte layout shared with DrawPushConstants in vulkan_renderer.cpp:
// params.x/.y carry radius/thickness as raw float bits, params.z packs
// kind | (clip_count << 8), params.w packs viewport width | (height << 16).
layout(push_constant) uniform DrawPushConstants {
    vec4 rect;
    vec4 color;
    vec4 clip_rects[4];
    vec4 clip_radii;
    uvec4 params;
} pc;

struct Affine {
    vec4 x;
    vec4 y;
};
layout(std140, set = 1, binding = 0) uniform DrawUniform {
    Affine world;
    Affine clips[4];
    uvec4 ranges; // first/count for path segments and buffered clips
} draw;

vec2 map_affine(Affine matrix, vec2 point) {
    return vec2(dot(matrix.x.xyz, vec3(point, 1.0)),
                dot(matrix.y.xyz, vec3(point, 1.0)));
}

layout(location = 0) out vec2 v_tex_coord;

void main() {
    const vec2 unit_positions[4] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );

    vec2 unit = unit_positions[gl_VertexIndex];
    vec2 pixel = pc.rect.xy + (pc.rect.zw * unit);
    vec2 world_pixel = map_affine(draw.world, pixel);
    vec2 viewport = vec2(float(pc.params.w & 0xFFFFu), float(pc.params.w >> 16u));
    vec2 ndc;
    ndc.x = (world_pixel.x / viewport.x) * 2.0 - 1.0;
    ndc.y = (world_pixel.y / viewport.y) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_tex_coord = unit;
}
