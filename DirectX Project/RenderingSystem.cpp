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
    BuildHDRDescriptorHeaps(device);

    mGBuffer.Create(device, width, height,
        mGBufferRtvHeap.Get(), 0,
        srvHeap, gbufferSrvBaseSlot,
        rtvDescSize, cbvSrvDescSize);

    BuildLightingRootSignature(device);
    BuildPSOs(device, backBufferFormat, depthStencilFormat, geometryRootSig);
    BuildHDRColorBuffer(device, width, height);
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

    BuildHDRColorBuffer(device, width, height);
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
        mGBuffer.GetNormalRTV()
    };

    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->OMSetRenderTargets(GBuffer::NumRTs, rtvs, false, &dsv);
    cmdList->ClearDepthStencilView(dsv,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    cmdList->SetPipelineState(
        mWireframeMode ? mGeometryWireframePSO.Get() : mGeometryPSO.Get());
}

void RenderingSystem::EndGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    mGBuffer.TransitionToShaderResource(cmdList);
}

void RenderingSystem::BeginLightingPass(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissor)
{
    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmdList->ClearRenderTargetView(mHDRColorRTV, clearColor, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &mHDRColorRTV, true, &dsv);
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetPipelineState(mLightingPSO.Get());
    cmdList->SetGraphicsRootSignature(mLightingRootSig.Get());
}

void RenderingSystem::EndLightingPass(ID3D12GraphicsCommandList* cmdList)
{
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mHDRColorBuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
}

void RenderingSystem::DrawLightingPass(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12DescriptorHeap* srvHeap,
    D3D12_GPU_VIRTUAL_ADDRESS   passCBAddress,
    D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE irradianceSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE prefilterSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE spotLightBufSRV)
{
    ID3D12DescriptorHeap* heaps[] = { srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootDescriptorTable(0, mGBuffer.GetSRVGPUHandle());
    cmdList->SetGraphicsRootConstantBufferView(1, passCBAddress);
    cmdList->SetGraphicsRootDescriptorTable(2, depthSRV);
    cmdList->SetGraphicsRootDescriptorTable(3, shadowSRV);
    cmdList->SetGraphicsRootDescriptorTable(4, irradianceSRV);
    cmdList->SetGraphicsRootDescriptorTable(5, prefilterSRV);
    cmdList->SetGraphicsRootDescriptorTable(6, brdfLutSRV);
    cmdList->SetGraphicsRootDescriptorTable(7, spotLightBufSRV);

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
    ThrowIfFailed(device->CreateDescriptorHeap(
        &desc, IID_PPV_ARGS(mGBufferRtvHeap.GetAddressOf())));
    mGBufferRtvHeap->SetName(L"GBuffer RTV Heap");
}

void RenderingSystem::BuildHDRDescriptorHeaps(ID3D12Device* device)
{
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device->CreateDescriptorHeap(
            &desc, IID_PPV_ARGS(mHDRRtvHeap.GetAddressOf())));
        mHDRRtvHeap->SetName(L"HDR Color RTV Heap");
        mHDRColorRTV = mHDRRtvHeap->GetCPUDescriptorHandleForHeapStart();
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(
            &desc, IID_PPV_ARGS(mHDRSrvHeapGPU.GetAddressOf())));
        mHDRSrvHeapGPU->SetName(L"HDR Color SRV Heap");

        mHDRColorSRV_CPU = mHDRSrvHeapGPU->GetCPUDescriptorHandleForHeapStart();
        mHDRColorSRV_GPU = mHDRSrvHeapGPU->GetGPUDescriptorHandleForHeapStart();
    }
}

void RenderingSystem::BuildHDRColorBuffer(ID3D12Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;

    if (mHDRColorBuffer)
        mHDRColorBuffer.Reset();

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = texDesc.Format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        IID_PPV_ARGS(&mHDRColorBuffer)));
    mHDRColorBuffer->SetName(L"HDR Color Buffer");

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = texDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(mHDRColorBuffer.Get(), &rtvDesc, mHDRColorRTV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(mHDRColorBuffer.Get(), &srvDesc, mHDRColorSRV_CPU);
}

void RenderingSystem::BuildLightingRootSignature(ID3D12Device* device)
{
    CD3DX12_DESCRIPTOR_RANGE gbufferTable;
    gbufferTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);
    CD3DX12_DESCRIPTOR_RANGE depthTable;
    depthTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    CD3DX12_DESCRIPTOR_RANGE shadowTable;
    shadowTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    CD3DX12_DESCRIPTOR_RANGE irradianceTable;
    irradianceTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
    CD3DX12_DESCRIPTOR_RANGE prefilterTable;
    prefilterTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
    CD3DX12_DESCRIPTOR_RANGE brdfLutTable;
    brdfLutTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);
    CD3DX12_DESCRIPTOR_RANGE spotLightBufTable;
    spotLightBufTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);

    CD3DX12_ROOT_PARAMETER params[8];
    params[0].InitAsDescriptorTable(1, &gbufferTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsConstantBufferView(0);
    params[2].InitAsDescriptorTable(1, &depthTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[3].InitAsDescriptorTable(1, &shadowTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[4].InitAsDescriptorTable(1, &irradianceTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[5].InitAsDescriptorTable(1, &prefilterTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[6].InitAsDescriptorTable(1, &brdfLutTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[7].InitAsDescriptorTable(1, &spotLightBufTable, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC linearClamp(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_STATIC_SAMPLER_DESC shadowSampler(1,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f, 0,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

    CD3DX12_STATIC_SAMPLER_DESC linearWrap(2,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_STATIC_SAMPLER_DESC samplers[3] = { linearClamp, shadowSampler, linearWrap };

    CD3DX12_ROOT_SIGNATURE_DESC sigDesc(
        _countof(params), params,
        _countof(samplers), samplers,
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
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32_FLOAT,    0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto vsGeo = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "VS", "vs_5_0");
    auto hsGeo = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "HS", "hs_5_0");
    auto dsGeo = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "DS", "ds_5_0");
    auto psGeo = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "PS", "ps_5_0");

    auto vsLOD1 = d3dUtil::CompileShader(L"Shaders\\GBufferLOD1.hlsl", nullptr, "VS", "vs_5_0");
    auto psLOD1 = d3dUtil::CompileShader(L"Shaders\\GBufferLOD1.hlsl", nullptr, "PS", "ps_5_0");

    auto vsBB = d3dUtil::CompileShader(L"Shaders\\Billboard.hlsl", nullptr, "VS", "vs_5_0");
    auto gsBB = d3dUtil::CompileShader(L"Shaders\\Billboard.hlsl", nullptr, "GS", "gs_5_0");
    auto psBB = d3dUtil::CompileShader(L"Shaders\\Billboard.hlsl", nullptr, "PS", "ps_5_0");

    auto vsLit = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", nullptr, "VS", "vs_5_0");
    auto psLit = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", nullptr, "PS", "ps_5_0");

    auto fillGBufferRTVFormats = [](D3D12_GRAPHICS_PIPELINE_STATE_DESC& d)
        {
            d.NumRenderTargets = GBuffer::NumRTs;
            d.RTVFormats[0] = GBuffer::AlbedoFormat;
            d.RTVFormats[1] = GBuffer::NormalFormat;
        };

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { mGeometryInputLayout.data(), (UINT)mGeometryInputLayout.size() };
        desc.pRootSignature = geometryRootSig;
        desc.VS = { reinterpret_cast<BYTE*>(vsGeo->GetBufferPointer()), vsGeo->GetBufferSize() };
        desc.HS = { reinterpret_cast<BYTE*>(hsGeo->GetBufferPointer()), hsGeo->GetBufferSize() };
        desc.DS = { reinterpret_cast<BYTE*>(dsGeo->GetBufferPointer()), dsGeo->GetBufferSize() };
        desc.PS = { reinterpret_cast<BYTE*>(psGeo->GetBufferPointer()), psGeo->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        fillGBufferRTVFormats(desc);
        desc.SampleDesc.Count = 1;
        desc.DSVFormat = depthStencilFormat;
        ThrowIfFailed(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(mGeometryPSO.GetAddressOf())));
        mGeometryPSO->SetName(L"LOD0 Geometry PSO (tessellated)");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC wireDesc = desc;
        D3D12_RASTERIZER_DESC wireRaster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        wireRaster.FillMode = D3D12_FILL_MODE_WIREFRAME;
        wireRaster.CullMode = D3D12_CULL_MODE_NONE;
        wireDesc.RasterizerState = wireRaster;
        ThrowIfFailed(device->CreateGraphicsPipelineState(
            &wireDesc, IID_PPV_ARGS(mGeometryWireframePSO.GetAddressOf())));
        mGeometryWireframePSO->SetName(L"LOD0 Geometry PSO (wireframe)");
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { mGeometryInputLayout.data(), (UINT)mGeometryInputLayout.size() };
        desc.pRootSignature = geometryRootSig;
        desc.VS = { reinterpret_cast<BYTE*>(vsLOD1->GetBufferPointer()), vsLOD1->GetBufferSize() };
        desc.PS = { reinterpret_cast<BYTE*>(psLOD1->GetBufferPointer()), psLOD1->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        fillGBufferRTVFormats(desc);
        desc.SampleDesc.Count = 1;
        desc.DSVFormat = depthStencilFormat;
        ThrowIfFailed(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(mGeometryLOD1PSO.GetAddressOf())));
        mGeometryLOD1PSO->SetName(L"LOD1 Geometry PSO (no tessellation)");
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { mGeometryInputLayout.data(), (UINT)mGeometryInputLayout.size() };
        desc.pRootSignature = geometryRootSig;
        desc.VS = { reinterpret_cast<BYTE*>(vsBB->GetBufferPointer()), vsBB->GetBufferSize() };
        desc.GS = { reinterpret_cast<BYTE*>(gsBB->GetBufferPointer()), gsBB->GetBufferSize() };
        desc.PS = { reinterpret_cast<BYTE*>(psBB->GetBufferPointer()), psBB->GetBufferSize() };

        D3D12_RASTERIZER_DESC raster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        raster.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState = raster;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        fillGBufferRTVFormats(desc);
        desc.SampleDesc.Count = 1;
        desc.DSVFormat = depthStencilFormat;
        ThrowIfFailed(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(mBillboardPSO.GetAddressOf())));
        mBillboardPSO->SetName(L"LOD2 Billboard PSO");
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
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.DSVFormat = depthStencilFormat;
        ThrowIfFailed(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(mLightingPSO.GetAddressOf())));
        mLightingPSO->SetName(L"Lighting Pass PSO");
    }
}