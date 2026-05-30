#include "d3dApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include "OBJLoader.h"
#include "GBuffer.h"
#include "RenderingSystem.h"
#include "ShadowMap.h"
#include "ShadowPass.h"
#include "Octree.h"
#include <fstream>
#include <random>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

const int NUM_DIR_LIGHTS = 1;
const int NUM_POINT_LIGHTS = 1;
const int NUM_SPOT_LIGHTS = 1;

static const UINT kRootDiffuse = 0;
static const UINT kRootObjCB = 1;
static const UINT kRootPassCB = 2;
static const UINT kRootMatCB = 3;
static const UINT kRootNormal = 4;
static const UINT kRootDisplace = 5;
static const UINT kRootRoughness = 6;
static const UINT kRootMetallic = 7;

static const int kDefaultNormalSlot = 2;
static const int kDefaultDispSlot = 3;
static const int kDefaultRoughnessSlot = 4;
static const int kDefaultMetallicSlot = 5;
static const int kDefaultAOSlot = 6;
static const int kObjTexBaseSlot = 7;
static const UINT kDefaultDiffuseSlot = kObjTexBaseSlot + 100;

static const int kNumScatteredObjects = 2000;

struct RenderItem
{
	RenderItem() = default;

	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int  NumFramesDirty = gNumFrameResources;
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType =
		D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int  BaseVertexLocation = 0;

	BoundingBox WorldBounds = BoundingBox(
		XMFLOAT3(0, 0, 0), XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX));
};

class CrateApp : public D3DApp
{
public:
	CrateApp(HINSTANCE hInstance);
	CrateApp(const CrateApp& rhs) = delete;
	CrateApp& operator=(const CrateApp& rhs) = delete;
	~CrateApp();

	virtual bool Initialize() override;

	void SetGamma(float gamma) { mGamma = gamma; }

private:
	virtual void OnResize()                          override;
	virtual void Update(const GameTimer& gt)         override;
	virtual void Draw(const GameTimer& gt)           override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)   override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
	void LoadIBLTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();

	void BuildOctree();
	static BoundingBox ComputeWorldBounds(
		const std::vector<Vertex>& vertices,
		const std::vector<std::uint32_t>& indices,
		UINT startIndex, UINT indexCount, INT baseVertex,
		CXMMATRIX world);

	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList,
		const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	void LoadOBJModel(const std::string& filename, const std::string& modelName);
	void LoadTextureFromFile(const std::string& path,
		const std::string& texName, int heapIndex,
		bool isSRGB = false);

	void CreateSolidTexture(const std::string& name, int heapIndex,
		uint8_t r, uint8_t g, uint8_t b, uint8_t a);

	void CreateChessboardTexture();
	void BuildDepthSRV();

private:
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int            mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature>  mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	static const UINT kGBufferSrvBaseSlot = 150;
	static const UINT kDepthSrvSlot = kGBufferSrvBaseSlot + 2;
	static const UINT kShadowSrvSlot = kGBufferSrvBaseSlot + 3;
	static const UINT kIrradianceSrvSlot = kGBufferSrvBaseSlot + 4;
	static const UINT kPrefilterSrvSlot = kGBufferSrvBaseSlot + 5;
	static const UINT kBrdfLutSrvSlot = kGBufferSrvBaseSlot + 6;

	RenderingSystem mRenderingSystem;

	ShadowMap  mShadowMap;
	ShadowPass mShadowPass;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>>     mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>>      mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>>              mShaders;
	std::unordered_map<std::string, int>                           mMaterialTextureSlot;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;

	std::unique_ptr<MeshGeometry> mOBJMesh;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<RenderItem*>                 mOpaqueRitems;

	std::vector<RenderItem*> mVisibleRitems;
	Octree                   mOctree;
	bool mFrustumCullingEnabled = false;
	bool mOctreeCullingEnabled = false;
	bool mWasNDown = false;
	bool mWasMDown = false;

	PassConstants mMainPassCB;

	XMFLOAT3   mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	bool     mIsMouseLookEnabled = false;
	XMFLOAT3 mCameraPos = { 0.0f, 1.7f, 0.0f };
	float    mCameraYaw = -XM_PI / 2.0f;
	float    mCameraPitch = 0.0f;
	float    mMoveSpeed = 5.0f;
	float    mSprintMultiplier = 2.0f;
	float    mMouseSensitivity = 0.002f;
	float    mPitchClamp = XM_PI / 2.0f - 0.1f;

	float mTheta = 1.3f * XM_PI;
	float mPhi = 0.4f * XM_PI;
	float mRadius = 2.5f;

	POINT mLastMousePos;
	POINT mCenterClient;
	POINT mCenterScreen;

	bool mWasZDown = false;

	float mGamma = 2.2f;
	bool  mEdgeDetection = false;
	bool  mVCRFilter = false;
	bool  mWas1Down = false;
	bool  mWas2Down = false;
};

static float  s_GammaResult = 2.2f;
static HWND   s_SliderHwnd = nullptr;
static HWND   s_PreviewHwnd = nullptr;
static HWND   s_LabelHwnd = nullptr;

static void GammaDrawPreview(HWND hPreview, HDC hdc)
{
	RECT rc;
	GetClientRect(hPreview, &rc);
	int W = rc.right;
	int H = rc.bottom;

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = W;
	bmi.bmiHeader.biHeight = -H;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* pBits = nullptr;
	HDC     memDC = CreateCompatibleDC(hdc);
	HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
	HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

	DWORD* pixels = (DWORD*)pBits;
	for (int x = 0; x < W; ++x)
	{
		float t = (float)x / (float)(W - 1);
		float c = powf(t, 1.0f / s_GammaResult);
		BYTE  gc = (BYTE)(c * 255.0f + 0.5f);
		DWORD col = RGB(gc, gc, gc);

		for (int y = 0; y < H; ++y)
			pixels[y * W + x] = col;
	}

	BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
	SelectObject(memDC, oldBmp);
	DeleteObject(bmp);
	DeleteDC(memDC);

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(255, 255, 0));
	RECT rLabel = { 4, 2, W, H - 2 };
	DrawText(hdc, L"Gamma-corrected output", -1, &rLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK GammaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_BAR_CLASSES };
		InitCommonControlsEx(&icex);

		HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

		CreateWindowExW(0, L"STATIC",
			L"Adjust gamma",
			WS_CHILD | WS_VISIBLE | SS_LEFT,
			12, 10, 576, 36, hwnd, nullptr, hInst, nullptr);

		s_PreviewHwnd = CreateWindowExW(0, L"STATIC", L"",
			WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
			12, 54, 576, 120, hwnd, (HMENU)(UINT_PTR)1001, hInst, nullptr);

		s_LabelHwnd = CreateWindowExW(0, L"STATIC", L"Gamma: 2.20",
			WS_CHILD | WS_VISIBLE | SS_CENTER,
			12, 184, 576, 20, hwnd, (HMENU)(UINT_PTR)1002, hInst, nullptr);

		s_SliderHwnd = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
			WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | TBS_NOTICKS,
			12, 208, 576, 36, hwnd, (HMENU)(UINT_PTR)1003, hInst, nullptr);
		SendMessage(s_SliderHwnd, TBM_SETRANGE, TRUE, MAKELPARAM(100, 300));
		SendMessage(s_SliderHwnd, TBM_SETPOS, TRUE, 220);
		SendMessage(s_SliderHwnd, TBM_SETTICFREQ, 10, 0);

		CreateWindowExW(0, L"BUTTON", L"Start Game",
			WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			238, 258, 124, 32, hwnd, (HMENU)(UINT_PTR)IDOK, hInst, nullptr);

		break;
	}

	case WM_DRAWITEM:
	{
		auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
		if (dis->CtlID == 1001)
		{
			GammaDrawPreview(dis->hwndItem, dis->hDC);
			return TRUE;
		}
		break;
	}

	case WM_HSCROLL:
	{
		if ((HWND)lParam == s_SliderHwnd)
		{
			int pos = (int)SendMessage(s_SliderHwnd, TBM_GETPOS, 0, 0);
			s_GammaResult = pos / 100.0f;

			wchar_t buf[32];
			swprintf_s(buf, L"Gamma: %.2f", s_GammaResult);
			SetWindowTextW(s_LabelHwnd, buf);
			InvalidateRect(s_PreviewHwnd, nullptr, FALSE);
		}
		break;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
			DestroyWindow(hwnd);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	return 0;
}

static float ShowGammaCalibration(HINSTANCE hInstance)
{
	s_GammaResult = 2.2f;

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = GammaWndProc;
	wc.hInstance = hInstance;
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"GammaCalibClass";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassExW(&wc);

	int screenW = GetSystemMetrics(SM_CXSCREEN);
	int screenH = GetSystemMetrics(SM_CYSCREEN);
	int dlgW = 620, dlgH = 320;
	int posX = (screenW - dlgW) / 2;
	int posY = (screenH - dlgH) / 2;

	HWND hwnd = CreateWindowExW(
		WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
		L"GammaCalibClass",
		L"Gamma Calibration",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
		posX, posY, dlgW, dlgH,
		nullptr, nullptr, hInstance, nullptr);

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	MSG msg = {};
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	UnregisterClassW(L"GammaCalibClass", hInstance);
	return s_GammaResult;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	try
	{
		float gamma = ShowGammaCalibration(hInstance);

		CrateApp theApp(hInstance);
		theApp.SetGamma(gamma);
		if (!theApp.Initialize())
			return 0;
		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

CrateApp::CrateApp(HINSTANCE hInstance) : D3DApp(hInstance) {}

CrateApp::~CrateApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool CrateApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	LoadIBLTextures();

	CreateSolidTexture("defaultNormal", kDefaultNormalSlot, 128, 128, 255, 255);
	CreateSolidTexture("defaultDisp", kDefaultDispSlot, 128, 128, 128, 255);
	CreateSolidTexture("defaultRoughness", kDefaultRoughnessSlot, 128, 128, 128, 255);
	CreateSolidTexture("defaultMetallic", kDefaultMetallicSlot, 0, 0, 0, 255);
	CreateSolidTexture("defaultAO", kDefaultAOSlot, 255, 255, 255, 255);
	CreateSolidTexture("defaultDiffuse", kDefaultDiffuseSlot, 255, 255, 255, 255);

	CreateChessboardTexture();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	//LoadOBJModel("sponza.obj", "myModel");
	LoadOBJModel("Cerberus_LP.obj", "1myModel");
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();
	BuildOctree();

	mRenderingSystem.Initialize(
		md3dDevice.Get(),
		mBackBufferFormat,
		mDepthStencilFormat,
		mClientWidth, mClientHeight,
		md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
		mCbvSrvDescriptorSize,
		mSrvDescriptorHeap.Get(),
		kGBufferSrvBaseSlot,
		mRootSignature.Get());

	BuildDepthSRV();

	mShadowMap.Create(
		md3dDevice.Get(),
		mSrvDescriptorHeap.Get(),
		kShadowSrvSlot,
		mCbvSrvDescriptorSize);

	mShadowPass.Initialize(
		md3dDevice.Get(),
		ShadowMap::DSVFormat,
		mInputLayout);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();

	return true;
}

void CrateApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);

	if (mSrvDescriptorHeap)
	{
		mRenderingSystem.OnResize(
			md3dDevice.Get(),
			mClientWidth, mClientHeight,
			mSrvDescriptorHeap.Get(),
			kGBufferSrvBaseSlot);

		BuildDepthSRV();
	}
}

void CrateApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	char title[256];
	sprintf_s(title,
		"DX12 - fps: %.1f | visible: %zu / %zu | Simple FC: %s | Octree FC: %s",
		1.0f / gt.DeltaTime(),
		mVisibleRitems.size(),
		mOpaqueRitems.size(),
		mFrustumCullingEnabled ? "ON" : "OFF",
		mOctreeCullingEnabled ? "ON" : "OFF");

	SetWindowTextA(mhMainWnd, title);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	if (mCurrFrameResource->Fence != 0 &&
		mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE ev = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, ev));
		WaitForSingleObject(ev, INFINITE);
		CloseHandle(ev);
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

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(),
		mRenderingSystem.GetGeometryPSO()));

	BoundingFrustum frustum;
	BoundingFrustum::CreateFromMatrix(frustum, XMLoadFloat4x4(&mProj));
	{
		XMVECTOR det;
		XMMATRIX invView = XMMatrixInverse(&det, XMLoadFloat4x4(&mView));
		frustum.Transform(frustum, invView);
	}

	if (mOctreeCullingEnabled)
	{
		static std::vector<UINT> visIdx;
		mOctree.Query(frustum, visIdx);
		mVisibleRitems.clear();
		mVisibleRitems.reserve(visIdx.size());
		for (UINT i : visIdx)
			mVisibleRitems.push_back(mOpaqueRitems[i]);
	}
	else if (mFrustumCullingEnabled)
	{
		mVisibleRitems.clear();
		mVisibleRitems.reserve(mOpaqueRitems.size());
		for (auto* ri : mOpaqueRitems)
			if (frustum.Intersects(ri->WorldBounds))
				mVisibleRitems.push_back(ri);
	}
	else
	{
		mVisibleRitems = mOpaqueRitems;
	}

	static int debugFrameCounter = 0;
	if (++debugFrameCounter % 60 == 0)
	{
		OutputDebugStringA(("Visible: " + std::to_string(mVisibleRitems.size()) +
			" of " + std::to_string(mOpaqueRitems.size()) + "\n").c_str());
	}

	ID3D12DescriptorHeap* heaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

	const UINT objCBSize =
		d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto* objectCBResource = mCurrFrameResource->ObjectCB->Resource();

	mShadowPass.Render(
		mCommandList.Get(),
		mShadowMap,
		*mCurrFrameResource,
		[&](ID3D12GraphicsCommandList* cl, int)
		{
			for (const auto* ri : mOpaqueRitems)
			{
				auto vbv = ri->Geo->VertexBufferView();
				auto ibv = ri->Geo->IndexBufferView();
				cl->IASetVertexBuffers(0, 1, &vbv);
				cl->IASetIndexBuffer(&ibv);

				D3D12_GPU_VIRTUAL_ADDRESS objAddr =
					objectCBResource->GetGPUVirtualAddress()
					+ (UINT64)ri->ObjCBIndex * objCBSize;
				cl->SetGraphicsRootConstantBufferView(0, objAddr);

				cl->DrawIndexedInstanced(
					ri->IndexCount, 1,
					ri->StartIndexLocation,
					ri->BaseVertexLocation, 0);
			}
		});

	mRenderingSystem.BeginGeometryPass(
		mCommandList.Get(),
		DepthStencilView(),
		mScreenViewport,
		mScissorRect);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(kRootPassCB,
		passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mVisibleRitems);

	mRenderingSystem.EndGeometryPass(mCommandList.Get());

	auto bbBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &bbBarrier);

	auto depthToSRV = CD3DX12_RESOURCE_BARRIER::Transition(
		mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &depthToSRV);

	mRenderingSystem.BeginLightingPass(
		mCommandList.Get(),
		CurrentBackBufferView(),
		DepthStencilView(),
		mScreenViewport,
		mScissorRect);

	CD3DX12_GPU_DESCRIPTOR_HANDLE depthSRV(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		kDepthSrvSlot, mCbvSrvDescriptorSize);

	CD3DX12_GPU_DESCRIPTOR_HANDLE irradianceSRV(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		kIrradianceSrvSlot, mCbvSrvDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE prefilterSRV(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		kPrefilterSrvSlot, mCbvSrvDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE brdfLutSRV(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		kBrdfLutSrvSlot, mCbvSrvDescriptorSize);

	mRenderingSystem.DrawLightingPass(
		mCommandList.Get(),
		mSrvDescriptorHeap.Get(),
		passCB->GetGPUVirtualAddress(),
		depthSRV,
		mShadowMap.GetSRV(),
		irradianceSRV,
		prefilterSRV,
		brdfLutSRV);

	auto depthToWrite = CD3DX12_RESOURCE_BARRIER::Transition(
		mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCommandList->ResourceBarrier(1, &depthToWrite);

	bbBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &bbBarrier);

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CrateApp::UpdateMainPassCB(const GameTimer& gt)
{
	mEyePos = mCameraPos;

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	auto viewDet = XMMatrixDeterminant(view);
	auto projDet = XMMatrixDeterminant(proj);
	auto vpDet = XMMatrixDeterminant(viewProj);
	XMMATRIX invView = XMMatrixInverse(&viewDet, view);
	XMMATRIX invProj = XMMatrixInverse(&projDet, proj);
	XMMATRIX invVP = XMMatrixInverse(&vpDet, viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invVP));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.15f, 0.15f, 0.15f, 1.0f };

	//Directional light
	mMainPassCB.Lights[0].Direction = { 0.3f, -1.0f, 0.3f };
	mMainPassCB.Lights[0].Strength = { 6.0f,  6.0f,  6.0f };

	//Point light
	mMainPassCB.Lights[1].Position = { 15.0f, 1.5f, 0.0f };
	mMainPassCB.Lights[1].Strength = { 5.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[1].FalloffStart = 3.0f;
	mMainPassCB.Lights[1].FalloffEnd = 8.0f;

	//Spot light
	{
		int idx = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS;
		XMFLOAT3 forward;
		forward.x = cosf(mCameraYaw) * cosf(mCameraPitch);
		forward.y = sinf(mCameraPitch);
		forward.z = sinf(mCameraYaw) * cosf(mCameraPitch);
		mMainPassCB.Lights[idx].Position = mCameraPos;
		mMainPassCB.Lights[idx].Direction = forward;
		mMainPassCB.Lights[idx].Strength = { 0.0f, 0.0f, 5.0f };
		mMainPassCB.Lights[idx].FalloffStart = 3.0f;
		mMainPassCB.Lights[idx].FalloffEnd = 8.0f;
		mMainPassCB.Lights[idx].SpotPower = 320.0f;
	}

	mShadowPass.UpdateCascades(
		mMainPassCB,
		proj,
		invView,
		mMainPassCB.Lights[0].Direction);

	for (int i = 0; i < NUM_CASCADES; ++i)
	{
		ShadowPassConstants spc;
		spc.LightViewProj = mMainPassCB.LightViewProj[i];
		mCurrFrameResource->ShadowPassCB->CopyData(i, spc);
	}

	mMainPassCB.Gamma = mGamma;
	mMainPassCB.EdgeDetection = mEdgeDetection ? 1 : 0;
	mMainPassCB.VCRFilter = mVCRFilter ? 1 : 0;
	mCurrFrameResource->PassCB->CopyData(0, mMainPassCB);
}

void CrateApp::BuildDepthSRV()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		kDepthSrvSlot, mCbvSrvDescriptorSize);

	md3dDevice->CreateShaderResourceView(mDepthStencilBuffer.Get(), &srvDesc, handle);
}

void CrateApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE diffuseTable;
	diffuseTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE normalTable;
	normalTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_DESCRIPTOR_RANGE dispTable;
	dispTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

	CD3DX12_DESCRIPTOR_RANGE roughnessTable;
	roughnessTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

	CD3DX12_DESCRIPTOR_RANGE metallicTable;
	metallicTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

	CD3DX12_ROOT_PARAMETER params[8];
	params[kRootDiffuse].InitAsDescriptorTable(1, &diffuseTable, D3D12_SHADER_VISIBILITY_PIXEL);
	params[kRootObjCB].InitAsConstantBufferView(0);
	params[kRootPassCB].InitAsConstantBufferView(1);
	params[kRootMatCB].InitAsConstantBufferView(2);
	params[kRootNormal].InitAsDescriptorTable(1, &normalTable, D3D12_SHADER_VISIBILITY_PIXEL);
	params[kRootDisplace].InitAsDescriptorTable(1, &dispTable, D3D12_SHADER_VISIBILITY_DOMAIN);
	params[kRootRoughness].InitAsDescriptorTable(1, &roughnessTable, D3D12_SHADER_VISIBILITY_PIXEL);
	params[kRootMetallic].InitAsDescriptorTable(1, &metallicTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC desc(
		_countof(params), params,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serialized, error;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), error.GetAddressOf());
	if (error) OutputDebugStringA((char*)error->GetBufferPointer());
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CrateApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 210;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&heapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDesc(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	auto woodTex = mTextures["woodCrateTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = woodTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(woodTex.Get(), &srvDesc, hDesc);
}

void CrateApp::BuildShadersAndInputLayout()
{
	mShaders["geometryVS"] = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["geometryHS"] = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "HS", "hs_5_0");
	mShaders["geometryDS"] = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "DS", "ds_5_0");
	mShaders["geometryPS"] = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "PS", "ps_5_0");

	mShaders["lightingVS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["lightingPS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32_FLOAT,    0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void CrateApp::BuildPSOs() {}

void CrateApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;

	{
		auto chessboard = geoGen.CreateChessboard(5.0f, 5.0f, 8, 0.0f);

		SubmeshGeometry sub;
		sub.IndexCount = (UINT)chessboard.Indices32.size();
		sub.StartIndexLocation = 0;
		sub.BaseVertexLocation = 0;

		std::vector<Vertex> verts(chessboard.Vertices.size());
		for (size_t i = 0; i < verts.size(); ++i)
		{
			verts[i].Pos = chessboard.Vertices[i].Position;
			verts[i].Normal = chessboard.Vertices[i].Normal;
			verts[i].TexC = chessboard.Vertices[i].TexC;
			verts[i].Color = chessboard.Vertices[i].Color;
			verts[i].Tangent = { 1.0f, 1.0f, 1.0f };
		}
		auto& inds = chessboard.Indices32;

		const UINT vbBytes = (UINT)verts.size() * sizeof(Vertex);
		const UINT ibBytes = (UINT)inds.size() * sizeof(std::uint32_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "chessboardGeo";
		ThrowIfFailed(D3DCreateBlob(vbBytes, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), verts.data(), vbBytes);
		ThrowIfFailed(D3DCreateBlob(ibBytes, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), inds.data(), ibBytes);
		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), verts.data(), vbBytes, geo->VertexBufferUploader);
		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), inds.data(), ibBytes, geo->IndexBufferUploader);
		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbBytes;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibBytes;
		geo->DrawArgs["chessboard"] = sub;
		mGeometries[geo->Name] = std::move(geo);
	}

	{
		auto box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);

		SubmeshGeometry sub;
		sub.IndexCount = (UINT)box.Indices32.size();
		sub.StartIndexLocation = 0;
		sub.BaseVertexLocation = 0;

		std::vector<Vertex> verts(box.Vertices.size());
		for (size_t i = 0; i < verts.size(); ++i)
		{
			verts[i].Pos = box.Vertices[i].Position;
			verts[i].Normal = box.Vertices[i].Normal;
			verts[i].TexC = box.Vertices[i].TexC;
			verts[i].Tangent = box.Vertices[i].TangentU;
		}
		auto& inds = box.Indices32;

		const UINT vbBytes = (UINT)verts.size() * sizeof(Vertex);
		const UINT ibBytes = (UINT)inds.size() * sizeof(std::uint32_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "boxGeo";
		ThrowIfFailed(D3DCreateBlob(vbBytes, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), verts.data(), vbBytes);
		ThrowIfFailed(D3DCreateBlob(ibBytes, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), inds.data(), ibBytes);
		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), verts.data(), vbBytes, geo->VertexBufferUploader);
		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), inds.data(), ibBytes, geo->IndexBufferUploader);
		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbBytes;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibBytes;
		geo->DrawArgs["box"] = sub;
		mGeometries[geo->Name] = std::move(geo);
	}

	{
		auto sphere = geoGen.CreateSphere(0.5f, 20, 20);

		SubmeshGeometry sub;
		sub.IndexCount = (UINT)sphere.Indices32.size();
		sub.StartIndexLocation = 0;
		sub.BaseVertexLocation = 0;

		std::vector<Vertex> verts(sphere.Vertices.size());
		for (size_t i = 0; i < verts.size(); ++i)
		{
			verts[i].Pos = sphere.Vertices[i].Position;
			verts[i].Normal = sphere.Vertices[i].Normal;
			verts[i].TexC = sphere.Vertices[i].TexC;
			verts[i].Tangent = sphere.Vertices[i].TangentU;
		}
		auto& inds = sphere.Indices32;

		const UINT vbBytes = (UINT)verts.size() * sizeof(Vertex);
		const UINT ibBytes = (UINT)inds.size() * sizeof(std::uint32_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "sphereGeo";
		ThrowIfFailed(D3DCreateBlob(vbBytes, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), verts.data(), vbBytes);
		ThrowIfFailed(D3DCreateBlob(ibBytes, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), inds.data(), ibBytes);
		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), verts.data(), vbBytes, geo->VertexBufferUploader);
		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), inds.data(), ibBytes, geo->IndexBufferUploader);
		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbBytes;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibBytes;
		geo->DrawArgs["sphere"] = sub;
		mGeometries[geo->Name] = std::move(geo);
	}
}

void CrateApp::BuildMaterials()
{
	{
		auto woodCrate = std::make_unique<Material>();
		woodCrate->Name = "woodCrate";
		woodCrate->MatCBIndex = 0;
		woodCrate->DiffuseSrvHeapIndex = 0;
		woodCrate->NormalSrvHeapIndex = kDefaultNormalSlot;
		woodCrate->DispSrvHeapIndex = kDefaultDispSlot;
		woodCrate->DiffuseAlbedo = XMFLOAT4(1, 1, 1, 1);
		woodCrate->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
		woodCrate->Roughness = 0.2f;
		woodCrate->DispScale = 0.0f;
		woodCrate->RoughnessSrvHeapIndex = kDefaultRoughnessSlot;
		woodCrate->MetallicSrvHeapIndex = kDefaultMetallicSlot;
		woodCrate->AOSrvHeapIndex = kDefaultAOSlot;
		mMaterials["woodCrate"] = std::move(woodCrate);
	}
	{
		auto chess = std::make_unique<Material>();
		chess->Name = "chessboard";
		chess->MatCBIndex = 1;
		chess->DiffuseSrvHeapIndex = 1;
		chess->NormalSrvHeapIndex = kDefaultNormalSlot;
		chess->DispSrvHeapIndex = kDefaultDispSlot;
		chess->DiffuseAlbedo = XMFLOAT4(1, 1, 1, 1);
		chess->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
		chess->Roughness = 0.5f;
		chess->DispScale = 0.0f;
		chess->RoughnessSrvHeapIndex = kDefaultRoughnessSlot;
		chess->MetallicSrvHeapIndex = kDefaultMetallicSlot;
		chess->AOSrvHeapIndex = kDefaultAOSlot;
		mMaterials["chessboard"] = std::move(chess);
	}
	{
		auto boxMat = std::make_unique<Material>();
		boxMat->Name = "boxMat";
		boxMat->MatCBIndex = 2;
		boxMat->DiffuseSrvHeapIndex = 0;
		boxMat->NormalSrvHeapIndex = kDefaultNormalSlot;
		boxMat->DispSrvHeapIndex = kDefaultDispSlot;
		boxMat->DiffuseAlbedo = XMFLOAT4(0.8f, 0.6f, 0.3f, 1.0f);
		boxMat->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
		boxMat->Roughness = 0.6f;
		boxMat->DispScale = 0.0f;
		boxMat->RoughnessSrvHeapIndex = kDefaultRoughnessSlot;
		boxMat->MetallicSrvHeapIndex = kDefaultMetallicSlot;
		boxMat->AOSrvHeapIndex = kDefaultAOSlot;
		mMaterials["boxMat"] = std::move(boxMat);
	}
	{
		auto metalMat = std::make_unique<Material>();
		metalMat->Name = "metalSphere";
		metalMat->MatCBIndex = 3;
		metalMat->DiffuseSrvHeapIndex = kDefaultDiffuseSlot;
		metalMat->NormalSrvHeapIndex = kDefaultNormalSlot;
		metalMat->DispSrvHeapIndex = kDefaultDispSlot;
		metalMat->RoughnessSrvHeapIndex = kDefaultRoughnessSlot;
		metalMat->MetallicSrvHeapIndex = kDefaultMetallicSlot;
		metalMat->AOSrvHeapIndex = kDefaultAOSlot;

		metalMat->DiffuseAlbedo = XMFLOAT4(1.0f, 0.78f, 0.34f, 1.0f);
		metalMat->FresnelR0 = XMFLOAT3(0.95f, 0.64f, 0.54f);
		metalMat->Roughness = 0.05f;
		metalMat->Metallic = 1.0f;
		metalMat->DispScale = 0.0f;

		mMaterials["metalSphere"] = std::move(metalMat);
	}
}

void CrateApp::BuildRenderItems()
{
	{
		auto item = std::make_unique<RenderItem>();
		item->ObjCBIndex = (UINT)mAllRitems.size();
		item->NumFramesDirty = gNumFrameResources;
		item->Mat = mMaterials["chessboard"].get();
		item->Geo = mGeometries["chessboardGeo"].get();
		item->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		item->IndexCount = item->Geo->DrawArgs["chessboard"].IndexCount;
		item->StartIndexLocation = item->Geo->DrawArgs["chessboard"].StartIndexLocation;
		item->BaseVertexLocation = item->Geo->DrawArgs["chessboard"].BaseVertexLocation;

		XMMATRIX world = XMMatrixTranslation(0.0f, 3.0f, 0.0f);
		XMStoreFloat4x4(&item->World, world);

		BoundingBox localBounds(XMFLOAT3(0, 0, 0), XMFLOAT3(2.5f, 0.02f, 2.5f));
		localBounds.Transform(item->WorldBounds, world);

		mAllRitems.push_back(std::move(item));
	}

	{
		std::mt19937 rng(12345u);
		std::uniform_real_distribution<float> distXZ(-80.0f, 80.0f);
		std::uniform_real_distribution<float> distScale(0.3f, 2.0f);
		std::uniform_real_distribution<float> distRotY(0.0f, XM_2PI);

		const BoundingBox localBoxBounds(XMFLOAT3(0, 0, 0), XMFLOAT3(0.5f, 0.5f, 0.5f));
		auto* boxGeo = mGeometries["boxGeo"].get();
		auto* boxMat = mMaterials["boxMat"].get();
		const SubmeshGeometry& boxSub = boxGeo->DrawArgs.at("box");

		for (int i = 0; i < kNumScatteredObjects; ++i)
		{
			float x = distXZ(rng);
			float z = distXZ(rng);
			float s = distScale(rng);
			float rotY = distRotY(rng);

			XMMATRIX world =
				XMMatrixScaling(s, s, s)
				* XMMatrixRotationY(rotY)
				* XMMatrixTranslation(x, s * 0.5f, z);

			auto item = std::make_unique<RenderItem>();
			item->ObjCBIndex = (UINT)mAllRitems.size();
			item->NumFramesDirty = gNumFrameResources;
			item->Mat = boxMat;
			item->Geo = boxGeo;
			item->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
			item->IndexCount = boxSub.IndexCount;
			item->StartIndexLocation = boxSub.StartIndexLocation;
			item->BaseVertexLocation = boxSub.BaseVertexLocation;
			XMStoreFloat4x4(&item->World, world);
			localBoxBounds.Transform(item->WorldBounds, world);
			mAllRitems.push_back(std::move(item));
		}
	}

	{
		auto item = std::make_unique<RenderItem>();
		item->ObjCBIndex = (UINT)mAllRitems.size();
		item->NumFramesDirty = gNumFrameResources;
		item->Mat = mMaterials["metalSphere"].get();
		item->Geo = mGeometries["sphereGeo"].get();
		item->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		item->IndexCount = item->Geo->DrawArgs["sphere"].IndexCount;
		item->StartIndexLocation = item->Geo->DrawArgs["sphere"].StartIndexLocation;
		item->BaseVertexLocation = item->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMMATRIX world = XMMatrixScaling(5.0f, 5.0f, 5.0f)
			* XMMatrixTranslation(0.0f, 1.5f, 10.0f);
		XMStoreFloat4x4(&item->World, world);

		BoundingBox localBounds(XMFLOAT3(0, 0, 0), XMFLOAT3(0.5f, 0.5f, 0.5f));
		localBounds.Transform(item->WorldBounds, world);

		mAllRitems.push_back(std::move(item));
	}

	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void CrateApp::BuildOctree()
{
	std::vector<BoundingBox> bounds;
	bounds.reserve(mOpaqueRitems.size());
	for (const auto* ri : mOpaqueRitems)
		bounds.push_back(ri->WorldBounds);
	mOctree.Build(bounds, 7, 8);
}

BoundingBox CrateApp::ComputeWorldBounds(
	const std::vector<Vertex>& vertices,
	const std::vector<std::uint32_t>& indices,
	UINT startIndex, UINT indexCount, INT baseVertex,
	CXMMATRIX world)
{
	XMVECTOR mn = XMVectorReplicate(+FLT_MAX);
	XMVECTOR mx = XMVectorReplicate(-FLT_MAX);

	for (UINT k = startIndex; k < startIndex + indexCount; ++k)
	{
		const UINT vi = (UINT)((INT)indices[k] + baseVertex);
		if (vi >= (UINT)vertices.size()) continue;
		XMVECTOR p = XMVector3Transform(XMLoadFloat3(&vertices[vi].Pos), world);
		mn = XMVectorMin(mn, p);
		mx = XMVectorMax(mx, p);
	}

	BoundingBox result;
	XMStoreFloat3(&result.Center,
		XMVectorScale(XMVectorAdd(mn, mx), 0.5f));
	XMStoreFloat3(&result.Extents,
		XMVectorMax(XMVectorScale(XMVectorSubtract(mx, mn), 0.5f),
			XMVectorReplicate(0.01f)));
	return result;
}

void CrateApp::CreateSolidTexture(const std::string& name, int heapIndex,
	uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint8_t pixel[4] = { r, g, b, a };

	auto tex = std::make_unique<Texture>();
	tex->Name = name;

	D3D12_RESOURCE_DESC td = {};
	td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = 1; td.Height = 1;
	td.DepthOrArraySize = 1; td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex->Resource)));

	UINT64 upSize = GetRequiredIntermediateSize(tex->Resource.Get(), 0, 1);
	CD3DX12_HEAP_PROPERTIES upHp(D3D12_HEAP_TYPE_UPLOAD);
	auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(upSize);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tex->UploadHeap)));

	D3D12_SUBRESOURCE_DATA sd = {};
	sd.pData = pixel; sd.RowPitch = 4; sd.SlicePitch = 4;

	auto b0 = CD3DX12_RESOURCE_BARRIER::Transition(tex->Resource.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	mCommandList->ResourceBarrier(1, &b0);
	UpdateSubresources(mCommandList.Get(), tex->Resource.Get(), tex->UploadHeap.Get(), 0, 0, 1, &sd);
	auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(tex->Resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &b1);

	CD3DX12_CPU_DESCRIPTOR_HANDLE h(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	h.Offset(heapIndex, mCbvSrvDescriptorSize);
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sv.Texture2D.MipLevels = 1;
	md3dDevice->CreateShaderResourceView(tex->Resource.Get(), &sv, h);
	mTextures[name] = std::move(tex);
}

bool LoadTGAFile(const std::wstring& filename, std::vector<uint8_t>& outData,
	int& width, int& height, DXGI_FORMAT& format)
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
		std::swap(outData[offset], outData[offset + 2]);
	}

	format = DXGI_FORMAT_R8G8B8A8_UNORM;
	return true;
}

void CrateApp::LoadOBJModel(const std::string& filename, const std::string& modelName)
{
	auto GetDefaultSlotForType = [&](const std::string& prefix) -> int
		{
			if (prefix == "tex_diff_") return 0;
			if (prefix == "tex_norm_") return kDefaultNormalSlot;
			if (prefix == "tex_rough_") return kDefaultRoughnessSlot;
			if (prefix == "tex_met_") return kDefaultMetallicSlot;
			if (prefix == "tex_ao_") return kDefaultAOSlot;
			if (prefix == "tex_disp_") return kDefaultDispSlot;
			return 0;
		};
	OBJMeshData mesh;
	std::vector<OBJMaterialData> materials;
	if (!OBJLoader::Load(filename, mesh, materials)) return;

	std::vector<Vertex> vertices;
	for (const auto& v : mesh.Vertices)
	{
		Vertex vert;
		vert.Pos = v.Position;
		vert.Normal = v.Normal;
		vert.TexC = v.TexCoord;
		vert.Tangent = v.Tangent;
		vertices.push_back(vert);
	}

	const UINT vbBytes = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibBytes = (UINT)mesh.Indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = modelName;
	ThrowIfFailed(D3DCreateBlob(vbBytes, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbBytes);
	ThrowIfFailed(D3DCreateBlob(ibBytes, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), mesh.Indices.data(), ibBytes);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbBytes, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), mesh.Indices.data(), ibBytes, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbBytes;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibBytes;
	geo->DrawArgs = mesh.DrawArgs;
	mGeometries[geo->Name] = std::move(geo);

	std::string modelDir;
	size_t slash = filename.find_last_of("\\/");
	if (slash != std::string::npos) modelDir = filename.substr(0, slash + 1);

	int matIndex = (int)mMaterials.size();
	int nextHeapSlot = kObjTexBaseSlot;

	auto resolveTexPath = [&](const std::string& p) -> std::string
		{
			if (p.empty()) return p;
			if (p.find(':') != std::string::npos || p.front() == '/' || p.front() == '\\')
				return p;
			return modelDir + p;
		};

	for (const auto& mtl : materials)
	{
		auto mat = std::make_unique<Material>();
		mat->Name = mtl.Name;
		mat->MatCBIndex = matIndex++;
		mat->DiffuseAlbedo = mtl.DiffuseAlbedo;
		mat->FresnelR0 = mtl.FresnelR0;
		mat->Roughness = mtl.Roughness;
		mat->Metallic = mtl.Metallic;
		mat->AOScale = mtl.AOScale;
		mat->MatTransform = MathHelper::Identity4x4();
		mat->NumFramesDirty = gNumFrameResources;

		mat->DiffuseSrvHeapIndex = 0;
		mat->NormalSrvHeapIndex = kDefaultNormalSlot;
		mat->DispSrvHeapIndex = kDefaultDispSlot;
		mat->RoughnessSrvHeapIndex = kDefaultRoughnessSlot;
		mat->MetallicSrvHeapIndex = kDefaultMetallicSlot;
		mat->AOSrvHeapIndex = kDefaultAOSlot;
		mat->DispScale = 0.0f;

		auto loadTex = [&](const std::string& rawPath,
			const std::string& prefix, int& outSlot,
			bool isSRGB = false)
			{
				if (rawPath.empty())
				{
					outSlot = GetDefaultSlotForType(prefix);
					return;
				}

				std::string texName = prefix + mtl.Name;
				auto it = mMaterialTextureSlot.find(texName);
				if (it == mMaterialTextureSlot.end())
				{
					LoadTextureFromFile(resolveTexPath(rawPath), texName, nextHeapSlot, isSRGB);
					outSlot = nextHeapSlot;
					mMaterialTextureSlot[texName] = nextHeapSlot;
					++nextHeapSlot;
				}
				else
				{
					outSlot = it->second;
				}
			};

		loadTex(mtl.DiffuseMapPath, "tex_diff_", mat->DiffuseSrvHeapIndex, true);
		loadTex(mtl.NormalMapPath, "tex_norm_", mat->NormalSrvHeapIndex, false);
		loadTex(mtl.RoughnessMapPath, "tex_rough_", mat->RoughnessSrvHeapIndex, false);
		loadTex(mtl.MetallicMapPath, "tex_met_", mat->MetallicSrvHeapIndex, false);
		loadTex(mtl.AOMapPath, "tex_ao_", mat->AOSrvHeapIndex, false);
		if (!mtl.DisplaceMapPath.empty())
		{
			loadTex(mtl.DisplaceMapPath, "tex_disp_", mat->DispSrvHeapIndex, false);
			mat->DispScale = 0.15f;
		}

		mMaterials[mat->Name] = std::move(mat);
	}

	const XMMATRIX world = XMMatrixScaling(0.2f, 0.2f, 0.2f);

	for (const auto& drawArg : mesh.DrawArgs)
	{
		auto item = std::make_unique<RenderItem>();
		item->ObjCBIndex = (UINT)mAllRitems.size();
		item->NumFramesDirty = gNumFrameResources;
		item->TexTransform = MathHelper::Identity4x4();

		auto matIt = mesh.SubmeshToMaterial.find(drawArg.first);
		if (matIt != mesh.SubmeshToMaterial.end())
		{
			auto mIt = mMaterials.find(matIt->second);
			item->Mat = (mIt != mMaterials.end())
				? mIt->second.get()
				: mMaterials["woodCrate"].get();
		}
		else { item->Mat = mMaterials["woodCrate"].get(); }

		item->Geo = mGeometries[modelName].get();
		item->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		item->IndexCount = drawArg.second.IndexCount;
		item->StartIndexLocation = drawArg.second.StartIndexLocation;
		item->BaseVertexLocation = drawArg.second.BaseVertexLocation;
		XMStoreFloat4x4(&item->World, world);

		item->WorldBounds = ComputeWorldBounds(
			vertices, mesh.Indices,
			drawArg.second.StartIndexLocation, drawArg.second.IndexCount,
			drawArg.second.BaseVertexLocation,
			world);

		mAllRitems.push_back(std::move(item));
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
	XMFLOAT3 forward = {
		cosf(mCameraYaw) * cosf(mCameraPitch),
		sinf(mCameraPitch),
		sinf(mCameraYaw) * cosf(mCameraPitch)
	};
	XMFLOAT3 right;
	XMStoreFloat3(&right, XMVector3Normalize(
		XMVector3Cross(XMLoadFloat3(&forward), XMVectorSet(0.f, 1.f, 0.f, 0.f))));

	bool  sprint = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	float speed = mMoveSpeed * (sprint ? mSprintMultiplier : 1.0f) * gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000) { mCameraPos.x += forward.x * speed; mCameraPos.y += forward.y * speed; mCameraPos.z += forward.z * speed; }
	if (GetAsyncKeyState('S') & 0x8000) { mCameraPos.x -= forward.x * speed; mCameraPos.y -= forward.y * speed; mCameraPos.z -= forward.z * speed; }
	if (GetAsyncKeyState('A') & 0x8000) { mCameraPos.x += right.x * speed;   mCameraPos.z += right.z * speed; }
	if (GetAsyncKeyState('D') & 0x8000) { mCameraPos.x -= right.x * speed;   mCameraPos.z -= right.z * speed; }
	if (GetAsyncKeyState('Q') & 0x8000) mCameraPos.y -= speed;
	if (GetAsyncKeyState('E') & 0x8000) mCameraPos.y += speed;

	bool isZDown = (GetAsyncKeyState('Z') & 0x8000) != 0;
	if (isZDown && !mWasZDown) mRenderingSystem.ToggleWireframe();
	mWasZDown = isZDown;

	bool is1Down = (GetAsyncKeyState('1') & 0x8000) != 0;
	if (is1Down && !mWas1Down)
	{
		mEdgeDetection = !mEdgeDetection;
		OutputDebugStringA(mEdgeDetection
			? "[PostFX] Edge Detection: ON\n"
			: "[PostFX] Edge Detection: OFF\n");
	}
	mWas1Down = is1Down;

	bool is2Down = (GetAsyncKeyState('2') & 0x8000) != 0;
	if (is2Down && !mWas2Down)
	{
		mVCRFilter = !mVCRFilter;
		OutputDebugStringA(mVCRFilter
			? "[PostFX] VCR Filter: ON\n"
			: "[PostFX] VCR Filter: OFF\n");
	}
	mWas2Down = is2Down;

	bool isNDown = (GetAsyncKeyState('N') & 0x8000) != 0;
	if (isNDown && !mWasNDown)
	{
		mFrustumCullingEnabled = !mFrustumCullingEnabled;
		OutputDebugStringA(mFrustumCullingEnabled
			? "[Culling] Simple frustum culling: ON\n"
			: "[Culling] Simple frustum culling: OFF\n");
	}
	mWasNDown = isNDown;

	bool isMDown = (GetAsyncKeyState('M') & 0x8000) != 0;
	if (isMDown && !mWasMDown)
	{
		mOctreeCullingEnabled = !mOctreeCullingEnabled;
		OutputDebugStringA(mOctreeCullingEnabled
			? "[Culling] Octree frustum culling: ON\n"
			: "[Culling] Octree frustum culling: OFF\n");
	}
	mWasMDown = isMDown;
}

void CrateApp::UpdateCamera(const GameTimer& gt)
{
	XMFLOAT3 forward = {
		cosf(mCameraYaw) * cosf(mCameraPitch),
		sinf(mCameraPitch),
		sinf(mCameraYaw) * cosf(mCameraPitch)
	};
	XMVECTOR pos = XMLoadFloat3(&mCameraPos);
	XMVECTOR target = XMVectorAdd(pos, XMLoadFloat3(&forward));
	XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
	XMStoreFloat4x4(&mView, XMMatrixLookAtLH(pos, target, up));
	mEyePos = mCameraPos;
}

void CrateApp::AnimateMaterials(const GameTimer& gt) {}

void CrateApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			ObjectConstants oc;
			XMStoreFloat4x4(&oc.World, XMMatrixTranspose(XMLoadFloat4x4(&e->World)));
			XMStoreFloat4x4(&oc.TexTransform, XMMatrixTranspose(XMLoadFloat4x4(&e->TexTransform)));
			oc.IsChessboard = (e->Mat && e->Mat->Name == "chessboard") ? 1 : 0;
			currObjectCB->CopyData(e->ObjCBIndex, oc);
			e->NumFramesDirty--;
		}
	}
}

void CrateApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMatCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			MaterialConstants mc;
			mc.DiffuseAlbedo = mat->DiffuseAlbedo;
			mc.FresnelR0 = mat->FresnelR0;
			mc.Roughness = mat->Roughness;
			mc.Metallic = mat->Metallic;
			mc.AOScale = mat->AOScale;
			XMStoreFloat4x4(&mc.MatTransform,
				XMMatrixTranspose(XMLoadFloat4x4(&mat->MatTransform)));
			mc.DispScale = mat->DispScale;
			currMatCB->CopyData(mat->MatCBIndex, mc);
			mat->NumFramesDirty--;
		}
	}
}

void CrateApp::CreateChessboardTexture()
{
	const int texSize = 64, sqSize = 8;
	std::vector<uint8_t> pixels(texSize * texSize * 4);
	for (int i = 0; i < texSize; ++i)
		for (int j = 0; j < texSize; ++j)
		{
			bool white = ((j / sqSize) + (i / sqSize)) % 2 == 0;
			int  idx = (i * texSize + j) * 4;
			pixels[idx + 0] = white ? 245 : 139;
			pixels[idx + 1] = white ? 222 : 69;
			pixels[idx + 2] = white ? 179 : 19;
			pixels[idx + 3] = 255;
		}

	auto tex = std::make_unique<Texture>();
	tex->Name = "chessboardTex";

	D3D12_RESOURCE_DESC td = {};
	td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = texSize; td.Height = texSize;
	td.DepthOrArraySize = 1; td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex->Resource)));

	UINT64 upSize = GetRequiredIntermediateSize(tex->Resource.Get(), 0, 1);
	CD3DX12_HEAP_PROPERTIES upHp(D3D12_HEAP_TYPE_UPLOAD);
	auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(upSize);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tex->UploadHeap)));

	D3D12_SUBRESOURCE_DATA sd = {};
	sd.pData = pixels.data();
	sd.RowPitch = texSize * 4;
	sd.SlicePitch = sd.RowPitch * texSize;

	auto b = CD3DX12_RESOURCE_BARRIER::Transition(tex->Resource.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	mCommandList->ResourceBarrier(1, &b);
	UpdateSubresources(mCommandList.Get(),
		tex->Resource.Get(), tex->UploadHeap.Get(), 0, 0, 1, &sd);
	b = CD3DX12_RESOURCE_BARRIER::Transition(tex->Resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &b);

	CD3DX12_CPU_DESCRIPTOR_HANDLE h(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	h.Offset(1, mCbvSrvDescriptorSize);
	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sv.Texture2D.MipLevels = 1;
	md3dDevice->CreateShaderResourceView(tex->Resource.Get(), &sv, h);
	mTextures[tex->Name] = std::move(tex);
}

void CrateApp::LoadTextures()
{
	auto wt = std::make_unique<Texture>();
	wt->Name = "woodCrateTex";
	wt->Filename = L"textures/WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), wt->Filename.c_str(), wt->Resource, wt->UploadHeap));
	mTextures[wt->Name] = std::move(wt);
}

void CrateApp::LoadTextureFromFile(const std::string& path,
	const std::string& texName, int heapIndex,
	bool isSRGB)
{
	OutputDebugStringA(("LoadTexture: [" + texName + "] path=[" + path + "] slot=" +
		std::to_string(heapIndex) + "\n").c_str());
	std::wstring wpath(path.begin(), path.end());
	std::vector<uint8_t> tgaData;
	int width, height;
	DXGI_FORMAT fmt;
	bool loaded = LoadTGAFile(wpath, tgaData, width, height, fmt);

	CD3DX12_CPU_DESCRIPTOR_HANDLE h(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	h.Offset(heapIndex, mCbvSrvDescriptorSize);

	Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap;

	if (loaded)
	{
		int bpp = (int)tgaData.size() / (width * height);
		std::vector<uint8_t> rgba;
		const uint8_t* src = tgaData.data();
		if (bpp == 3)
		{
			rgba.resize(width * height * 4);
			for (int i = 0; i < width * height; ++i)
			{
				rgba[i * 4 + 0] = tgaData[i * 3 + 0];
				rgba[i * 4 + 1] = tgaData[i * 3 + 1];
				rgba[i * 4 + 2] = tgaData[i * 3 + 2];
				rgba[i * 4 + 3] = 255;
			}
			src = rgba.data();
		}

		const DXGI_FORMAT texFormat = isSRGB
			? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
			: DXGI_FORMAT_R8G8B8A8_UNORM;

		D3D12_RESOURCE_DESC td = {};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = width; td.Height = height;
		td.DepthOrArraySize = 1; td.MipLevels = 1;
		td.Format = texFormat;
		td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
		if (SUCCEEDED(md3dDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texResource))))
		{
			UINT64 upSize = GetRequiredIntermediateSize(texResource.Get(), 0, 1);
			CD3DX12_HEAP_PROPERTIES upHp(D3D12_HEAP_TYPE_UPLOAD);
			auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(upSize);
			if (SUCCEEDED(md3dDevice->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadHeap))))
			{
				D3D12_SUBRESOURCE_DATA sd = {};
				sd.pData = src;
				sd.RowPitch = width * 4;
				sd.SlicePitch = sd.RowPitch * height;

				auto b = CD3DX12_RESOURCE_BARRIER::Transition(texResource.Get(),
					D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
				mCommandList->ResourceBarrier(1, &b);
				UpdateSubresources(mCommandList.Get(),
					texResource.Get(), uploadHeap.Get(), 0, 0, 1, &sd);
				b = CD3DX12_RESOURCE_BARRIER::Transition(texResource.Get(),
					D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				mCommandList->ResourceBarrier(1, &b);

				D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
				sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				sv.Format = texFormat;
				sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				sv.Texture2D.MipLevels = 1;
				md3dDevice->CreateShaderResourceView(texResource.Get(), &sv, h);

				auto tex = std::make_unique<Texture>();
				tex->Name = texName;
				tex->Resource = texResource;
				tex->UploadHeap = uploadHeap;
				mTextures[texName] = std::move(tex);
				return;
			}
		}
	}

	uint8_t pixel[4] = { 255, 255, 255, 255 };

	D3D12_RESOURCE_DESC td = {};
	td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = 1; td.Height = 1;
	td.DepthOrArraySize = 1; td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texResource)));

	UINT64 upSize = GetRequiredIntermediateSize(texResource.Get(), 0, 1);
	CD3DX12_HEAP_PROPERTIES upHp(D3D12_HEAP_TYPE_UPLOAD);
	auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(upSize);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadHeap)));

	D3D12_SUBRESOURCE_DATA sd = {};
	sd.pData = pixel;
	sd.RowPitch = 4;
	sd.SlicePitch = 4;

	auto b = CD3DX12_RESOURCE_BARRIER::Transition(texResource.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	mCommandList->ResourceBarrier(1, &b);
	UpdateSubresources(mCommandList.Get(), texResource.Get(), uploadHeap.Get(), 0, 0, 1, &sd);
	b = CD3DX12_RESOURCE_BARRIER::Transition(texResource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &b);

	D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sv.Texture2D.MipLevels = 1;
	md3dDevice->CreateShaderResourceView(texResource.Get(), &sv, h);

	auto tex = std::make_unique<Texture>();
	tex->Name = texName + "_fallback";
	tex->Resource = texResource;
	tex->UploadHeap = uploadHeap;
	mTextures[tex->Name] = std::move(tex);
}

void CrateApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
		mFrameResources.push_back(std::make_unique<FrameResource>(
			md3dDevice.Get(), 1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
}

void CrateApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	UINT objCBBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	for (const auto* ri : ritems)
	{
		static bool printed = false;
		if (!printed) {
			OutputDebugStringA(("Mat: " + ri->Mat->Name +
				" diffuse=" + std::to_string(ri->Mat->DiffuseSrvHeapIndex) +
				" rough=" + std::to_string(ri->Mat->RoughnessSrvHeapIndex) +
				" metal=" + std::to_string(ri->Mat->MetallicSrvHeapIndex) + "\n").c_str());
			printed = true;
		}
		auto vbv = ri->Geo->VertexBufferView();
		auto ibv = ri->Geo->IndexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(&ibv);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objAddr =
			objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBBytes;
		D3D12_GPU_VIRTUAL_ADDRESS matAddr =
			matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBBytes;

		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseTex(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		diffuseTex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		CD3DX12_GPU_DESCRIPTOR_HANDLE normalTex(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		normalTex.Offset(ri->Mat->NormalSrvHeapIndex, mCbvSrvDescriptorSize);

		CD3DX12_GPU_DESCRIPTOR_HANDLE dispTex(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		dispTex.Offset(ri->Mat->DispSrvHeapIndex, mCbvSrvDescriptorSize);

		CD3DX12_GPU_DESCRIPTOR_HANDLE roughnessTex(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		roughnessTex.Offset(ri->Mat->RoughnessSrvHeapIndex, mCbvSrvDescriptorSize);

		CD3DX12_GPU_DESCRIPTOR_HANDLE metallicTex(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		metallicTex.Offset(ri->Mat->MetallicSrvHeapIndex, mCbvSrvDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(kRootDiffuse, diffuseTex);
		cmdList->SetGraphicsRootConstantBufferView(kRootObjCB, objAddr);
		cmdList->SetGraphicsRootConstantBufferView(kRootMatCB, matAddr);
		cmdList->SetGraphicsRootDescriptorTable(kRootNormal, normalTex);
		cmdList->SetGraphicsRootDescriptorTable(kRootDisplace, dispTex);
		cmdList->SetGraphicsRootDescriptorTable(kRootRoughness, roughnessTex);
		cmdList->SetGraphicsRootDescriptorTable(kRootMetallic, metallicTex);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1,
			ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CrateApp::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	const CD3DX12_STATIC_SAMPLER_DESC anisoWrap(4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f, 8);
	const CD3DX12_STATIC_SAMPLER_DESC anisoClamp(5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f, 8);
	return { pointWrap, pointClamp, linearWrap, linearClamp, anisoWrap, anisoClamp };
}

static const wchar_t* kIrradianceMapPath = L"ibl/IrradianceMap_BC6U.dds";
static const wchar_t* kPrefilterMapPath = L"ibl/PreFilteredEnvMap_BC6U.dds";
static const wchar_t* kBrdfLutPath = L"ibl/IntegrationMap.dds";

void CrateApp::LoadIBLTextures()
{
	{
		auto tex = std::make_unique<Texture>();
		tex->Name = "iblIrradiance";
		tex->Filename = kIrradianceMapPath;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
			md3dDevice.Get(), mCommandList.Get(),
			tex->Filename.c_str(),
			tex->Resource, tex->UploadHeap));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = tex->Resource->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = tex->Resource->GetDesc().MipLevels;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(
			mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			kIrradianceSrvSlot, mCbvSrvDescriptorSize);
		md3dDevice->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hCpu);

		mTextures[tex->Name] = std::move(tex);
	}

	{
		auto tex = std::make_unique<Texture>();
		tex->Name = "iblPrefilter";
		tex->Filename = kPrefilterMapPath;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
			md3dDevice.Get(), mCommandList.Get(),
			tex->Filename.c_str(),
			tex->Resource, tex->UploadHeap));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = tex->Resource->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = tex->Resource->GetDesc().MipLevels;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(
			mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			kPrefilterSrvSlot, mCbvSrvDescriptorSize);
		md3dDevice->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hCpu);

		mTextures[tex->Name] = std::move(tex);
	}

	{
		auto tex = std::make_unique<Texture>();
		tex->Name = "iblBrdfLut";
		tex->Filename = kBrdfLutPath;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
			md3dDevice.Get(), mCommandList.Get(),
			tex->Filename.c_str(),
			tex->Resource, tex->UploadHeap));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = tex->Resource->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(
			mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			kBrdfLutSrvSlot, mCbvSrvDescriptorSize);
		md3dDevice->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hCpu);

		mTextures[tex->Name] = std::move(tex);
	}
}