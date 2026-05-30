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

    int gEdgeDetection;
    int gVCRFilter;
    int2 gPostFXPad;
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

//Edge detection (Sobel)
float ComputeSobelEdge(float2 uv)
{
    float2 d = gInvRenderTargetSize;
    float lin[3][3];
    [unroll]
    for (int dy = 0; dy < 3; ++dy)
    [unroll]
        for (int dx = 0; dx < 3; ++dx)
        {
            float2 off = float2(dx - 1, dy - 1) * d;
            float depth01 = gDepthMap.Sample(gsamLinearClamp, uv + off);
            lin[dy][dx] = (gNearZ * gFarZ) / (gFarZ - depth01 * (gFarZ - gNearZ));
        }

    float gx = -lin[0][0] + lin[0][2] - 2 * lin[1][0] + 2 * lin[1][2] - lin[2][0] + lin[2][2];
    float gy = -lin[0][0] - 2 * lin[0][1] - lin[0][2] + lin[2][0] + 2 * lin[2][1] + lin[2][2];
    float edge = sqrt(gx * gx + gy * gy);

    float centerDepth = max(lin[1][1], 0.001f);
    edge = saturate(edge / (centerDepth * 0.1f));
    return edge;
}

//VCR helpers
float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float FilmGrain(float2 uv, float time)
{
    float frame = floor(time * 24.0f);
    return Hash21(uv * gRenderTargetSize + frame) * 2.0f - 1.0f;
}

float VCRDistortX(float2 uv, float time)
{
    float slowBand = floor(uv.y * 8.0f + time * 0.8f);
    float slowRand = Hash21(float2(slowBand, floor(time * 2.0f)));
    float slowShift = (slowRand - 0.5f) * 0.03f;

    float fastBand = floor(uv.y * 60.0f + time * 3.0f);
    float fastRand = Hash21(float2(fastBand, floor(time * 8.0f)));
    float fastShift = (fastRand > 0.88f) ? (fastRand - 0.88f) * 0.18f : 0.0f;

    return slowShift + fastShift;
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 uvGeo = pin.TexC;
    float2 uvFX = pin.TexC;

    float2 chromaOffset = float2(0, 0);
    if (gVCRFilter)
    {
        uvFX.x = frac(uvFX.x + VCRDistortX(uvFX, gTotalTime));
        float2 fromCenter = uvFX - 0.5f;
        float strength = length(fromCenter) * 0.006f + 0.0015f;
        chromaOffset = float2(strength, 0.0f);
    }
    
    float4 albedoSample;
    float4 normalSample = gNormalMap.Sample(gsamLinearClamp, uvGeo);
    float depth = gDepthMap.Sample(gsamLinearClamp, uvGeo);

    if (gVCRFilter)
    {
        float r = gAlbedoMap.Sample(gsamLinearClamp, uvFX + chromaOffset).r;
        float g = gAlbedoMap.Sample(gsamLinearClamp, uvFX).g;
        float b = gAlbedoMap.Sample(gsamLinearClamp, uvFX - chromaOffset).b;
        float a = gAlbedoMap.Sample(gsamLinearClamp, uvFX).a;
        albedoSample = float4(r, g, b, a);
    }
    else
    {
        albedoSample = gAlbedoMap.Sample(gsamLinearClamp, uvGeo);
    }

    //фон
    if (depth >= 1.f)
        return float4(0.1f, 0.1f, 0.15f, 1.f);

    //Восстановление позиции из глубины
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
    float4 directLight = ComputeLighting(gLights, mat, posW, N, V, shadowFactor);

    //IBL ambient
    float3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    //Diffuse IBL (irradiance map)
    float3 irradiance = gIrradianceMap.Sample(gsamLinearWrap, N).rgb;
    float3 diffuseIBL = kD * irradiance * albedo;

    //Specular IBL
    float3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 7.0f;
    float3 prefilteredColor = gPrefilterMap.SampleLevel(
        gsamLinearWrap, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdfUV = float2(max(dot(N, V), 0.0f), roughness);
    float2 brdfScale = gBrdfLUT.Sample(gsamLinearClamp, brdfUV).rg;
    float3 specularIBL = prefilteredColor * (F0 * brdfScale.x + brdfScale.y);

    float3 ambient = diffuseIBL + specularIBL;
    
    float4 litColor = float4(ambient, 1.f) + directLight;
    litColor.a = 1.f;
    
    litColor.rgb = litColor.rgb / (litColor.rgb + 1.f);

    //Edge detection
    if (gEdgeDetection)
    {
        const int radius = 1;
        float edge = 0.0f;
        [unroll]
        for (int ey = -radius; ey <= radius; ++ey)
        [unroll]
            for (int ex = -radius; ex <= radius; ++ex)
            {
                float2 off = float2(ex, ey) * gInvRenderTargetSize;
                edge = max(edge, ComputeSobelEdge(uvGeo + off));
            }
        litColor.rgb = lerp(litColor.rgb, float3(0.f, 0.f, 0.f), edge * 0.97f);
    }

    //VCR
    if (gVCRFilter)
    {
        float bandA = sin((uvFX.y * 6.0f - gTotalTime * 0.5f) * 6.28318f) * 0.5f + 0.5f;
        float bandB = sin((uvFX.y * 18.0f + gTotalTime * 1.1f) * 6.28318f) * 0.5f + 0.5f;
        litColor.rgb *= lerp(0.75f, 1.0f, lerp(bandA, bandB, 0.4f));
        litColor.rgb += FilmGrain(pin.TexC, gTotalTime) * 0.035f;
        litColor.rgb *= float3(0.96f, 0.98f, 1.04f);
        float2 fc = pin.TexC - 0.5f;
        litColor.rgb *= saturate(1.0f - dot(fc, fc) * 1.2f);
    }

    //Gamma correction
    litColor.rgb = pow(max(litColor.rgb, 0.0001f), 1.0f / gGamma);

    return litColor;
}
