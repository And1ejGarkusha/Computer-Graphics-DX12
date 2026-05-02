#pragma once
#include "d3dUtil.h"
#include "d3dx12.h"
#include "FrameResource.h"
#include "GBuffer.h"
#include <array>

class RenderingSystem
{
public:
    void Initialize(ID3D12Device* device,
        DXGI_FORMAT            backBufferFormat,
        DXGI_FORMAT            depthStencilFormat,
        UINT                   width,
        UINT                   height,
        UINT                   rtvDescSize,
        UINT                   cbvSrvDescSize,
        ID3D12DescriptorHeap* srvHeap,
        UINT                   gbufferSrvBaseSlot,
        ID3D12RootSignature* geometryRootSig);

    void OnResize(ID3D12Device* device,
        UINT                   width,
        UINT                   height,
        ID3D12DescriptorHeap* srvHeap,
        UINT                   gbufferSrvBaseSlot);

    void BeginGeometryPass(ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissor);

    void EndGeometryPass(ID3D12GraphicsCommandList* cmdList);

    void BeginLightingPass(ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissor);

    void DrawLightingPass(ID3D12GraphicsCommandList* cmdList,
        ID3D12DescriptorHeap* srvHeap,
        D3D12_GPU_VIRTUAL_ADDRESS  passCBAddress);

    ID3D12RootSignature* GetLightingRootSig()   const { return mLightingRootSig.Get(); }
    ID3D12PipelineState* GetGeometryPSO()       const { return mGeometryPSO.Get(); }
    ID3D12PipelineState* GetLightingPSO()       const { return mLightingPSO.Get(); }
    const GBuffer& GetGBuffer()           const { return mGBuffer; }

private:
    void BuildRtvHeap(ID3D12Device* device);
    void BuildLightingRootSignature(ID3D12Device* device);
    void BuildPSOs(ID3D12Device* device,
        DXGI_FORMAT            backBufferFormat,
        DXGI_FORMAT            depthStencilFormat,
        ID3D12RootSignature* geometryRootSig);

    GBuffer  mGBuffer;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mGBufferRtvHeap;

    Microsoft::WRL::ComPtr<ID3D12RootSignature>  mLightingRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  mGeometryPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  mLightingPSO;

    UINT mRtvDescSize = 0;
    UINT mSrvDescSize = 0;
    UINT mSrvBaseSlot = 0;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mGeometryInputLayout;
};