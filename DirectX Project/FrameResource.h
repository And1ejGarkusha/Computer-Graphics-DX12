#pragma once

#include "d3dUtil.h"
#include "MathHelper.h"
#include "UploadBuffer.h"

static const int NUM_CASCADES = 4;

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    int IsChessboard = 0;
    float Pad[3];
};

struct ShadowPassConstants
{
    DirectX::XMFLOAT4X4 LightViewProj = MathHelper::Identity4x4();
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3   EyePosW = { 0.0f, 0.0f, 0.0f };
    float               cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2   RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2   InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

    Light Lights[MaxLights];

    DirectX::XMFLOAT4X4 LightViewProj[NUM_CASCADES];
    DirectX::XMFLOAT4   CascadeSplits = { 0.f, 0.f, 0.f, 0.f };
};

struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 Tangent;
    DirectX::XMFLOAT2 Color;
};

struct FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    std::unique_ptr<UploadBuffer<PassConstants>>       PassCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>>   MaterialCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>>     ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<ShadowPassConstants>> ShadowPassCB = nullptr;

    UINT64 Fence = 0;
};