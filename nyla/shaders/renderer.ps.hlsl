struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

struct Entity
{
    float4x4 model;
    float4 color;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

ConstantBuffer<Entity> entity : register(b2, space0);
Texture2D textures[] : register(s0, space1);
SamplerState samplers[] : register(t0, space2);

struct PSOutput
{
    float4 color : SV_Target;
};

PSOutput main(VSOutput input)
{
    PSOutput o;

    Texture2D texture = textures[entity.textureIndex];
    SamplerState samplerState = samplers[entity.samplerIndex];

    o.color = texture.Sample(samplerState, input.uv);

    return o;
}