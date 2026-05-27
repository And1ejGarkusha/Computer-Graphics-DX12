#pragma once
#include "d3dUtil.h"
#include "d3dx12.h"
#include "FrameResource.h"
#include "ShadowMap.h"
#include <array>
#include <functional>

class ShadowPass
{
public:
    float SplitLambda = 0.75f;

    void Initialize(
        ID3D12Device* device,
        DXGI_FORMAT          shadowMapDsvFormat,
        const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout);

    void UpdateCascades(
        PassConstants& passConstants,
        const DirectX::XMMATRIX& proj,
        const DirectX::XMMATRIX& invView,
        const DirectX::XMFLOAT3& lightDirW);

    void Render(
        ID3D12GraphicsCommandList* cmdList,
        ShadowMap& shadowMap,
        const FrameResource& frameRes,
        std::function<void(ID3D12GraphicsCommandList*, int)> drawCallback);

private:
    static std::array<DirectX::XMVECTOR, 8> GetSubFrustumCornersWS(
        float nearZ, float farZ,
        const DirectX::XMMATRIX& proj,
        const DirectX::XMMATRIX& invView);

    static DirectX::XMMATRIX ComputeLightViewProj(
        const std::array<DirectX::XMVECTOR, 8>& cornersWS,
        const DirectX::XMVECTOR& lightDir);

    void BuildRootSignature(ID3D12Device* device);
    void BuildPSO(ID3D12Device* device, DXGI_FORMAT dsvFormat,
        const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;

    float               mCascadeSplits[NUM_CASCADES] = {};
    DirectX::XMFLOAT4X4 mLightViewProj[NUM_CASCADES] = {};
};