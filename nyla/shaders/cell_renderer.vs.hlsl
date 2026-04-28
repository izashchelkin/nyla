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

struct VS_INPUT
{
    uint vert_id : SV_VertexID;
    uint inst_id : SV_InstanceID;
    // packed cell:
    //   x = (cellY << 16) | cellX
    //   y = (flags << 16) | glyphIndex
    //   z = fgRgba (0xAABBGGRR)
    //   w = bgRgba
    uint4 cell_data : ATTRIB0;
};

struct VS_OUTPUT
{
    float4 position : SV_Position;
    nointerpolation uint glyph_index : GLYPH;
    nointerpolation uint fg : FG;
    nointerpolation uint bg : BG;
    float2 atlas_uv : ATLAS_UV;
};

static const float2 corners[6] = {
    float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1),
};

VS_OUTPUT main(VS_INPUT input)
{
    uint cellX = input.cell_data.x & 0xFFFFu;
    uint cellY = (input.cell_data.x >> 16) & 0xFFFFu;
    uint glyphIndex = input.cell_data.y & 0xFFFFu;

    float2 corner = corners[input.vert_id];

    float2 pixelPos =
        float2(pc.origin_px) + float2(cellX, cellY) * float2(pc.cell_size_px) + corner * float2(pc.cell_size_px);

    float2 ndc = (pixelPos / float2(pc.screen_size_px)) * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    uint glyphsPerRow = pc.atlas_size_px.x / pc.cell_size_px.x;
    uint atlasCol = glyphIndex % glyphsPerRow;
    uint atlasRow = glyphIndex / glyphsPerRow;
    float2 atlasOriginPx = float2(atlasCol, atlasRow) * float2(pc.cell_size_px);
    float2 atlasPosPx = atlasOriginPx + corner * float2(pc.cell_size_px);

    VS_OUTPUT o;
    o.position = float4(ndc, 0.0f, 1.0f);
    o.glyph_index = glyphIndex;
    o.fg = input.cell_data.z;
    o.bg = input.cell_data.w;
    o.atlas_uv = atlasPosPx / float2(pc.atlas_size_px);
    return o;
}
