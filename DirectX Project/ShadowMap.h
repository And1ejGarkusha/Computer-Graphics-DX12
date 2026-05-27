#pragma once
#include "d3dUtil.h"
#include "d3dx12.h"
#include "FrameResource.h"

class ShadowMap
{
public:
    static const UINT        Resolution = 2048;
    static const DXGI_FORMAT TexFormat = DXGI_FORMAT_R32_TYPELESS;
    static const DXGI_FORMAT DSVFormat = DXGI_FORMAT_D32_FLOAT;
    static const DXGI_FORMAT SRVFormat = DXGI_FORMAT_R32_FLOAT;

    void Create(
        ID3D12Device* device,
        ID3D12DescriptorHeap* srvHeap,
        UINT                  srvBaseOffset,
        UINT                  srvDescSize);

    void TransitionToDepthWrite(ID3D12GraphicsCommandList* cmdList);
    void TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList);

    void BeginCascade(ID3D12GraphicsCommandList* cmdList, int cascade);

    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const { return mSRVGPUHandle; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource>       mTexture;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    CD3DX12_CPU_DESCRIPTOR_HANDLE                mDSVHandles[NUM_CASCADES] = {};
    CD3DX12_GPU_DESCRIPTOR_HANDLE                mSRVGPUHandle = {};
};

inline void ShadowMap::Create(
    ID3D12Device* device,
    ID3D12DescriptorHeap* srvHeap,
    UINT                  srvBaseOffset,
    UINT                  srvDescSize)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = Resolution;
    texDesc.Height = Resolution;
    texDesc.DepthOrArraySize = NUM_CASCADES;
    texDesc.MipLevels = 1;
    texDesc.Format = TexFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE cv = {};
    cv.Format = DSVFormat;
    cv.DepthStencil.Depth = 1.0f;
    cv.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &cv, IID_PPV_ARGS(mTexture.GetAddressOf())));
    mTexture->SetName(L"CSM Texture2DArray");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = NUM_CASCADES;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
    mDsvHeap->SetName(L"CSM DSV Heap");

    UINT dsvDescSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvStart(
        mDsvHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        mDSVHandles[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvStart, i, dsvDescSize);

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DSVFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = (UINT)i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        device->CreateDepthStencilView(mTexture.Get(), &dsvDesc, mDSVHandles[i]);
    }

    mSRVGPUHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        srvHeap->GetGPUDescriptorHandleForHeapStart(), srvBaseOffset, srvDescSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvCPU(
        srvHeap->GetCPUDescriptorHandleForHeapStart(), srvBaseOffset, srvDescSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = SRVFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = NUM_CASCADES;
    srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(mTexture.Get(), &srvDesc, srvCPU);
}

inline void ShadowMap::TransitionToDepthWrite(ID3D12GraphicsCommandList* cmdList)
{
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(
        mTexture.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmdList->ResourceBarrier(1, &b);
}

inline void ShadowMap::TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList)
{
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(
        mTexture.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &b);
}

inline void ShadowMap::BeginCascade(ID3D12GraphicsCommandList* cmdList, int cascade)
{
    assert(cascade >= 0 && cascade < NUM_CASCADES);
    cmdList->ClearDepthStencilView(
        mDSVHandles[cascade], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)Resolution, (float)Resolution, 0.f, 1.f };
    D3D12_RECT     sr = { 0, 0, (LONG)Resolution, (LONG)Resolution };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    cmdList->OMSetRenderTargets(0, nullptr, FALSE, &mDSVHandles[cascade]);
}