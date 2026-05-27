cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    int      gIsChessboard;
    float3   gPad;
};

cbuffer cbShadowPass : register(b1)
{
    float4x4 gLightViewProj;
};

struct VertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD0;
    float3 Tangent : TANGENT;
    float2 Color   : COLOR;
};

float4 VS(VertexIn vin) : SV_POSITION
{
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    return mul(posW, gLightViewProj);
}
