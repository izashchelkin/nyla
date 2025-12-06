struct PSInput
{
    float3 color : COLOR0;
};

struct PSOutput
{
    float4 color : SV_Target;
};

PSOutput main(PSInput input)
{
    PSOutput o;
    o.color = float4(input.color, 1.0);
    return o;
}