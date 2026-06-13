#include "PostProcessPass.h"

void PostProcessPass::Initialize(ID3D12Device* device,
    DXGI_FORMAT backBufferFormat,
    UINT width, UINT height,
    UINT cbvSrvDescSize)
{
    mWidth = width;
    mHeight = height;

    BuildRootSignature(device);
    BuildPSO(device, backBufferFormat);
}

void PostProcessPass::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;
}

void PostProcessPass::BuildRootSignature(ID3D12Device* device)
{
    CD3DX12_DESCRIPTOR_RANGE sceneColorTable;
    sceneColorTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE depthTable;
    depthTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsDescriptorTable(1, &sceneColorTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &depthTable, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_STATIC_SAMPLER_DESC linearClamp(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC sigDesc(
        _countof(params), params,
        1, &linearClamp,
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
        IID_PPV_ARGS(mRootSig.GetAddressOf())));
    mRootSig->SetName(L"PostProcess Root Signature");
}

void PostProcessPass::BuildPSO(ID3D12Device* device, DXGI_FORMAT backBufferFormat)
{
    auto vs = d3dUtil::CompileShader(L"Shaders\\PostProcess.hlsl", nullptr, "VS", "vs_5_0");
    auto ps = d3dUtil::CompileShader(L"Shaders\\PostProcess.hlsl", nullptr, "PS", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.InputLayout.NumElements = 0;
    desc.InputLayout.pInputElementDescs = nullptr;
    desc.pRootSignature = mRootSig.Get();
    desc.VS = { reinterpret_cast<BYTE*>(vs->GetBufferPointer()), vs->GetBufferSize() };
    desc.PS = { reinterpret_cast<BYTE*>(ps->GetBufferPointer()), ps->GetBufferSize() };
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
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(mPSO.GetAddressOf())));
    mPSO->SetName(L"PostProcess PSO");
}

void PostProcessPass::BeginPostProcess(ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissor)
{
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->OMSetRenderTargets(1, &backBufferRTV, false, nullptr);
    cmdList->SetPipelineState(mPSO.Get());
    cmdList->SetGraphicsRootSignature(mRootSig.Get());
}

void PostProcessPass::DrawPostProcess(ID3D12GraphicsCommandList* cmdList,
    ID3D12DescriptorHeap* srvHeap,
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
    D3D12_GPU_VIRTUAL_ADDRESS postProcessCBAddress)
{
    ID3D12DescriptorHeap* heaps[] = { srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootDescriptorTable(0, sceneColorSRV);
    cmdList->SetGraphicsRootDescriptorTable(1, depthSRV);
    cmdList->SetGraphicsRootConstantBufferView(2, postProcessCBAddress);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);
}