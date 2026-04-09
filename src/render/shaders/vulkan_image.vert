#version 450

layout(push_constant) uniform DrawPushConstants {
    vec4 rect;
    vec4 color;
    vec4 clip_rects[3];
    vec4 clip_radii;
    vec4 params0;
    vec4 viewport;
} pc;

layout(location = 0) out vec2 v_tex_coord;
layout(location = 1) out vec2 v_pixel_position;

void main() {
    const vec2 unit_positions[4] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );

    vec2 unit = unit_positions[gl_VertexIndex];
    vec2 pixel = pc.rect.xy + (pc.rect.zw * unit);
    vec2 ndc;
    ndc.x = (pixel.x / pc.viewport.x) * 2.0 - 1.0;
    ndc.y = (pixel.y / pc.viewport.y) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_tex_coord = unit;
    v_pixel_position = pixel;
}
