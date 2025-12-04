struct SceneUbo
{
    float4x4 vp;
    float4x4 invVp;
};

struct EntityUbo
{
    float4x4 model;
    float3 color;
    float _padding;
};

[[vk::push_constant]]
ConstantBuffer<SceneUbo> scene;

[[vk::binding(1, 0)]]
ConstantBuffer<EntityUbo> entity;

struct VSInput
{
    float2 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    float4 localPos = float4(input.position, 0.0f, 1.0f);

    float4 worldPos = mul(entity.model, localPos);
    o.position = mul(scene.vp, worldPos);
    o.color = entity.color;

    return o;
}