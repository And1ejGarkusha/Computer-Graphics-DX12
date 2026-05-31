Texture2D gDiffuseMap   : register(t0);
Texture2D gNormalMap    : register(t1);
Texture2D gRoughnessMap : register(t3);
Texture2D gMetallicMap  : register(t4);

SamplerState gsamLinearWrap  : register(s2);
SamplerState gsamLinearClamp : register(s3);

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
    float    gDispScale;
    float    gMetallic;
    float    AOScale;
    float    _padding;
};

struct VertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD0;
    float3 TangentL: TANGENT;
    float2 Color   : COLOR;
};

struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float3 NormalW : NORMAL;
    float2 TexC    : TEXCOORD0;
    float3 TangentW: TEXCOORD1;
};

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH    = mul(posW, gViewProj);
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3)gWorld));
    vout.TangentW= normalize(mul(vin.TangentL, (float3x3)gWorld));

    float4 texC  = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC    = mul(texC, gMatTransform).xy;

    return vout;
}

GBufferOut PS(VertexOut pin)
{
    float4 diffuse   = gDiffuseMap.Sample(gsamLinearWrap, pin.TexC) * gDiffuseAlbedo;
    float  roughness = gRoughnessMap.Sample(gsamLinearWrap, pin.TexC).r * gRoughness;
    float  metallic  = gMetallicMap.Sample(gsamLinearWrap, pin.TexC).r  * gMetallic;

    float3 N = normalize(pin.NormalW);
    float3 T = normalize(pin.TangentW - dot(pin.TangentW, N) * N);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);

    float3 normalSample = gNormalMap.Sample(gsamLinearWrap, pin.TexC).rgb;
    float3 bumpNormal   = normalSample * 2.0f - 1.0f;
    float3 normalW      = normalize(mul(bumpNormal, TBN));

    GBufferOut gout;
    gout.Albedo = float4(diffuse.rgb, roughness);
    gout.Normal = float4(normalW * 0.5f + 0.5f, metallic);
    return gout;
}
