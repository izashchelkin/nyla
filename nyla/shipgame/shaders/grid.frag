#version 460

layout(set = 0, binding = 0) uniform SceneUbo {
    mat4 view;
    mat4 proj;
} scene;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

void main() {
    const float kGridSize   = 32.f;
    const float KLineWidthPx = 1.2f;

    mat4 inv_vp = inverse(scene.proj * scene.view);
    vec4 world = inv_vp * vec4(uv, 0.0, 1.0);
    world /= world.w;
    vec2 wp = world.xy;

    vec2 cell = wp / kGridSize;
    vec2 cell_mod = mod(cell, 64.f);

    vec2 fw = fwidth(cell);
    float pixel_in_cells = max(fw.x, fw.y);

    vec2 frac_cell = fract(cell_mod);
    vec2 dist1d = min(frac_cell, 1.0 - frac_cell);
    float d = min(dist1d.x, dist1d.y);

    float half_thickness_cells = 0.5 * KLineWidthPx * pixel_in_cells;

    float alpha = 1.0 - smoothstep(half_thickness_cells,
                                   half_thickness_cells + pixel_in_cells,
                                   d);

    out_color = vec4(1.0, 1.0, 1.0, alpha * 0.01);
}