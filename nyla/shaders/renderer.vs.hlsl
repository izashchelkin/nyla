struct Scene
{
    float4x4 vp;
    float4x4 invVp;
};

struct Entity
{
    float4x4 model;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

ConstantBuffer<Scene> scene : register(b1, space0);
ConstantBuffer<Entity> entity : register(b2, space0);

struct VSInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    float4 worldPos = mul(entity.model, float4(input.position, 1));

    o.position = mul(scene.vp, worldPos);

    // o.position.y = -o.position.y;

    o.normal = input.normal;
    o.uv = input.uv;

    return o;
}