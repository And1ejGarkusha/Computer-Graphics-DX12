Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplaceMap : register(t2);
Texture2D gRoughnessMap : register(t3);
Texture2D gMetallicMap : register(t4);

SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    int gIsChessboard;
    float3 gPad;
};

cbuffer cbPass : register(b1)
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
    float4 gLightsPad[48];
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;

    float gDispScale;
    float gMetallic;
    float AOScale;
    float _padding;
};

static const float gTessMinDist = 0.0f;
static const float gTessMaxDist = 25.0f;
static const float gTessMinFactor = 1.0f;
static const float gTessMaxFactor = 64.0f;

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD0;
    float3 TangentL : TANGENT;
    float2 Color : COLOR;
};

struct VertexOut
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD0;
    float3 PosL : TEXCOORD1;
    float3 NormalL : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
};

struct PatchTess
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

struct DomainOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD0;
    float3 TangentW : TEXCOORD1;
};

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

    float3 pos = vin.PosL;
    
    if (gIsChessboard == 1)
    {
        float speed = 1.0f;
        float maxExtraH = 0.35f;
        float t = abs(sin(gTotalTime * speed));
        float origBottom = -0.05f;
        float origTop = 0.05f;
        float origHeight = 0.10f;

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

    float4 posW = mul(float4(pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3) gWorld));
    vout.TangentW = normalize(mul(vin.TangentL, (float3x3) gWorld));

    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    vout.PosL = pos;
    vout.NormalL = vin.NormalL;

    return vout;
}

PatchTess ConstantHS(InputPatch<VertexOut, 3> patch,
                     uint patchID : SV_PrimitiveID)
{
    PatchTess pt;

    float3 e0mid = (patch[1].PosW + patch[2].PosW) * 0.5f;
    float3 e1mid = (patch[2].PosW + patch[0].PosW) * 0.5f;
    float3 e2mid = (patch[0].PosW + patch[1].PosW) * 0.5f;
    float3 cent = (patch[0].PosW + patch[1].PosW + patch[2].PosW) / 3.0f;

    float d0 = distance(e0mid, gEyePosW);
    float d1 = distance(e1mid, gEyePosW);
    float d2 = distance(e2mid, gEyePosW);
    float dc = distance(cent, gEyePosW);

    float range = gTessMaxDist - gTessMinDist;

    pt.EdgeTess[0] = lerp(gTessMaxFactor, gTessMinFactor, saturate((d0 - gTessMinDist) / range));
    pt.EdgeTess[1] = lerp(gTessMaxFactor, gTessMinFactor, saturate((d1 - gTessMinDist) / range));
    pt.EdgeTess[2] = lerp(gTessMaxFactor, gTessMinFactor, saturate((d2 - gTessMinDist) / range));
    pt.InsideTess = lerp(gTessMaxFactor, gTessMinFactor, saturate((dc - gTessMinDist) / range));

    return pt;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(16.0f)]
VertexOut HS(InputPatch<VertexOut, 3> p,
             uint i : SV_OutputControlPointID,
             uint patchID : SV_PrimitiveID)
{
    return p[i];
}

static const float gWaveAmplitude = 0.18f;
static const float gWaveFreqX = 1.5f;
static const float gWaveFreqZ = 1.2f;
static const float gWaveFreqX2 = 0.9f;
static const float gWaveFreqZ2 = 1.8f;
static const float gWaveSpeedA = 1.1f;
static const float gWaveSpeedB = 0.7f;

[domain("tri")]
DomainOut DS(PatchTess patchTess,
             float3 bary : SV_DomainLocation,
             const OutputPatch<VertexOut, 3> tri)
{
    DomainOut dout;
    
    float3 posL = bary.x * tri[0].PosL + bary.y * tri[1].PosL + bary.z * tri[2].PosL;
    float3 normL = bary.x * tri[0].NormalL + bary.y * tri[1].NormalL + bary.z * tri[2].NormalL;
    float2 texC = bary.x * tri[0].TexC + bary.y * tri[1].TexC + bary.z * tri[2].TexC;
    float3 tanW = bary.x * tri[0].TangentW + bary.y * tri[1].TangentW + bary.z * tri[2].TangentW;

    normL = normalize(normL);
    
    if (gDispScale > 0.0f)
    {
        float height = gDisplaceMap.SampleLevel(gsamLinearWrap, texC, 0).r;
        posL += normL * (height - 0.5f) * gDispScale;
    }
    
    float4 posW = mul(float4(posL, 1.0f), gWorld);
    float3 normW = normalize(mul(normL, (float3x3) gWorld));
    tanW = normalize(tanW);
    
    if (gIsChessboard == 2)
    {
        float wx = posW.x;
        float wz = posW.z;
        float t = gTotalTime;
        
        float phaseA = gWaveFreqX * wx + gWaveFreqZ * wz - gWaveSpeedA * t;
        float phaseB = gWaveFreqX2 * wx + gWaveFreqZ2 * wz - gWaveSpeedB * t * 1.3f;

        float dispY = gWaveAmplitude * (sin(phaseA) + 0.6f * sin(phaseB));
        posW.y += dispY;
        
        float dydx = gWaveAmplitude * (gWaveFreqX * cos(phaseA) + 0.6f * gWaveFreqX2 * cos(phaseB));
        float dydz = gWaveAmplitude * (gWaveFreqZ * cos(phaseA) + 0.6f * gWaveFreqZ2 * cos(phaseB));
        normW = normalize(float3(-dydx, 1.0f, -dydz));
    }

    dout.NormalW = normW;
    dout.TangentW = tanW;
    dout.TexC = texC;
    dout.PosH = mul(float4(posW.xyz, 1.0f), gViewProj);

    return dout;
}

GBufferOut PS(DomainOut pin)
{
    float4 diffuse = gDiffuseMap.Sample(gsamLinearWrap, pin.TexC) * gDiffuseAlbedo;
    float roughness = gRoughnessMap.Sample(gsamLinearWrap, pin.TexC).r * gRoughness;
    float metallic = gMetallicMap.Sample(gsamLinearWrap, pin.TexC).r * gMetallic;

    float3 N = normalize(pin.NormalW);
    float3 T = normalize(pin.TangentW - dot(pin.TangentW, N) * N);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);

    float3 normalSample = gNormalMap.Sample(gsamLinearWrap, pin.TexC).rgb;
    float3 bumpNormal = normalSample * 2.0f - 1.0f;
    float3 normalW = normalize(mul(bumpNormal, TBN));

    GBufferOut gout;
    gout.Albedo = float4(diffuse.rgb, roughness);
    gout.Normal = float4(normalW * 0.5f + 0.5f, metallic);
    return gout;
}
