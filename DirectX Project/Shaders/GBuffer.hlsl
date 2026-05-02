Texture2D    gDiffuseMap : register(t0);
SamplerState gsamLinear  : register(s2);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    int      gIsChessboard;
    float3   gPad;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3   gEyePosW;
    float    cbPerObjectPad1;
    float2   gRenderTargetSize;
    float2   gInvRenderTargetSize;
    float    gNearZ;
    float    gFarZ;
    float    gTotalTime;
    float    gDeltaTime;
    float4   gAmbientLight;
    float4   gLightsPad[48];
};

cbuffer cbMaterial : register(b2)
{
    float4   gDiffuseAlbedo;
    float3   gFresnelR0;
    float    gRoughness;
    float4x4 gMatTransform;
};

struct VertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD;
    float2 Color   : COLOR;
};

struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC    : TEXCOORD;
};

struct GBufferOut
{
    float4 Albedo   : SV_Target0;
    float4 Normal   : SV_Target1;
    float4 Position : SV_Target2;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;
    
    float3 pos = vin.PosL;
    if (gIsChessboard == 1)
    {
        float speed         = 1.0f;
        float maxExtraH     = 0.35f;
        float t             = abs(sin(gTotalTime * speed));
        float origBottom    = -0.05f;
        float origTop       =  0.05f;
        float origHeight    =  0.10f;

        if (vin.Color.x == 0.0f)
        {
            float ty = (pos.y - origBottom) / origHeight;
            pos.y = lerp(origBottom, origTop + maxExtraH * t, ty);
        }
        else
        {
            float ty = (pos.y - origBottom) / origHeight;
            pos.y = lerp(origBottom - maxExtraH * t, origTop, ty);
        }
    }

    float4 posW    = mul(float4(pos, 1.0f), gWorld);
    vout.PosW      = posW.xyz;
    vout.NormalW   = mul(vin.NormalL, (float3x3)gWorld);
    vout.PosH      = mul(posW, gViewProj);

    float4 texC    = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC      = mul(texC, gMatTransform).xy;

    return vout;
}

GBufferOut PS(VertexOut pin)
{
    float4 diffuse = gDiffuseMap.Sample(gsamLinear, pin.TexC) * gDiffuseAlbedo;
    float3 normalW = normalize(pin.NormalW);

    GBufferOut gout;
    gout.Albedo = float4(diffuse.rgb, gRoughness);
    gout.Normal = float4(normalW, gFresnelR0.x);
    gout.Position = float4(pin.PosW, 1.0f);
    return gout;
}
