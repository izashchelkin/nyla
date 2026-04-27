struct Scene
{
    float4x4 vp;
    float4x4 invVp;
};

struct Entity
{
    float4x4 model;
};

ConstantBuffer<Scene> scene : register(b1, space0);
ConstantBuffer<Entity> entity : register(b2, space0);

struct VSInput
{
    float4 position : POSITION0;
    float4 color : COLOR0;
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
