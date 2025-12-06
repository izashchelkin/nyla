struct SceneUbo
{
    float4x4 vp;
    float4x4 invVp;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUbo> scene;

struct EntityUbo
{
    float4x4 model;
};

[[vk::binding(1, 0)]]
ConstantBuffer<EntityUbo> entity;

struct VSInput
{
    float4 position : POSITION;
    float4 color : Color0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    float4 localPos = float4(input.position.xy, 0.0f, 1.0f);

    float4 worldPos = mul(entity.model, localPos);
    o.position = mul(scene.vp, worldPos);
    o.color = input.color.xyz;

    return o;
}