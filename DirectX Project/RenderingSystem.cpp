#include "RenderingSystem.h"
#include "d3dUtil.h"

void RenderingSystem::Initialize(
    ID3D12Device* device,
    DXGI_FORMAT            backBufferFormat,
    DXGI_FORMAT            depthStencilFormat,
    UINT                   width,
    UINT                   height,
    UINT                   rtvDescSize,
    UINT                   cbvSrvDescSize,
    ID3D12DescriptorHeap* srvHeap,
    UINT                   gbufferSrvBaseSlot,
    ID3D12RootSignature* geometryRootSig)
{
    mRtvDescSize = rtvDescSize;
    mSrvDescSize = cbvSrvDescSize;
    mSrvBaseSlot = gbufferSrvBaseSlot;

    BuildRtvHeap(device);

    mGBuffer.Create(device, width, height,
        mGBufferRtvHeap.Get(), 0,
        srvHeap, gbufferSrvBaseSlot,
        rtvDescSize, cbvSrvDescSize);

    BuildLightingRootSignature(device);
    BuildPSOs(device, backBufferFormat, depthStencilFormat, geometryRootSig);
}

void RenderingSystem::OnResize(
    ID3D12Device* device,
    UINT                   width,
    UINT                   height,
    ID3D12DescriptorHeap* srvHeap,
    UINT                   gbufferSrvBaseSlot)
{
    mGBuffer.Resize(device, width, height,
        mGBufferRtvHeap.Get(), 0,
        srvHeap, gbufferSrvBaseSlot,
        mRtvDescSize, mSrvDescSize);
}

void RenderingSystem::BeginGeometryPass(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissor)
{
    mGBuffer.TransitionToRenderTarget(cmdList);
    mGBuffer.Clear(cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GBuffer::NumRTs] = {
        mGBuffer.GetAlbedoRTV(),
        mGBuffer.GetNormalRTV(),
        mGBuffer.GetPositionRTV()
    };

    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->OMSetRenderTargets(GBuffer::NumRTs, rtvs, false, &dsv);
    cmdList->ClearDepthStencilView(dsv,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    cmdList->SetPipelineState(mGeometryPSO.Get());
}

void RenderingSystem::EndGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    mGBuffer.TransitionToShaderResource(cmdList);
}

void RenderingSystem::BeginLightingPass(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissor)
{
    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmdList->ClearRenderTargetView(backBufferRTV, clearColor, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &backBufferRTV, true, &dsv);
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetPipelineState(mLightingPSO.Get());
    cmdList->SetGraphicsRootSignature(mLightingRootSig.Get());
}

void RenderingSystem::DrawLightingPass(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12DescriptorHeap* srvHeap,
    D3D12_GPU_VIRTUAL_ADDRESS  passCBAddress)
{
    ID3D12DescriptorHeap* heaps[] = { srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootDescriptorTable(0, mGBuffer.GetSRVGPUHandle());

    cmdList->SetGraphicsRootConstantBufferView(1, passCBAddress);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::BuildRtvHeap(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = GBuffer::NumRTs;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&desc,
        IID_PPV_ARGS(mGBufferRtvHeap.GetAddressOf())));
    mGBufferRtvHeap->SetName(L"GBuffer RTV Heap");
}

void RenderingSystem::BuildLightingRootSignature(ID3D12Device* device)
{
    CD3DX12_DESCRIPTOR_RANGE gbufferTable;
    gbufferTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1);

    CD3DX12_ROOT_PARAMETER params[2];
    params[0].InitAsDescriptorTable(1, &gbufferTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsConstantBufferView(0);

    CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC sigDesc(
        _countof(params), params, 1, &linearClamp,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> serialized, error;
    HRESULT hr = D3D12SerializeRootSignature(
        &sigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf());

    if (error)
        OutputDebugStringA((char*)error->GetBufferPointer());
    ThrowIfFailed(hr);

    ThrowIfFailed(device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(mLightingRootSig.GetAddressOf())));
    mLightingRootSig->SetName(L"Lighting Root Signature");
}

void RenderingSystem::BuildPSOs(
    ID3D12Device* device,
    DXGI_FORMAT            backBufferFormat,
    DXGI_FORMAT            depthStencilFormat,
    ID3D12RootSignature* geometryRootSig)
{
    mGeometryInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32_FLOAT,    0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto vsGeo = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "VS", "vs_5_0");
    auto psGeo = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "PS", "ps_5_0");
    auto vsLit = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", nullptr, "VS", "vs_5_0");
    auto psLit = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", nullptr, "PS", "ps_5_0");

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { mGeometryInputLayout.data(),
                                       (UINT)mGeometryInputLayout.size() };
        desc.pRootSignature = geometryRootSig;
        desc.VS = { reinterpret_cast<BYTE*>(vsGeo->GetBufferPointer()), vsGeo->GetBufferSize() };
        desc.PS = { reinterpret_cast<BYTE*>(psGeo->GetBufferPointer()), psGeo->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = GBuffer::NumRTs;
        desc.RTVFormats[0] = GBuffer::AlbedoFormat;
        desc.RTVFormats[1] = GBuffer::NormalFormat;
        desc.RTVFormats[2] = GBuffer::PositionFormat;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.DSVFormat = depthStencilFormat;

        ThrowIfFailed(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(mGeometryPSO.GetAddressOf())));
        mGeometryPSO->SetName(L"Geometry Pass PSO");
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout.NumElements = 0;
        desc.InputLayout.pInputElementDescs = nullptr;
        desc.pRootSignature = mLightingRootSig.Get();
        desc.VS = { reinterpret_cast<BYTE*>(vsLit->GetBufferPointer()), vsLit->GetBufferSize() };
        desc.PS = { reinterpret_cast<BYTE*>(psLit->GetBufferPointer()), psLit->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        D3D12_DEPTH_STENCIL_DESC dsd = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        dsd.DepthEnable = FALSE;
        dsd.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState = dsd;

        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = backBufferFormat;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.DSVFormat = depthStencilFormat;

        ThrowIfFailed(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(mLightingPSO.GetAddressOf())));
        mLightingPSO->SetName(L"Lighting Pass PSO");
    }
}