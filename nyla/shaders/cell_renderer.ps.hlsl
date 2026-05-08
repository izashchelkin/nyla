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
    nointerpolation uint flags : FLAGS;
    nointerpolation uint fg : FG;
    nointerpolation uint bg : BG;
    float2 atlas_uv : ATLAS_UV;
    float2 local_uv : LOCAL_UV;
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

    // Underline (Flag 1): draw a 1-2 pixel line at the bottom.
    // In local_uv, y=1 is bottom. We use a threshold like 0.9.
    if ((input.flags & 1u) != 0)
    {
        if (input.local_uv.y > 0.9f)
            coverage = 1.0f;
    }

    // Strike (Flag 2): draw a 1-2 pixel line in the middle.
    if ((input.flags & 2u) != 0)
    {
        if (abs(input.local_uv.y - 0.5f) < 0.05f)
            coverage = 1.0f;
    }

    float4 fg = UnpackRgba(input.fg);
    float4 bg = UnpackRgba(input.bg);

    PS_OUTPUT o;
    o.color = lerp(bg, fg, coverage);

    // Cursor (Flags 4-12):
    uint cursorStyle = (input.flags >> 2) & 0x3u;
    if (cursorStyle == 1) // Block
    {
        o.color.rgb = 1.0f - o.color.rgb;
    }
    else if (cursorStyle == 2) // Underline
    {
        if (input.local_uv.y > 0.85f)
            o.color.rgb = 1.0f - o.color.rgb;
    }
    else if (cursorStyle == 3) // Bar
    {
        if (input.local_uv.x < 0.15f)
            o.color.rgb = 1.0f - o.color.rgb;
    }

    return o;
}
