cbuffer StaticUbo : register(b0, space0)
{
    float window_width;
    float window_height;
};

float4 main(float3 in_position : POSITION) : SV_Position
{
    float ndc_x = 2.0f * in_position.x / window_width  - 1.0f;
    float ndc_y = 1.0f - 2.0f * in_position.y / window_height;
    return float4(ndc_x, ndc_y, in_position.z, 1.0f);
}