#pragma once
#include "d3dUtil.h"
#include "d3dx12.h"

class PostProcessPass
{
public:
    void Initialize(ID3D12Device* device,
        DXGI_FORMAT backBufferFormat,
        UINT width, UINT height,
        UINT cbvSrvDescSize);

    void OnResize(ID3D12Device* device, UINT width, UINT height);

    void BeginPostProcess(ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissor);

    void DrawPostProcess(ID3D12GraphicsCommandList* cmdList,
        ID3D12DescriptorHeap* srvHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
        D3D12_GPU_VIRTUAL_ADDRESS postProcessCBAddress);

    ID3D12RootSignature* GetRootSig() const { return mRootSig.Get(); }
    ID3D12PipelineState* GetPSO() const { return mPSO.Get(); }

private:
    void BuildRootSignature(ID3D12Device* device);
    void BuildPSO(ID3D12Device* device, DXGI_FORMAT backBufferFormat);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;

    UINT mWidth = 0, mHeight = 0;
};