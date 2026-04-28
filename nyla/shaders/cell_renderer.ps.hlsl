struct PassConst
{
    uint2 screen_size_px;
    uint2 origin_px;
    uint2 cell_size_px;
    uint2 atlas_size_px;
    uint atlas_srv_index;
    uint sampler_index;
    uint2 _pad;
};

ConstantBuffer<PassConst> pc : register(b3, space0);
Texture2D textures[] : register(s0, space1);
SamplerState samplers[] : register(t0, space2);

struct PS_INPUT
{
    float4 position : SV_Position;
    nointerpolation uint glyph_index : GLYPH;
    nointerpolation uint fg : FG;
    nointerpolation uint bg : BG;
    float2 atlas_uv : ATLAS_UV;
};

struct PS_OUTPUT
{
    float4 color : SV_TARGET;
};

float4 UnpackRgba(uint c)
{
    float r = float((c >> 0) & 0xFFu) / 255.0f;
    float g = float((c >> 8) & 0xFFu) / 255.0f;
    float b = float((c >> 16) & 0xFFu) / 255.0f;
    float a = float((c >> 24) & 0xFFu) / 255.0f;
    return float4(r, g, b, a);
}

PS_OUTPUT main(PS_INPUT input)
{
    Texture2D atlas = textures[pc.atlas_srv_index];
    SamplerState samp = samplers[pc.sampler_index];

    float coverage = atlas.Sample(samp, input.atlas_uv).r;

    float4 fg = UnpackRgba(input.fg);
    float4 bg = UnpackRgba(input.bg);

    PS_OUTPUT o;
    o.color = lerp(bg, fg, coverage);
    return o;
}
