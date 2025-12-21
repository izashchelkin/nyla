struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

struct Entity
{
    float4x4 model;
    float4 color;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Entity> entity;

struct PSOutput
{
    float4 color : SV_Target;
};

PSOutput main(VSOutput input)
{
    PSOutput o;
    o.color = input.color + entity.color;
    return o;
}