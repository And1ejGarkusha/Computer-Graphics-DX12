Texture2D gDiffuseMap : register(t0);
SamplerState gsamLinearWrap : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    int gIsChessboard;
    float3 gPad;
};

cbuffer cbShadowPass : register(b1)
{
    float4x4 gLightViewProj;
};

static const float ALPHA_CUTOFF = 0.1f;

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD0;
    float3 Tangent : TANGENT;
    float2 Color : COLOR;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD0;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(posW, gLightViewProj);
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = texC.xy;

    return vout;
}

void PS(VertexOut pin)
{
    float4 diffuseSample = gDiffuseMap.Sample(gsamLinearWrap, pin.TexC);
    clip(diffuseSample.a - ALPHA_CUTOFF);
}
