Texture2D gDiffuseMap : register(t0);
SamplerState gsamLinearWrap : register(s2);

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

struct BillboardVSIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD0;
    float3 TangentL : TANGENT;
    float2 Color : COLOR;
};

struct BillboardVSOut
{
    float3 CenterW : POSITION;
    float2 Size : SIZE;
};

BillboardVSOut VS(BillboardVSIn vin)
{
    BillboardVSOut vout;
    
    vout.CenterW = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
    
    float sx = length(float3(gWorld[0][0], gWorld[1][0], gWorld[2][0]));
    float sy = length(float3(gWorld[0][1], gWorld[1][1], gWorld[2][1]));
    float sz = length(float3(gWorld[0][2], gWorld[1][2], gWorld[2][2]));
    
    float geomRadius = max(length(vin.PosL) * max(sx, max(sy, sz)), 0.5f);
    vout.Size = float2(geomRadius, geomRadius);

    return vout;
}

struct BillboardGSOut
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
    float2 TexC : TEXCOORD0;
    float3 NormalW : NORMAL;
};

[maxvertexcount(4)]
void GS(point BillboardVSOut gin[1],
        inout TriangleStream<BillboardGSOut> triStream)
{
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 look = gEyePosW - gin[0].CenterW;
    look.y = 0.0f;
    float lookLen = length(look);
    if (lookLen < 0.001f)
        look = float3(0.0f, 0.0f, 1.0f);
    else
        look /= lookLen;

    float3 right = cross(up, look);

    float hw = gin[0].Size.x;
    float hh = gin[0].Size.y;

    float4 corners[4];
    corners[0] = float4(gin[0].CenterW + hw * right - hh * up, 1.0f);
    corners[1] = float4(gin[0].CenterW + hw * right + hh * up, 1.0f);
    corners[2] = float4(gin[0].CenterW - hw * right - hh * up, 1.0f);
    corners[3] = float4(gin[0].CenterW - hw * right + hh * up, 1.0f);

    float2 uvs[4] =
    {
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f)
    };

    BillboardGSOut gout;
    gout.NormalW = look;
    gout.Color = gDiffuseAlbedo;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        gout.PosH = mul(corners[i], gViewProj);
        gout.TexC = uvs[i];
        triStream.Append(gout);
    }
}

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

GBufferOut PS(BillboardGSOut pin)
{
    float4 diffuse = gDiffuseMap.Sample(gsamLinearWrap, pin.TexC) * pin.Color;
    clip(diffuse.a - 0.1f);

    float3 n = normalize(pin.NormalW);

    GBufferOut gout;
    gout.Albedo = float4(diffuse.rgb, gRoughness);
    gout.Normal = float4(n * 0.5f + 0.5f, gMetallic);
    return gout;
}
