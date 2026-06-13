Texture2D gSceneColor : register(t0);
Texture2D gDepthMap : register(t1);

SamplerState gsamLinearClamp : register(s0);

cbuffer cbPostProcess : register(b0)
{
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gTotalTime;
    float gGamma;
    int gEdgeDetection;
    int gVCRFilter;
    float gNearZ;
    float gFarZ;
    float3 gPad;
}

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

float ComputeSobelEdge(float2 uv, Texture2D depthMap, float2 invSize, float nearZ, float farZ)
{
    float2 d = invSize;
    float lin[3][3];
    [unroll]
    for (int dy = 0; dy < 3; ++dy)
    [unroll]
        for (int dx = 0; dx < 3; ++dx)
        {
            float2 off = float2(dx - 1, dy - 1) * d;
            float depth01 = depthMap.Sample(gsamLinearClamp, uv + off);
            lin[dy][dx] = (nearZ * farZ) / (farZ - depth01 * (farZ - nearZ));
        }

    float gx = -lin[0][0] + lin[0][2] - 2 * lin[1][0] + 2 * lin[1][2] - lin[2][0] + lin[2][2];
    float gy = -lin[0][0] - 2 * lin[0][1] - lin[0][2] + lin[2][0] + 2 * lin[2][1] + lin[2][2];
    float edge = sqrt(gx * gx + gy * gy);

    float centerDepth = max(lin[1][1], 0.001f);
    edge = saturate(edge / (centerDepth * 0.1f));
    return edge;
}

float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float FilmGrain(float2 uv, float2 renderTargetSize, float time)
{
    float frame = floor(time * 24.0f);
    return Hash21(uv * renderTargetSize + frame) * 2.0f - 1.0f;
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
    
    float4 sceneColor;
    float depth = gDepthMap.Sample(gsamLinearClamp, uvGeo);
    
    if (gVCRFilter)
    {
        float r = gSceneColor.Sample(gsamLinearClamp, uvFX + chromaOffset).r;
        float g = gSceneColor.Sample(gsamLinearClamp, uvFX).g;
        float b = gSceneColor.Sample(gsamLinearClamp, uvFX - chromaOffset).b;
        float a = gSceneColor.Sample(gsamLinearClamp, uvFX).a;
        sceneColor = float4(r, g, b, a);
    }
    else
    {
        sceneColor = gSceneColor.Sample(gsamLinearClamp, uvGeo);
    }

    if (depth >= 1.f)
        return float4(0.1f, 0.1f, 0.15f, 1.f);

    // Edge detection
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
                edge = max(edge, ComputeSobelEdge(uvGeo + off, gDepthMap,
                           gInvRenderTargetSize, gNearZ, gFarZ));
            }
        sceneColor.rgb = lerp(sceneColor.rgb, float3(0.f, 0.f, 0.f), edge * 0.97f);
    }

    // VCR
    if (gVCRFilter)
    {
        float bandA = sin((uvFX.y * 6.0f - gTotalTime * 0.5f) * 6.28318f) * 0.5f + 0.5f;
        float bandB = sin((uvFX.y * 18.0f + gTotalTime * 1.1f) * 6.28318f) * 0.5f + 0.5f;
        sceneColor.rgb *= lerp(0.75f, 1.0f, lerp(bandA, bandB, 0.4f));
        sceneColor.rgb += FilmGrain(pin.TexC, gRenderTargetSize, gTotalTime) * 0.035f;
        sceneColor.rgb *= float3(0.96f, 0.98f, 1.04f);
        float2 fc = pin.TexC - 0.5f;
        sceneColor.rgb *= saturate(1.0f - dot(fc, fc) * 1.2f);
    }

    // Gamma correction
    sceneColor.rgb = pow(max(sceneColor.rgb, 0.0001f), 1.0f / gGamma);

    return sceneColor;
}