#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif
#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 1
#endif
#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 1
#endif

#include "LightingUtil.hlsl"

Texture2D gAlbedoMap   : register(t1);
Texture2D gNormalMap   : register(t2);
Texture2D gPositionMap : register(t3);

SamplerState gsamLinearClamp : register(s0);

cbuffer cbPass : register(b0)
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

    Light    gLights[MaxLights];
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(uint vertID : SV_VertexID)
{
    float2 uv = float2((vertID << 1) & 2, vertID & 2);

    VertexOut vout;
    vout.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    vout.TexC = uv;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 albedoSample   = gAlbedoMap.Sample(gsamLinearClamp,   pin.TexC);
    float4 normalSample   = gNormalMap.Sample(gsamLinearClamp,   pin.TexC);
    float4 positionSample = gPositionMap.Sample(gsamLinearClamp, pin.TexC);
    
    if (positionSample.a < 0.5f)
        return float4(0.1f, 0.1f, 0.15f, 1.0f);
    
    float3 albedo    = albedoSample.rgb;
    float  roughness = albedoSample.a;
    float3 normalW   = normalize(normalSample.xyz);
    float  fresnelR0 = normalSample.a;
    float3 posW      = positionSample.xyz;
    
    Material mat;
    mat.DiffuseAlbedo = float4(albedo, 1.0f);
    mat.FresnelR0     = float3(fresnelR0, fresnelR0, fresnelR0);
    mat.Shininess     = 1.0f - roughness;

    float3 toEyeW = normalize(gEyePosW - posW);
    
    float4 ambient = gAmbientLight * float4(albedo, 1.0f);
    
    float3 shadowFactor = 1.0f;
    float4 directLight  = ComputeLighting(gLights, mat, posW, normalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;
    litColor.a = 1.0f;
    
    litColor.rgb = litColor.rgb / (litColor.rgb + 1.0f);

    return litColor;
}
