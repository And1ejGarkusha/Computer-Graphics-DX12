#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 1
#endif
#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 1
#endif
#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 1
#endif
#ifndef NUM_CASCADES
#define NUM_CASCADES 4
#endif

#ifndef PCF_HALF
#define PCF_HALF 2
#endif

static const float gShadowTexelSize = 1.0f / 2048.0f;

#include "LightingUtil.hlsl"

Texture2D gAlbedoMap : register(t1);
Texture2D gNormalMap : register(t2);
Texture2D<float> gDepthMap : register(t3);

Texture2DArray gShadowMap : register(t4);

SamplerState gsamLinearClamp : register(s0);
SamplerComparisonState gsamShadow : register(s1);

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;

    float4 gAmbientLight;

    Light gLights[MaxLights];
    
    float4x4 gLightViewProj[NUM_CASCADES];
    float4 gCascadeSplits;
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
    vout.PosH = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
    vout.TexC = uv;
    return vout;
}

int SelectCascade(float viewDepth)
{
    float splits[NUM_CASCADES];
    splits[0] = gCascadeSplits.x;
    splits[1] = gCascadeSplits.y;
    splits[2] = gCascadeSplits.z;
    splits[3] = gCascadeSplits.w;

    for (int i = 0; i < NUM_CASCADES - 1; ++i)
        if (viewDepth < splits[i])
            return i;
    return NUM_CASCADES - 1;
}

float CalcShadowFactor(float3 posW)
{
    float4 posV = mul(float4(posW, 1.f), gView);
    float viewZ = abs(posV.z);
    int cascade = SelectCascade(viewZ);
    
    float4 posL = mul(float4(posW, 1.f), gLightViewProj[cascade]);
    posL /= posL.w;
    
    float2 uv;
    uv.x = posL.x * 0.5f + 0.5f;
    uv.y = -posL.y * 0.5f + 0.5f;
    float depth = posL.z;
    
    if (uv.x < 0.f || uv.x > 1.f || uv.y < 0.f || uv.y > 1.f)
        return 1.0f;
    
    const float bias = 0.003f;
    float compareDepth = depth - bias;
    
    float shadow = 0.f;
    [unroll]
    for (int dy = -PCF_HALF; dy <= PCF_HALF; ++dy)
    {
        [unroll]
        for (int dx = -PCF_HALF; dx <= PCF_HALF; ++dx)
        {
            float2 offset = float2((float) dx, (float) dy) * gShadowTexelSize;
            shadow += gShadowMap.SampleCmpLevelZero(
                gsamShadow,
                float3(uv + offset, (float) cascade),
                compareDepth);
        }
    }

    const float taps = (float) ((2 * PCF_HALF + 1) * (2 * PCF_HALF + 1));
    return shadow / taps;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 albedoSample = gAlbedoMap.Sample(gsamLinearClamp, pin.TexC);
    float4 normalSample = gNormalMap.Sample(gsamLinearClamp, pin.TexC);
    float depth = gDepthMap.Sample(gsamLinearClamp, pin.TexC);
    
    if (depth >= 1.f)
        return float4(0.1f, 0.1f, 0.15f, 1.f);
    
    float2 ndcXY = pin.TexC * float2(2.f, -2.f) + float2(-1.f, 1.f);
    float4 ndcPos = float4(ndcXY, depth, 1.f);
    float4 worldPos = mul(ndcPos, gInvViewProj);
    float3 posW = worldPos.xyz / worldPos.w;
    
    float3 albedo = albedoSample.rgb;
    float roughness = albedoSample.a;
    float3 normalW = normalize(normalSample.xyz);
    float fresnelR0 = normalSample.a;

    Material mat;
    mat.DiffuseAlbedo = float4(albedo, 1.f);
    mat.FresnelR0 = float3(fresnelR0, fresnelR0, fresnelR0);
    mat.Shininess = 1.f - roughness;

    float3 toEyeW = normalize(gEyePosW - posW);
    float4 ambient = gAmbientLight * float4(albedo, 1.f);
    
    float3 shadowFactor = float3(CalcShadowFactor(posW), 1.f, 1.f);

    float4 directLight = ComputeLighting(gLights, mat, posW, normalW, toEyeW, shadowFactor);
    float4 litColor = ambient + directLight;
    litColor.a = 1.f;
    
    litColor.rgb = litColor.rgb / (litColor.rgb + 1.f);

    return litColor;
}
