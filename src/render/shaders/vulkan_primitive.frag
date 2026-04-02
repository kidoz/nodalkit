#version 450

layout(push_constant) uniform PrimitivePushConstants {
    vec4 rect;
    vec4 color;
    vec4 clip_rects[2];
    vec4 clip_radii;
    vec4 params0;
    vec4 viewport;
} pc;

layout(location = 0) in vec2 v_pixel_position;
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

void main() {
    float radius = pc.params0.x;
    float thickness = pc.params0.y;
    uint kind = uint(pc.params0.z);
    uint clip_count = uint(pc.params0.w);

    float coverage = clamp(0.5 - rounded_rect_sd(v_pixel_position, pc.rect, radius), 0.0, 1.0);
    for (uint clip_index = 0; clip_index < clip_count; ++clip_index) {
        float clip_radius = pc.clip_radii[clip_index];
        float clip_coverage =
            clamp(0.5 - rounded_rect_sd(v_pixel_position, pc.clip_rects[clip_index], clip_radius),
                  0.0,
                  1.0);
        coverage *= clip_coverage;
    }

    if (kind == 1u) {
        float inner_radius = max(0.0, radius - thickness);
        vec4 inner_rect = vec4(pc.rect.x + thickness,
                               pc.rect.y + thickness,
                               max(0.0, pc.rect.z - (thickness * 2.0)),
                               max(0.0, pc.rect.w - (thickness * 2.0)));
        float inner_coverage =
            clamp(0.5 - rounded_rect_sd(v_pixel_position, inner_rect, inner_radius), 0.0, 1.0);
        coverage = clamp(coverage - inner_coverage, 0.0, 1.0);
    }

    out_color = vec4(pc.color.rgb, pc.color.a * coverage);
}
