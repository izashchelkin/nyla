struct VS_INPUT
{
    uint vertexId : SV_VertexID;
};

struct VS_OUTPUT
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT v)
{
    VS_OUTPUT o;

    const float2 pos[3] = {
        float2(-1.0f, -1.0f),
        float2(3.0f, -1.0f),
        float2(-1.0f, 3.0f),
    };
    float2 p = pos[v.vertexId];

    o.position = float4(p, 0.0f, 1.0f);
    o.uv = p;

    return o;
}