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
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissor);

    void EndLightingPass(ID3D12GraphicsCommandList* cmdList);

    void DrawLightingPass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12DescriptorHeap* srvHeap,
        D3D12_GPU_VIRTUAL_ADDRESS   passCBAddress,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE shadowSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE prefilterSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE spotLightBufSRV);

    void ToggleWireframe() { mWireframeMode = !mWireframeMode; }
    bool IsWireframe()     const { return mWireframeMode; }

    ID3D12RootSignature* GetLightingRootSig()  const { return mLightingRootSig.Get(); }
    ID3D12PipelineState* GetGeometryPSO()      const { return mGeometryPSO.Get(); }
    ID3D12PipelineState* GetGeometryWireframePSO() const { return mGeometryWireframePSO.Get(); }
    ID3D12PipelineState* GetGeometryLOD1PSO()  const { return mGeometryLOD1PSO.Get(); }
    ID3D12PipelineState* GetBillboardPSO()     const { return mBillboardPSO.Get(); }
    ID3D12PipelineState* GetLightingPSO()      const { return mLightingPSO.Get(); }
    const GBuffer& GetGBuffer()                const { return mGBuffer; }

    ID3D12Resource* GetHDRColorBuffer() const { return mHDRColorBuffer.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetHDRColorRTV() const { return mHDRColorRTV; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetHDRColorSRV() const { return mHDRColorSRV_GPU; }

private:
    void BuildRtvHeap(ID3D12Device* device);
    void BuildLightingRootSignature(ID3D12Device* device);
    void BuildPSOs(ID3D12Device* device,
        DXGI_FORMAT            backBufferFormat,
        DXGI_FORMAT            depthStencilFormat,
        ID3D12RootSignature* geometryRootSig);
    void BuildHDRColorBuffer(ID3D12Device* device, UINT width, UINT height);
    void BuildHDRDescriptorHeaps(ID3D12Device* device);

    GBuffer  mGBuffer;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mGBufferRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  mLightingRootSig;

    Microsoft::WRL::ComPtr<ID3D12PipelineState>  mGeometryPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  mGeometryWireframePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  mGeometryLOD1PSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  mBillboardPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  mLightingPSO;

    bool mWireframeMode = false;

    UINT mRtvDescSize = 0;
    UINT mSrvDescSize = 0;
    UINT mSrvBaseSlot = 0;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mGeometryInputLayout;

    Microsoft::WRL::ComPtr<ID3D12Resource> mHDRColorBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHDRRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHDRSrvHeapGPU;
    D3D12_CPU_DESCRIPTOR_HANDLE mHDRColorRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE mHDRColorSRV_CPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE mHDRColorSRV_GPU = {};
};