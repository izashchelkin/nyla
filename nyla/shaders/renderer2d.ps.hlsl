struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct Entity
{
    float4x4 model;
    float4 color;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Entity> entity;

[[vk::binding(0, 1)]]
SamplerState samplers[];

[[vk::binding(1, 1)]]
Texture2D textures[];

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
    // o.color = input.color + entity.color;
    return o;
}