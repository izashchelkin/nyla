struct Scene
{
    float4x4 vp;
    float4x4 invVp;
};

struct Entity
{
    float4x4 model;
    float4 color;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

ConstantBuffer<Scene> scene : register(b1, space0);
ConstantBuffer<Entity> entity : register(b2, space0);

struct VSInput
{
    float4 position : POSITION;
    float4 color : Color0;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    float4 worldPos = mul(entity.model, input.position);
    o.position = mul(scene.vp, worldPos);
    o.color = input.color;
    o.uv = input.uv;

    return o;
}