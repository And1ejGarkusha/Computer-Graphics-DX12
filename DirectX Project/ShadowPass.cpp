#include "ShadowPass.h"
#include "d3dUtil.h"
#include <cfloat>
#include <cmath>
#include <algorithm>

using namespace DirectX;

void ShadowPass::Initialize(
    ID3D12Device* device,
    DXGI_FORMAT   shadowMapDsvFormat,
    const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout)
{
    BuildRootSignature(device);
    BuildPSO(device, shadowMapDsvFormat, inputLayout);
}

void ShadowPass::UpdateCascades(
    PassConstants& passConstants,
    const XMMATRIX& proj,
    const XMMATRIX& invView,
    const XMFLOAT3& lightDirW)
{
    const float nearZ = passConstants.NearZ;
    const float farZ = passConstants.FarZ;

    for (int i = 1; i <= NUM_CASCADES; ++i)
    {
        float p = (float)i / (float)NUM_CASCADES;
        float lg = nearZ * std::pow(farZ / nearZ, p);
        float lin = nearZ + (farZ - nearZ) * p;
        mCascadeSplits[i - 1] = SplitLambda * lg + (1.0f - SplitLambda) * lin;
    }

    XMVECTOR lightDir = XMLoadFloat3(&lightDirW);

    float prevSplit = nearZ;
    for (int i = 0; i < NUM_CASCADES; ++i)
    {
        auto corners = GetSubFrustumCornersWS(prevSplit, mCascadeSplits[i], proj, invView);
        XMMATRIX lvp = ComputeLightViewProj(corners, lightDir);
        XMStoreFloat4x4(&mLightViewProj[i], XMMatrixTranspose(lvp));
        prevSplit = mCascadeSplits[i];
    }

    for (int i = 0; i < NUM_CASCADES; ++i)
        passConstants.LightViewProj[i] = mLightViewProj[i];

    passConstants.CascadeSplits = {
        mCascadeSplits[0],
        mCascadeSplits[1],
        mCascadeSplits[2],
        mCascadeSplits[3]
    };
}

void ShadowPass::Render(
    ID3D12GraphicsCommandList* cmdList,
    ShadowMap& shadowMap,
    const FrameResource& frameRes,
    ShadowDrawCallback drawCallback)
{
    shadowMap.TransitionToDepthWrite(cmdList);

    cmdList->SetPipelineState(mPSO.Get());
    cmdList->SetGraphicsRootSignature(mRootSig.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const UINT shadowCBStride =
        d3dUtil::CalcConstantBufferByteSize(sizeof(ShadowPassConstants));

    for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
    {
        shadowMap.BeginCascade(cmdList, cascade);

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
            frameRes.ShadowPassCB->Resource()->GetGPUVirtualAddress()
            + (UINT64)cascade * shadowCBStride;
        cmdList->SetGraphicsRootConstantBufferView(kRootShadowPassCB, cbAddr);

        D3D12_GPU_DESCRIPTOR_HANDLE nullHandle = {};
        drawCallback(cmdList, cascade, nullHandle);
    }

    shadowMap.TransitionToShaderResource(cmdList);
}

std::array<XMVECTOR, 8> ShadowPass::GetSubFrustumCornersWS(
    float nearZ, float farZ,
    const XMMATRIX& proj,
    const XMMATRIX& invView)
{
    float tanHalfFovX = 1.0f / XMVectorGetX(proj.r[0]);
    float tanHalfFovY = 1.0f / XMVectorGetY(proj.r[1]);

    std::array<XMVECTOR, 8> corners;
    int idx = 0;
    for (float z : { nearZ, farZ })
    {
        float hw = z * tanHalfFovX;
        float hh = z * tanHalfFovY;
        corners[idx++] = XMVector3TransformCoord(XMVectorSet(-hw, -hh, z, 1.f), invView);
        corners[idx++] = XMVector3TransformCoord(XMVectorSet(hw, -hh, z, 1.f), invView);
        corners[idx++] = XMVector3TransformCoord(XMVectorSet(hw, hh, z, 1.f), invView);
        corners[idx++] = XMVector3TransformCoord(XMVectorSet(-hw, hh, z, 1.f), invView);
    }
    return corners;
}

XMMATRIX ShadowPass::ComputeLightViewProj(
    const std::array<XMVECTOR, 8>& cornersWS,
    const XMVECTOR& lightDir)
{
    XMVECTOR center = XMVectorZero();
    for (const auto& c : cornersWS)
        center = XMVectorAdd(center, c);
    center = XMVectorScale(center, 1.0f / 8.0f);

    XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
    if (std::abs(XMVectorGetY(lightDir)) > 0.99f)
        up = XMVectorSet(0.f, 0.f, 1.f, 0.f);

    XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(lightDir, 200.0f));
    XMMATRIX lightView = XMMatrixLookAtLH(eye, center, up);

    float minX = FLT_MAX, maxX = -FLT_MAX;
    float minY = FLT_MAX, maxY = -FLT_MAX;
    float minZ = FLT_MAX, maxZ = -FLT_MAX;

    for (const auto& c : cornersWS)
    {
        XMVECTOR lc = XMVector3TransformCoord(c, lightView);
        float x = XMVectorGetX(lc);
        float y = XMVectorGetY(lc);
        float z = XMVectorGetZ(lc);

        minX = (std::min)(minX, x);   maxX = (std::max)(maxX, x);
        minY = (std::min)(minY, y);   maxY = (std::max)(maxY, y);
        minZ = (std::min)(minZ, z);   maxZ = (std::max)(maxZ, z);
    }

    constexpr float zPullback = 10.0f;
    if (minZ < 0.f) minZ *= zPullback;
    else            minZ /= zPullback;

    XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
        minX, maxX, minY, maxY, minZ, maxZ);

    return lightView * lightProj;
}

void ShadowPass::BuildRootSignature(ID3D12Device* device)
{

    CD3DX12_DESCRIPTOR_RANGE diffuseRange;
    diffuseRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER params[3];
    params[kRootObjCB].InitAsConstantBufferView(0);
    params[kRootShadowPassCB].InitAsConstantBufferView(1);
    params[kRootDiffuseTex].InitAsDescriptorTable(
        1, &diffuseRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC staticSampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC sigDesc(
        _countof(params), params,
        1, &staticSampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> serialized, error;
    HRESULT hr = D3D12SerializeRootSignature(
        &sigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf());
    if (error)
        OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));
    ThrowIfFailed(hr);

    ThrowIfFailed(device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(mRootSig.GetAddressOf())));
    mRootSig->SetName(L"Shadow Pass Root Signature");
}

void ShadowPass::BuildPSO(
    ID3D12Device* device,
    DXGI_FORMAT   dsvFormat,
    const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout)
{
    auto vsShadow = d3dUtil::CompileShader(
        L"Shaders\\Shadow.hlsl", nullptr, "VS", "vs_5_0");

    auto psShadow = d3dUtil::CompileShader(
        L"Shaders\\Shadow.hlsl", nullptr, "PS", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
    desc.pRootSignature = mRootSig.Get();
    desc.VS = { reinterpret_cast<BYTE*>(vsShadow->GetBufferPointer()),
                vsShadow->GetBufferSize() };
    desc.PS = { reinterpret_cast<BYTE*>(psShadow->GetBufferPointer()),
                psShadow->GetBufferSize() };

    D3D12_RASTERIZER_DESC raster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    raster.DepthBias = 8000;
    raster.SlopeScaledDepthBias = 1.5f;
    raster.DepthBiasClamp = 0.0f;
    raster.CullMode = D3D12_CULL_MODE_NONE;

    desc.RasterizerState = raster;
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;

    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 0;
    desc.DSVFormat = dsvFormat;
    desc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(mPSO.GetAddressOf())));
    mPSO->SetName(L"Shadow Pass PSO");
}