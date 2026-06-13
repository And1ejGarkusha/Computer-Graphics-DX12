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

TextureCube gIrradianceMap : register(t5);
TextureCube gPrefilterMap : register(t6);
Texture2D gBrdfLUT : register(t7);
StructuredBuffer<Light> gFiredSpotLights : register(t8);

SamplerState gsamLinearClamp : register(s0);
SamplerComparisonState gsamShadow : register(s1);
SamplerState gsamLinearWrap : register(s2);

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

    float gGamma;
    float3 gGammaPad;

    int gNumActiveSpotLights;
    float3 gPostFXPad;
    
    int gEdgeDetection;
    int gVCRFilter;
    int gPostFXPad2;
    int gPostFXPad3;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(uint vertID : SV_VertexID)
{
    float2 uv = float2((vertID << 1) & 2, vertID & 2);
    VertexOut o;
    o.PosH = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
    o.TexC = uv;
    return o;
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
    int cascade = SelectCascade(abs(posV.z));
    float4 posL = mul(float4(posW, 1.f), gLightViewProj[cascade]);
    posL /= posL.w;

    float2 uv = float2(posL.x * 0.5f + 0.5f, -posL.y * 0.5f + 0.5f);
    if (any(uv < 0.f) || any(uv > 1.f))
        return 1.0f;

    float shadow = 0.f;
    [unroll]
    for (int dy = -PCF_HALF; dy <= PCF_HALF; ++dy)
    [unroll]
        for (int dx = -PCF_HALF; dx <= PCF_HALF; ++dx)
        {
            float2 off = float2(dx, dy) * gShadowTexelSize;
            shadow += gShadowMap.SampleCmpLevelZero(
            gsamShadow, float3(uv + off, (float) cascade), posL.z - 0.003f);
        }
    const float taps = (float) ((2 * PCF_HALF + 1) * (2 * PCF_HALF + 1));
    return shadow / taps;
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 uvGeo = pin.TexC;
    
    float4 albedoSample = gAlbedoMap.Sample(gsamLinearClamp, uvGeo);
    float4 normalSample = gNormalMap.Sample(gsamLinearClamp, uvGeo);
    float depth = gDepthMap.Sample(gsamLinearClamp, uvGeo);

    if (depth >= 1.f)
        return float4(0.1f, 0.1f, 0.15f, 1.f);

    float2 ndcXY = uvGeo * float2(2.f, -2.f) + float2(-1.f, 1.f);
    float4 ndcPos = float4(ndcXY, depth, 1.f);
    float4 wpRaw = mul(ndcPos, gInvViewProj);
    float3 posW = wpRaw.xyz / wpRaw.w;
    
    float3 albedo = albedoSample.rgb;
    float roughness = albedoSample.a;
    float3 normalW = normalize(normalSample.xyz * 2.0f - 1.0f);
    float metallic = normalSample.a;

    float3 V = normalize(gEyePosW - posW);
    float3 N = normalW;
    
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    
    Material mat;
    mat.DiffuseAlbedo = float4(albedo, 1.f);
    mat.FresnelR0 = F0;
    mat.Roughness = roughness;
    mat.Metallic = metallic;

    float3 shadowFactor = float3(CalcShadowFactor(posW), 1.f, 1.f);
    
    float3 directResult = 0.0f;
    for (int di = 0; di < NUM_DIR_LIGHTS; ++di)
        directResult += shadowFactor[di] * ComputeDirectionalLight(gLights[di], mat, N, V);
    for (int pi = NUM_DIR_LIGHTS; pi < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++pi)
        directResult += ComputePointLight(gLights[pi], mat, posW, N, V);
    int spotBase = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS;
    directResult += ComputeSpotLight(gLights[spotBase], mat, posW, N, V);
    for (int si = 0; si < gNumActiveSpotLights; ++si)
        directResult += ComputeSpotLight(gFiredSpotLights[si], mat, posW, N, V);

    float4 directLight = float4(directResult, 0.0f);

    float3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    float3 irradiance = gIrradianceMap.Sample(gsamLinearWrap, N).rgb;
    float3 diffuseIBL = kD * irradiance * albedo;

    float3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 7.0f;
    float3 prefilteredColor = gPrefilterMap.SampleLevel(
        gsamLinearWrap, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdfUV = float2(max(dot(N, V), 0.0f), roughness);
    float2 brdfScale = gBrdfLUT.Sample(gsamLinearClamp, brdfUV).rg;
    float3 specularIBL = prefilteredColor * (kS * brdfScale.x + brdfScale.y);

    float3 ambient = diffuseIBL + specularIBL;
    
    float4 litColor = float4(ambient, 1.f) + directLight;
    litColor.a = 1.f;
    
    litColor.rgb = litColor.rgb / (litColor.rgb + 1.f);
    
    return litColor;
}