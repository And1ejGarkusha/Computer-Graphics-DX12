#pragma once
#include "d3dUtil.h"
#include "d3dx12.h"

class GBuffer
{
public:
    static const int         NumRTs = 2;
    static const DXGI_FORMAT AlbedoFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static const DXGI_FORMAT NormalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    void Create(ID3D12Device* device,
        UINT                   width,
        UINT                   height,
        ID3D12DescriptorHeap* rtvHeap,
        UINT                   rtvBaseOffset,
        ID3D12DescriptorHeap* srvHeap,
        UINT                   srvBaseOffset,
        UINT                   rtvDescSize,
        UINT                   srvDescSize)
    {
        mWidth = width;
        mHeight = height;

        CreateBuffers(device, width, height);
        CreateViews(device, rtvHeap, rtvBaseOffset, srvHeap, srvBaseOffset,
            rtvDescSize, srvDescSize);
    }

    void Resize(ID3D12Device* device,
        UINT                   width,
        UINT                   height,
        ID3D12DescriptorHeap* rtvHeap,
        UINT                   rtvBaseOffset,
        ID3D12DescriptorHeap* srvHeap,
        UINT                   srvBaseOffset,
        UINT                   rtvDescSize,
        UINT                   srvDescSize)
    {
        mAlbedoBuffer.Reset();
        mNormalBuffer.Reset();
        Create(device, width, height, rtvHeap, rtvBaseOffset,
            srvHeap, srvBaseOffset, rtvDescSize, srvDescSize);
    }

    void TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList)
    {
        D3D12_RESOURCE_BARRIER barriers[NumRTs] = {
            CD3DX12_RESOURCE_BARRIER::Transition(mAlbedoBuffer.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(mNormalBuffer.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
        };
        cmdList->ResourceBarrier(NumRTs, barriers);
    }

    void TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList)
    {
        D3D12_RESOURCE_BARRIER barriers[NumRTs] = {
            CD3DX12_RESOURCE_BARRIER::Transition(mAlbedoBuffer.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(mNormalBuffer.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        };
        cmdList->ResourceBarrier(NumRTs, barriers);
    }

    void Clear(ID3D12GraphicsCommandList* cmdList)
    {
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        cmdList->ClearRenderTargetView(mAlbedoRTV, clearColor, 0, nullptr);
        cmdList->ClearRenderTargetView(mNormalRTV, clearColor, 0, nullptr);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetAlbedoRTV()   const { return mAlbedoRTV; }
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetNormalRTV()   const { return mNormalRTV; }

    CD3DX12_GPU_DESCRIPTOR_HANDLE GetSRVGPUHandle() const { return mSRVGPUHandle; }

    UINT Width()  const { return mWidth; }
    UINT Height() const { return mHeight; }

private:
    void CreateBuffers(ID3D12Device* device, UINT width, UINT height)
    {
        auto CreateRT = [&](DXGI_FORMAT fmt, Microsoft::WRL::ComPtr<ID3D12Resource>& res)
            {
                D3D12_RESOURCE_DESC desc = {};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = width;
                desc.Height = height;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = fmt;
                desc.SampleDesc.Count = 1;
                desc.SampleDesc.Quality = 0;
                desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

                D3D12_CLEAR_VALUE cv;
                cv.Format = fmt;
                cv.Color[0] = cv.Color[1] = cv.Color[2] = cv.Color[3] = 0.0f;

                CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
                ThrowIfFailed(device->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
                    IID_PPV_ARGS(res.GetAddressOf())));
            };

        CreateRT(AlbedoFormat, mAlbedoBuffer);
        CreateRT(NormalFormat, mNormalBuffer);
    }

    void CreateViews(ID3D12Device* device,
        ID3D12DescriptorHeap* rtvHeap,
        UINT                   rtvBase,
        ID3D12DescriptorHeap* srvHeap,
        UINT                   srvBase,
        UINT                   rtvDescSize,
        UINT                   srvDescSize)
    {
        auto rtvStart = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtvHeap->GetCPUDescriptorHandleForHeapStart());

        mAlbedoRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, rtvBase + 0, rtvDescSize);
        mNormalRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, rtvBase + 1, rtvDescSize);

        auto MakeRTV = [&](ID3D12Resource* res, DXGI_FORMAT fmt,
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
            {
                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
                rtvDesc.Format = fmt;
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                device->CreateRenderTargetView(res, &rtvDesc, handle);
            };

        MakeRTV(mAlbedoBuffer.Get(), AlbedoFormat, mAlbedoRTV);
        MakeRTV(mNormalBuffer.Get(), NormalFormat, mNormalRTV);

        auto srvCpuStart = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            srvHeap->GetCPUDescriptorHandleForHeapStart());
        mSRVGPUHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            srvHeap->GetGPUDescriptorHandleForHeapStart(), srvBase, srvDescSize);

        auto MakeSRV = [&](ID3D12Resource* res, DXGI_FORMAT fmt, UINT slot)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Format = fmt;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;

                CD3DX12_CPU_DESCRIPTOR_HANDLE handle(srvCpuStart, srvBase + slot, srvDescSize);
                device->CreateShaderResourceView(res, &srvDesc, handle);
            };

        MakeSRV(mAlbedoBuffer.Get(), AlbedoFormat, 0);
        MakeSRV(mNormalBuffer.Get(), NormalFormat, 1);
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> mAlbedoBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mNormalBuffer;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mAlbedoRTV;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mNormalRTV;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mSRVGPUHandle;

    UINT mWidth = 0;
    UINT mHeight = 0;
};