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
};

[[vk::binding(0, 0)]]
ConstantBuffer<Entity> entity;

[[vk::binding(1, 0)]]
Texture2D texture;

[[vk::binding(2, 0)]]
sampler mysampler;

struct PSOutput
{
    float4 color : SV_Target;
};

PSOutput main(VSOutput input)
{
    PSOutput o;
    o.color = texture.Sample(mysampler, input.uv);
    // o.color = input.color + entity.color;
    return o;
}