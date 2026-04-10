#include "d3dApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include "OBJLoader.h"
#include <fstream>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

struct RenderItem
{
	RenderItem() = default;

    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class CrateApp : public D3DApp
{
public:
    CrateApp(HINSTANCE hInstance);
    CrateApp(const CrateApp& rhs) = delete;
    CrateApp& operator=(const CrateApp& rhs) = delete;
    ~CrateApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	void LoadOBJModel(const std::string& filename, const std::string& modelName);
	std::unique_ptr<MeshGeometry> mOBJMesh;

	void LoadTextureFromFile(const std::string& path, const std::string& texName, int heapIndex);
	std::unordered_map<std::string, int> mMaterialTextureSlot;

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	bool mIsMouseLookEnabled = false;
	bool mIsSprinting = false;

	XMFLOAT3 mCameraPos = { 0.0f, 1.7f, 0.0f };
	float mCameraYaw = -XM_PI / 2.0f;
	float mCameraPitch = 0.0f;

	float mMoveSpeed = 5.0f;
	float mSprintMultiplier = 2.0f;
	float mMouseSensitivity = 0.002f;
	float mPitchClamp = XM_PI / 2.0f - 0.1f;

	float mTheta = 1.3f*XM_PI;
	float mPhi = 0.4f*XM_PI;
	float mRadius = 2.5f;

    POINT mLastMousePos;
	POINT mCenterClient;
	POINT mCenterScreen;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        CrateApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

CrateApp::CrateApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

CrateApp::~CrateApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool CrateApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();

	LoadOBJModel("sponza.obj", "myModel");

	BuildRenderItems();

	BuildFrameResources();
	BuildPSOs();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	return true;
}
 
void CrateApp::OnResize()
{
    D3DApp::OnResize();

    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void CrateApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void CrateApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    ThrowIfFailed(cmdListAlloc->Reset());

    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &transition);

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	auto cbbv = CurrentBackBufferView();
	auto dsv = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &cbbv, true, &dsv);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &transition);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;

    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

bool LoadTGAFile(const std::wstring& filename, std::vector<uint8_t>& outData, int& width, int& height, DXGI_FORMAT& format)
{
	std::ifstream file(filename, std::ios::binary);
	if (!file.is_open()) return false;

	uint8_t header[18];
	file.read(reinterpret_cast<char*>(header), 18);

	if (header[2] != 2) return false;

	width = header[12] + (header[13] << 8);
	height = header[14] + (header[15] << 8);
	uint8_t bpp = header[16];

	if (bpp != 24 && bpp != 32) return false;

	int bytesPerPixel = bpp / 8;
	int dataSize = width * height * bytesPerPixel;
	outData.resize(dataSize);

	file.read(reinterpret_cast<char*>(outData.data()), dataSize);

	for (int i = 0; i < width * height; ++i)
	{
		int offset = i * bytesPerPixel;
		uint8_t b = outData[offset];
		uint8_t g = outData[offset + 1];
		uint8_t r = outData[offset + 2];
		outData[offset] = r;
		outData[offset + 1] = g;
		outData[offset + 2] = b;
	}

	format = DXGI_FORMAT_R8G8B8A8_UNORM;
	return true;
}

void CrateApp::LoadOBJModel(const std::string& filename, const std::string& modelName)
{
	OutputDebugStringA(("Trying to load: " + filename + "\n").c_str());

	OBJMeshData mesh;
	std::vector<OBJMaterialData> materials;

	if (!OBJLoader::Load(filename, mesh, materials))
	{
		OutputDebugStringA(("FAILED to load OBJ file: " + filename + "\n").c_str());
		return;
	}

	OutputDebugStringA(("SUCCESS! Loaded " + std::to_string(mesh.Vertices.size()) + " vertices\n").c_str());

	std::vector<Vertex> vertices;
	for (const auto& v : mesh.Vertices)
	{
		Vertex vert;
		vert.Pos = v.Position;
		vert.Normal = v.Normal;
		vert.TexC = v.TexCoord;
		vertices.push_back(vert);
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)mesh.Indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = modelName;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), mesh.Indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), mesh.Indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;
	geo->DrawArgs = mesh.DrawArgs;

	mGeometries[geo->Name] = std::move(geo);

	std::string modelDir = filename;
	size_t lastSlash = modelDir.find_last_of("\\/");
	if (lastSlash != std::string::npos)
		modelDir = modelDir.substr(0, lastSlash + 1);
	else
		modelDir = "";

	OutputDebugStringA(("Model directory: " + modelDir + "\n").c_str());

	int matIndex = (int)mMaterials.size();
	int nextHeapSlot = 1;

	for (const auto& mtlMat : materials)
	{
		auto material = std::make_unique<Material>();
		material->Name = mtlMat.Name;
		material->MatCBIndex = matIndex++;
		material->DiffuseAlbedo = mtlMat.DiffuseAlbedo;
		material->FresnelR0 = mtlMat.FresnelR0;
		material->Roughness = mtlMat.Roughness;
		material->MatTransform = MathHelper::Identity4x4();
		material->NumFramesDirty = gNumFrameResources;

		if (!mtlMat.DiffuseMapPath.empty())
		{
			std::string texPath = mtlMat.DiffuseMapPath;

			if (texPath.find(':') == std::string::npos &&
				texPath.front() != '/' && texPath.front() != '\\')
			{
				texPath = modelDir + texPath;
			}

			std::string texName = "tex_" + mtlMat.Name;

			if (mMaterialTextureSlot.find(mtlMat.Name) == mMaterialTextureSlot.end())
			{
				LoadTextureFromFile(texPath, texName, nextHeapSlot);
				material->DiffuseSrvHeapIndex = nextHeapSlot;
				mMaterialTextureSlot[mtlMat.Name] = nextHeapSlot;
				nextHeapSlot++;
			}
			else
			{
				material->DiffuseSrvHeapIndex = mMaterialTextureSlot[mtlMat.Name];
			}
		}
		else
		{
			material->DiffuseSrvHeapIndex = 0;
		}

		mMaterials[material->Name] = std::move(material);
	}

	for (const auto& drawArg : mesh.DrawArgs)
	{
		auto objRitem = std::make_unique<RenderItem>();
		objRitem->ObjCBIndex = (UINT)mAllRitems.size();
		objRitem->NumFramesDirty = gNumFrameResources;
		objRitem->TexTransform = MathHelper::Identity4x4();

		auto matIt = mesh.SubmeshToMaterial.find(drawArg.first);
		if (matIt != mesh.SubmeshToMaterial.end())
		{
			auto materialIt = mMaterials.find(matIt->second);
			if (materialIt != mMaterials.end())
				objRitem->Mat = materialIt->second.get();
			else
				objRitem->Mat = mMaterials["woodCrate"].get();
		}
		else
		{
			objRitem->Mat = mMaterials["woodCrate"].get();
		}

		objRitem->Geo = mGeometries[modelName].get();
		objRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		objRitem->IndexCount = drawArg.second.IndexCount;
		objRitem->StartIndexLocation = drawArg.second.StartIndexLocation;
		objRitem->BaseVertexLocation = drawArg.second.BaseVertexLocation;

		XMMATRIX scale = XMMatrixScaling(0.02f, 0.02f, 0.02f);
		XMMATRIX translation = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
		XMMATRIX world = scale * translation;
		XMStoreFloat4x4(&objRitem->World, world);

		mAllRitems.push_back(std::move(objRitem));
	}

	OutputDebugStringA(("Total render items created: " + std::to_string(mAllRitems.size()) + "\n").c_str());

	for (const auto& mtlMat : materials)
	{
		char msg[256];
		sprintf_s(msg, "MTL: %s -> %s\n", mtlMat.Name.c_str(), mtlMat.DiffuseMapPath.c_str());
		OutputDebugStringA(msg);
	}

	for (const auto& pair : mesh.SubmeshToMaterial)
	{
		char msg[256];
		sprintf_s(msg, "Submesh %s -> Material %s\n", pair.first.c_str(), pair.second.c_str());
		OutputDebugStringA(msg);
	}
}


void CrateApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		mIsMouseLookEnabled = true;
		ShowCursor(FALSE);
		SetCapture(mhMainWnd);

		RECT rc;
		GetClientRect(mhMainWnd, &rc);
		MapWindowPoints(mhMainWnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
		ClipCursor(&rc);

		mCenterClient.x = (rc.right - rc.left) / 2;
		mCenterClient.y = (rc.bottom - rc.top) / 2;

		mCenterScreen = mCenterClient;
		ClientToScreen(mhMainWnd, &mCenterScreen);

		SetCursorPos(mCenterScreen.x, mCenterScreen.y);
	}
}

void CrateApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) == 0)
	{
		mIsMouseLookEnabled = false;
		ShowCursor(TRUE);
		ClipCursor(nullptr);
	}
	ReleaseCapture();
}

void CrateApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if (mIsMouseLookEnabled)
	{
		float dx = static_cast<float>(x - mCenterClient.x);
		float dy = static_cast<float>(y - mCenterClient.y);

		mCameraYaw -= dx * mMouseSensitivity;
		mCameraPitch -= dy * mMouseSensitivity;
		mCameraPitch = MathHelper::Clamp(mCameraPitch, -mPitchClamp, mPitchClamp);

		SetCursorPos(mCenterScreen.x, mCenterScreen.y);
	}
}
 
void CrateApp::OnKeyboardInput(const GameTimer& gt)
{
	XMFLOAT3 forward;
	forward.x = cosf(mCameraYaw) * cosf(mCameraPitch);
	forward.y = sinf(mCameraPitch);
	forward.z = sinf(mCameraYaw) * cosf(mCameraPitch);

	XMFLOAT3 right;
	XMStoreFloat3(&right, XMVector3Normalize(
		XMVector3Cross(XMLoadFloat3(&forward), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f))));

	XMFLOAT3 up;
	XMStoreFloat3(&up, XMVector3Normalize(
		XMVector3Cross(XMLoadFloat3(&right), XMLoadFloat3(&forward))));

	bool sprint = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	float speed = mMoveSpeed * (sprint ? mSprintMultiplier : 1.0f) * gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
	{
		mCameraPos.x += forward.x * speed;
		mCameraPos.y += forward.y * speed;
		mCameraPos.z += forward.z * speed;
	}
	if (GetAsyncKeyState('S') & 0x8000)
	{
		mCameraPos.x -= forward.x * speed;
		mCameraPos.y -= forward.y * speed;
		mCameraPos.z -= forward.z * speed;
	}

	if (GetAsyncKeyState('A') & 0x8000)
	{
		mCameraPos.x += right.x * speed;
		mCameraPos.y += right.y * speed;
		mCameraPos.z += right.z * speed;
	}
	if (GetAsyncKeyState('D') & 0x8000)
	{
		mCameraPos.x -= right.x * speed;
		mCameraPos.y -= right.y * speed;
		mCameraPos.z -= right.z * speed;
	}

	if (GetAsyncKeyState('Q') & 0x8000)
	{
		mCameraPos.y -= speed;
	}
	if (GetAsyncKeyState('E') & 0x8000)
	{
		mCameraPos.y += speed;
	}
}
 
void CrateApp::UpdateCamera(const GameTimer& gt)
{
	XMFLOAT3 forward;
	forward.x = cosf(mCameraYaw) * cosf(mCameraPitch);
	forward.y = sinf(mCameraPitch);
	forward.z = sinf(mCameraYaw) * cosf(mCameraPitch);

	XMVECTOR pos = XMLoadFloat3(&mCameraPos);
	XMVECTOR target = XMVectorAdd(pos, XMLoadFloat3(&forward));
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void CrateApp::AnimateMaterials(const GameTimer& gt)
{
	static float offsetX = 0.0f;
	static float offsetY = 0.0f;

	offsetX += gt.DeltaTime() * 0.2f;
	offsetY += gt.DeltaTime() * 0.2f;

	for (auto& e : mMaterials)
	{
		Material* mat = e.second.get();
		XMMATRIX texTransform = XMMatrixTranslation(offsetX, offsetY, 0.0f);
		XMStoreFloat4x4(&mat->MatTransform, texTransform);
		mat->NumFramesDirty = gNumFrameResources;
	}
}

void CrateApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}

void CrateApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			mat->NumFramesDirty--;
		}
	}
}

void CrateApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	auto viewDet = XMMatrixDeterminant(view);
	auto projDet = XMMatrixDeterminant(proj);
	XMMATRIX invView = XMMatrixInverse(&viewDet, view);
	XMMATRIX invProj = XMMatrixInverse(&projDet, proj);
	auto viewProjDet = XMMatrixDeterminant(viewProj);
	XMMATRIX invViewProj = XMMatrixInverse(&viewProjDet, viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void CrateApp::LoadTextures()
{
	auto woodCrateTex = std::make_unique<Texture>();
	woodCrateTex->Name = "woodCrateTex";
	woodCrateTex->Filename = L"textures/WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodCrateTex->Filename.c_str(),
		woodCrateTex->Resource, woodCrateTex->UploadHeap));
 
	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);
}

void CrateApp::LoadTextureFromFile(const std::string& path, const std::string& texName, int heapIndex)
{
	OutputDebugStringA(("Loading texture: " + path + "\n").c_str());

	std::wstring wpath(path.begin(), path.end());

	std::vector<uint8_t> tgaData;
	int width, height;
	DXGI_FORMAT format;

	if (LoadTGAFile(wpath, tgaData, width, height, format))
	{
		OutputDebugStringA("Loaded as TGA\n");

		int bytesPerPixel = tgaData.size() / (width * height);

		std::vector<uint8_t> rgbaData;
		const uint8_t* sourceData = tgaData.data();

		if (bytesPerPixel == 3)
		{
			rgbaData.resize(width * height * 4);
			for (int i = 0; i < width * height; ++i)
			{
				rgbaData[i * 4 + 0] = tgaData[i * 3 + 0];
				rgbaData[i * 4 + 1] = tgaData[i * 3 + 1];
				rgbaData[i * 4 + 2] = tgaData[i * 3 + 2];
				rgbaData[i * 4 + 3] = 255;
			}
			sourceData = rgbaData.data();
			bytesPerPixel = 4;
		}

		auto texture = std::make_unique<Texture>();
		texture->Name = texName;

		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Width = width;
		texDesc.Height = height;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		HRESULT hr = md3dDevice->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
			D3D12_RESOURCE_STATE_COMMON, nullptr,
			IID_PPV_ARGS(&texture->Resource));

		if (FAILED(hr))
		{
			OutputDebugStringA("Failed to create texture resource\n");
			return;
		}

		UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture->Resource.Get(), 0, 1);

		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

		hr = md3dDevice->CreateCommittedResource(
			&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&texture->UploadHeap));

		if (FAILED(hr))
		{
			OutputDebugStringA("Failed to create upload buffer\n");
			return;
		}

		D3D12_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pData = sourceData;
		subresourceData.RowPitch = width * 4;
		subresourceData.SlicePitch = subresourceData.RowPitch * height;

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture->Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		mCommandList->ResourceBarrier(1, &barrier);

		UpdateSubresources(mCommandList.Get(), texture->Resource.Get(), texture->UploadHeap.Get(),
			0, 0, 1, &subresourceData);

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture->Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		mCommandList->ResourceBarrier(1, &barrier);

		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
			mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		hDescriptor.Offset(heapIndex, mCbvSrvDescriptorSize);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		md3dDevice->CreateShaderResourceView(texture->Resource.Get(), &srvDesc, hDescriptor);

		mTextures[texName] = std::move(texture);
		OutputDebugStringA("TGA texture loaded successfully\n");
		return;
	}
}

void CrateApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CrateApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 50;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto woodCrateTex = mTextures["woodCrateTex"]->Resource;
 
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = woodCrateTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodCrateTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	md3dDevice->CreateShaderResourceView(woodCrateTex.Get(), &srvDesc, hDescriptor);
}

void CrateApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void CrateApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
 
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = 0;
	boxSubmesh.BaseVertexLocation = 0;

 
	std::vector<Vertex> vertices(box.Vertices.size());

	for(size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = box.GetIndices16();

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size()  * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void CrateApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));
}

void CrateApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size() + 1, (UINT)mMaterials.size())); // +1 для OBJ модели
	}
}

void CrateApp::BuildMaterials()
{
	auto woodCrate = std::make_unique<Material>();
	woodCrate->Name = "woodCrate";
	woodCrate->MatCBIndex = 0;
	woodCrate->DiffuseSrvHeapIndex = 0;
	woodCrate->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	woodCrate->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	woodCrate->Roughness = 0.2f;

	mMaterials["woodCrate"] = std::move(woodCrate);

	auto metalMat = std::make_unique<Material>();
	metalMat->Name = "metalMat";
	metalMat->MatCBIndex = 1;
	metalMat->DiffuseSrvHeapIndex = 0;
	metalMat->DiffuseAlbedo = XMFLOAT4(0.8f, 0.8f, 0.9f, 1.0f);
	metalMat->FresnelR0 = XMFLOAT3(0.8f, 0.8f, 0.8f);
	metalMat->Roughness = 0.1f;
	mMaterials["metalMat"] = std::move(metalMat);
}

void CrateApp::BuildRenderItems()
{
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void CrateApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	if (ritems.empty())
	{
		OutputDebugStringA("WARNING: No render items to draw!\n");
		return;
	}

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		if (i < 10)
		{
			char msg[256];
			sprintf_s(msg, "RI[%zu]: Material=%s, TexIndex=%d, IndexCount=%d\n",
				i, ri->Mat->Name.c_str(), ri->Mat->DiffuseSrvHeapIndex, ri->IndexCount);
			OutputDebugStringA(msg);
		}
	}

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

		auto vbv = ri->Geo->VertexBufferView();
		auto ibv = ri->Geo->IndexBufferView();
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CrateApp::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f,
		8);

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}