#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture;

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

struct ClipRegion {
    vec4 rect;
    vec4 metrics;
    Affine inverse;
};
layout(std430, set = 1, binding = 2) readonly buffer BufferedClips {
    ClipRegion regions[];
} clip_data;

vec2 map_affine(Affine matrix, vec2 point) {
    return vec2(dot(matrix.x.xyz, vec3(point, 1.0)),
                dot(matrix.y.xyz, vec3(point, 1.0)));
}

layout(location = 0) in vec2 v_tex_coord;
layout(location = 0) out vec4 out_color;

float rounded_rect_sd(vec2 pixel, vec4 rect, float radius) {
    float clamped_radius = clamp(radius, 0.0, min(rect.z, rect.w) * 0.5);
    vec2 center = rect.xy + rect.zw * 0.5;
    vec2 local = abs(pixel - center);
    vec2 half_size = rect.zw * 0.5;
    vec2 inner = max(half_size - clamped_radius, vec2(0.0));
    vec2 q = local - inner;
    float outside = length(max(q, vec2(0.0)));
    float inside = min(max(q.x, q.y), 0.0);
    return outside + inside - clamped_radius;
}

float rounded_clip_coverage(vec2 pixel, vec4 rect, float radius) {
    if (rect.z <= 0.0 || rect.w <= 0.0) {
        return 0.0;
    }
    return clamp(0.5 - rounded_rect_sd(pixel, rect, radius), 0.0, 1.0);
}

void main() {
    uint clip_count = pc.params.z >> 8u;
    float coverage = 1.0;

    float clip_mask = 1.0;
    for (uint clip_index = 0; clip_index < clip_count; ++clip_index) {
        float clip_radius = pc.clip_radii[clip_index];
        vec2 clip_pixel = map_affine(draw.clips[clip_index], gl_FragCoord.xy);
        float clip_coverage =
            rounded_clip_coverage(clip_pixel, pc.clip_rects[clip_index], clip_radius);
        clip_mask = min(clip_mask, clip_coverage);
    }
    for (uint index = 0; index < draw.ranges.w; ++index) {
        ClipRegion clip = clip_data.regions[draw.ranges.z + index];
        vec2 pixel = map_affine(clip.inverse, gl_FragCoord.xy);
        clip_mask = min(clip_mask, rounded_clip_coverage(pixel, clip.rect, clip.metrics.x));
    }

    vec4 sample_color = texture(u_texture, v_tex_coord);
    coverage *= clip_mask;
    out_color = vec4(sample_color.rgb, sample_color.a * coverage * pc.color.a);
}
