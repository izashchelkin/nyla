struct SceneUbo
{
    float4x4 vp;
    float4x4 invVp;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUbo> scene;

struct PSInput
{
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    const float kGridSize = 16.0f;
    const float kLineWidthPx = 1.2f;

    float4 world = mul(scene.invVp, float4(input.uv, 0.0f, 1.0f));
    world /= world.w;
    float2 wp = world.xy;

    float2 cell = wp / kGridSize;
    float2 cell_mod = fmod(cell, 64.0f);

    float2 fw = fwidth(cell);
    float pixel_in_cells = max(fw.x, fw.y);

    float2 frac_cell = frac(cell_mod);
    float2 dist1d = min(frac_cell, 1.0f - frac_cell);
    float d = min(dist1d.x, dist1d.y);

    float half_thickness_cells = 0.5f * kLineWidthPx * pixel_in_cells;

    float alpha = 1.0f - smoothstep(half_thickness_cells, half_thickness_cells + pixel_in_cells, d);

    return float4(1.0f, 1.0f, 1.0f, alpha * 0.02f); // out_color
}