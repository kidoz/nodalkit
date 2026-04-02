#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture;

layout(push_constant) uniform DrawPushConstants {
    vec4 rect;
    vec4 color;
    vec4 clip_rects[2];
    vec4 clip_radii;
    vec4 params0;
    vec4 viewport;
} pc;

layout(location = 0) in vec2 v_tex_coord;
layout(location = 1) in vec2 v_pixel_position;
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
    uint clip_count = uint(pc.params0.w);
    float coverage = 1.0;
    for (uint clip_index = 0; clip_index < clip_count; ++clip_index) {
        float clip_radius = pc.clip_radii[clip_index];
        float clip_coverage =
            clamp(0.5 - rounded_rect_sd(v_pixel_position, pc.clip_rects[clip_index], clip_radius),
                  0.0,
                  1.0);
        coverage *= clip_coverage;
    }

    vec4 sample_color = texture(u_texture, v_tex_coord);
    out_color = vec4(sample_color.rgb, sample_color.a * coverage);
}
