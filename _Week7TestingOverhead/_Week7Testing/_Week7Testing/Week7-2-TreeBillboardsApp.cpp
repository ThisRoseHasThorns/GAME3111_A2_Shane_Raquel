//***************************************************************************************
// TreeBillboardsApp.cpp 
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};

class TreeBillboardsApp : public D3DApp
{
public:
    TreeBillboardsApp(HINSTANCE hInstance);
    TreeBillboardsApp(const TreeBillboardsApp& rhs) = delete;
    TreeBillboardsApp& operator=(const TreeBillboardsApp& rhs) = delete;
    ~TreeBillboardsApp();

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
	void UpdateWaves(const GameTimer& gt); 

	// Castle rendering functions
	void BuildCastleGeometry();
	void BuildCastleCorners();
	void BuildCastleWalls();
	void BuildCone();
	void BuildPyramid();
	void BuildDiamond();

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayouts();
    void BuildLandGeometry();
    void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildTreeSpritesGeometry();
	void BuildMazePart(float sX, float sZ, float pX, float pZ, int index);
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    float GetHillsHeight(float x, float z)const;
    XMFLOAT3 GetHillsNormal(float x, float z)const;

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
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mStdInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

    RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

	Camera mCamera;

    POINT mLastMousePos;

	bool bNoClip = false;
	XMFLOAT3 prevCamPos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        TreeBillboardsApp theApp(hInstance);
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

TreeBillboardsApp::TreeBillboardsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

TreeBillboardsApp::~TreeBillboardsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TreeBillboardsApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
 
	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayouts();
	BuildCastleGeometry();
    BuildLandGeometry();
    BuildWavesGeometry();
	BuildBoxGeometry();
	BuildTreeSpritesGeometry();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

	// Init camera
	mCamera.SetPosition(0.0f, 15.0f, -80.0f);
	mCamera.UpdateViewMatrix();
	prevCamPos = mCamera.GetPosition3f();

	// Set background color
	mMainPassCB.FogColor = { 0.0f, 1.0f, 1.0f, 0.5f };

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void TreeBillboardsApp::OnResize()
{
    D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void TreeBillboardsApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
    UpdateWaves(gt);
}

void TreeBillboardsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TreeBillboardsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void TreeBillboardsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TreeBillboardsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		//step4: Instead of updating the angles based on input to orbit camera around scene, 
		//we rotate the camera’s look direction:
		//mTheta += dx;
		//mPhi += dy;

		if (bNoClip)
			mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}
 
void TreeBillboardsApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	float camSpeed = 50.0f;

	prevCamPos = mCamera.GetPosition3f();

	//GetAsyncKeyState returns a short (2 bytes)
	if (GetAsyncKeyState('W') & 0x8000) //most significant bit (MSB) is 1 when key is pressed (1000 000 000 000)
		mCamera.Walk(camSpeed * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-camSpeed * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-camSpeed * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(camSpeed * dt);

	if (GetAsyncKeyState('1') & 0x8000)
		bNoClip = true;

	if (GetAsyncKeyState('2') & 0x8000)
		bNoClip = false;

	mCamera.UpdateViewMatrix();
	
}
 
void TreeBillboardsApp::UpdateCamera(const GameTimer& gt)
{
	/*if (!bNoClip)
	{
		for (size_t i = 0; i < mAllRitems.size(); ++i)
		{
			RenderItem ri = *mAllRitems[i];
			float xPos = ri.World(0, 3);
			float yPos = ri.World(1, 3);
			float zPos = ri.World(2, 3);
			float xMin = xPos - ri.World(0, 0);
			float xMax = xPos + ri.World(0, 0);
			float yMin = yPos - ri.World(1, 1);
			float yMax = yPos + ri.World(1, 1);
			float zMin = zPos - ri.World(2, 2);
			float zMax = zPos + ri.World(2, 2);

			if ((mCamera.GetPosition3f().x >= xMin && mCamera.GetPosition3f().x <= xMax) &&
				(mCamera.GetPosition3f().y >= yMin && mCamera.GetPosition3f().y <= yMax) &&
				(mCamera.GetPosition3f().z >= zMin && mCamera.GetPosition3f().z <= zMax))
			{
				mCamera.SetPosition(prevCamPos);
				mCamera.UpdateViewMatrix();
			}


		}

	}*/
}

void TreeBillboardsApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if(tu >= 1.0f)
		tu -= 1.0f;

	if(tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void TreeBillboardsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void TreeBillboardsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
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

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TreeBillboardsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 1.25f, 0.5f, 0.35f, 1.0f };
	
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 2.57735f };
	mMainPassCB.Lights[0].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { -0.707f, -0.707f, -5.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	// Point Lights
	mMainPassCB.Lights[3].Position = { 0.0f, 30.0f, 10.0f };
	mMainPassCB.Lights[3].Strength = { 0.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[3].FalloffStart = 5.0f;
	mMainPassCB.Lights[3].FalloffEnd = 50.0f;
	
	mMainPassCB.Lights[4].Position = { -30.0f, 100.0f, -35.0f };
	mMainPassCB.Lights[4].Strength = { 0.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[4].FalloffStart = 5.0f;
	mMainPassCB.Lights[4].FalloffEnd = 50.0f;
	
	mMainPassCB.Lights[5].Position = { 30.0f, 100.0f, -35.0f };
	mMainPassCB.Lights[5].Strength = { 0.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[5].FalloffStart = 5.0f;
	mMainPassCB.Lights[5].FalloffEnd = 50.0f;
	
	mMainPassCB.Lights[6].Position = { -30.0f, 100.0f, 35.0f };
	mMainPassCB.Lights[6].Strength = { 0.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[6].FalloffStart = 5.0f;
	mMainPassCB.Lights[6].FalloffEnd = 50.0f;
	
	mMainPassCB.Lights[7].Position = { 30.0f, 100.0f, 35.0f };
	mMainPassCB.Lights[7].Strength = { 0.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[7].FalloffStart = 5.0f;
	mMainPassCB.Lights[7].FalloffEnd = 50.0f;
	
	mMainPassCB.Lights[8].Position = { -10.0f, 40.0f, -42.0f };
	mMainPassCB.Lights[8].Strength = { 0.0f, 0.0f, 1.0f };
	mMainPassCB.Lights[8].FalloffStart = 5.0f;
	mMainPassCB.Lights[8].FalloffEnd = 50.0f;
	
	mMainPassCB.Lights[9].Position = { 0.0f, 15.0f, 100.0f };
	mMainPassCB.Lights[9].Strength = { 0.0f, 0.0f, 1.0f };
	mMainPassCB.Lights[9].FalloffStart = 5.0f;
	mMainPassCB.Lights[9].FalloffEnd = 50.0f;

	mMainPassCB.Lights[10].Position = { -20.0f, 40.0f, -10.0f };
	mMainPassCB.Lights[10].Strength = { 0.0f, 0.0f, 1.0f };
	mMainPassCB.Lights[10].FalloffStart = 5.0f;
	mMainPassCB.Lights[10].FalloffEnd = 50.0f;
	
	mMainPassCB.Lights[11].Position = { 20.0f, 40.0f, -10.0f };
	mMainPassCB.Lights[11].Strength = { 0.0f, 0.0f, 1.0f };
	mMainPassCB.Lights[11].FalloffStart = 5.0f;
	mMainPassCB.Lights[11].FalloffEnd = 50.0f;
	

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TreeBillboardsApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for(int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);
		
		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void TreeBillboardsApp::BuildCastleGeometry()
{
	BuildCastleWalls();
	BuildCastleCorners();
	BuildCone();
	BuildPyramid();
	BuildDiamond();
}

void TreeBillboardsApp::BuildCastleCorners()
{
	// Create a singular wall geometry, which will then be copied to make multiple walls
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData corner = geoGen.CreateCylinder(1.0f, 1.0f, 1.0f, 15, 10);

	std::vector<Vertex> vertices(corner.Vertices.size());
	for (size_t i = 0; i < corner.Vertices.size(); ++i)
	{
		auto& p = corner.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = corner.Vertices[i].Normal;
		vertices[i].TexC = corner.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = corner.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "wallGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["corner"] = submesh;

	mGeometries["cornerGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildCastleWalls()
{
	// Create a singular wall geometry, which will then be copied to make multiple walls
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData wall = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);

	std::vector<Vertex> vertices(wall.Vertices.size());
	for (size_t i = 0; i < wall.Vertices.size(); ++i)
	{
		auto& p = wall.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = wall.Vertices[i].Normal;
		vertices[i].TexC = wall.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = wall.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "wallGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["wall"] = submesh;

	mGeometries["wallGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildCone()
{
	// Cones used to top the castle towers
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, false, 15, 10);

	std::vector<Vertex> vertices(cone.Vertices.size());
	for (size_t i = 0; i < cone.Vertices.size(); ++i)
	{
		auto& p = cone.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = cone.Vertices[i].Normal;
		vertices[i].TexC = cone.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = cone.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "coneGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["cone"] = submesh;

	mGeometries["coneGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildPyramid()
{
	// Pyramid of power
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f);

	std::vector<Vertex> vertices(pyramid.Vertices.size());
	for (size_t i = 0; i < pyramid.Vertices.size(); ++i)
	{
		auto& p = pyramid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = pyramid.Vertices[i].Normal;
		vertices[i].TexC = pyramid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = pyramid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "pyramidGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["pyramid"] = submesh;

	mGeometries["pyramidGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildDiamond()
{
	// Diamonds of Doom
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f, 1.0f);

	std::vector<Vertex> vertices(diamond.Vertices.size());
	for (size_t i = 0; i < diamond.Vertices.size(); ++i)
	{
		auto& p = diamond.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = diamond.Vertices[i].Normal;
		vertices[i].TexC = diamond.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = diamond.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "diamondGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["diamond"] = submesh;

	mGeometries["diamondGeo"] = std::move(geo);
}

void TreeBillboardsApp::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../../Textures/greengrass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../../Textures/water.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto brickTex = std::make_unique<Texture>();
	brickTex->Name = "brickTex";
	brickTex->Filename = L"../../Textures/brick.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), brickTex->Filename.c_str(),
		brickTex->Resource, brickTex->UploadHeap));

	auto marbleTex = std::make_unique<Texture>();
	marbleTex->Name = "marbleTex";
	marbleTex->Filename = L"../../Textures/marble.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), marbleTex->Filename.c_str(),
		marbleTex->Resource, marbleTex->UploadHeap));

	auto woodTex = std::make_unique<Texture>();
	woodTex->Name = "woodTex";
	woodTex->Filename = L"../../Textures/wood.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodTex->Filename.c_str(),
		woodTex->Resource, woodTex->UploadHeap));

	auto crystalTex = std::make_unique<Texture>();
	crystalTex->Name = "crystalTex";
	crystalTex->Filename = L"../../Textures/crystal.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), crystalTex->Filename.c_str(),
		crystalTex->Resource, crystalTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"../../Textures/treeArray.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));

	

	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[brickTex->Name] = std::move(brickTex);
	mTextures[marbleTex->Name] = std::move(marbleTex);
	mTextures[woodTex->Name] = std::move(woodTex);
	mTextures[crystalTex->Name] = std::move(crystalTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
	
}

void TreeBillboardsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
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

void TreeBillboardsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 8;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto brickTex = mTextures["brickTex"]->Resource;
	auto marbleTex = mTextures["marbleTex"]->Resource;
	auto woodTex = mTextures["woodTex"]->Resource;
	auto crystalTex = mTextures["crystalTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
	

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = brickTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(brickTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = marbleTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(marbleTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = woodTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(woodTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = crystalTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(crystalTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);

	
}

void TreeBillboardsApp::BuildShadersAndInputLayouts()
{
	const D3D_SHADER_MACRO defines[] =
	{
		//"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		//"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mStdInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void TreeBillboardsApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(720.0f, 720.0f, 225, 225);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //

    std::vector<Vertex> vertices(grid.Vertices.size());
    for(size_t i = 0; i < grid.Vertices.size(); ++i)
    {
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		if (abs(p.x) > 175 && abs(p.x) < 999 && abs(p.z) < 999 || abs(p.z) > 200 && abs(p.z) < 999 && abs(p.x) < 999)
		{
			vertices[i].Pos.y = -10.0f;
		}
		else
		{
			vertices[i].Pos.y = 6.0f; //GetHillsHeight(p.x, p.z);
		}

		vertices[i].Normal = XMFLOAT3(p.x, 1.0f, p.y); //GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
    }

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildWavesGeometry()
{
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for(int i = 0; i < m - 1; ++i)
    {
        for(int j = 0; j < n - 1; ++j)
        {
            indices[k] = i*n + j;
            indices[k + 1] = i*n + j + 1;
            indices[k + 2] = (i + 1)*n + j;

            indices[k + 3] = (i + 1)*n + j;
            indices[k + 4] = i*n + j + 1;
            indices[k + 5] = (i + 1)*n + j + 1;

            k += 6; // next quad
        }
    }

	UINT vbByteSize = mWaves->VertexCount()*sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size()*sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["box"] = submesh;

	mGeometries["boxGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildTreeSpritesGeometry()
{
	//step5
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 64;
	std::array<TreeSpriteVertex, 64> vertices;
	for(UINT i = 0; i < treeCount; ++i)
	{
		if (i < treeCount / 4)
		{
			float x = MathHelper::RandF(-150.0f, -120.0f);
			float z = MathHelper::RandF(-180.0f, 180.0f);
			float y = 24.0f; //GetHillsHeight(x, z);

			vertices[i].Pos = XMFLOAT3(x, y, z);
			vertices[i].Size = XMFLOAT2(40.0f, 40.0f);
		}
		else if (i < treeCount / 2)
		{
			float x = MathHelper::RandF(120.0f, 150.0f);
			float z = MathHelper::RandF(-180.0f, 180.0f);
			float y = 24.0f; //GetHillsHeight(x, z);

			vertices[i].Pos = XMFLOAT3(x, y, z);
			vertices[i].Size = XMFLOAT2(40.0f, 40.0f);
		}
		else if (i < (3 * treeCount) / 4)
		{
			if (i % 2 == 0)
			{
				float x = MathHelper::RandF(-130.0f, -10.0f);
				float z = MathHelper::RandF(-180.0f, -160.0f);
				float y = 24.0f; //GetHillsHeight(x, z);

				vertices[i].Pos = XMFLOAT3(x, y, z);
				vertices[i].Size = XMFLOAT2(40.0f, 40.0f);
			}
			else
			{
				float x = MathHelper::RandF(10.0f, 130.0f);
				float z = MathHelper::RandF(-180.0f, -160.0f);
				float y = 24.0f; //GetHillsHeight(x, z);

				vertices[i].Pos = XMFLOAT3(x, y, z);
				vertices[i].Size = XMFLOAT2(40.0f, 40.0f);
			}
		}
		else
		{
			float x = MathHelper::RandF(-100.0f, 100.0f);
			float z = MathHelper::RandF(160.0f, 180.0f);
			float y = 24.0f; //GetHillsHeight(x, z);

			vertices[i].Pos = XMFLOAT3(x, y, z);
			vertices[i].Size = XMFLOAT2(40.0f, 40.0f);
		}
	}

	std::array<std::uint16_t, 64> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39,
		40, 41, 42, 43, 44, 45, 46, 47, 
		48, 49, 50, 51, 52, 53, 54, 55, 
		56, 57, 58, 59, 60, 61, 62, 63
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildMazePart(float sX, float sZ, float pX, float pZ, int index)
{
	// Build the individual wall of the maze
	auto tempRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&tempRitem->TexTransform, (XMMatrixScaling(6.0f, 4.0f, 4.0f)));
	XMStoreFloat4x4(&tempRitem->World, (XMMatrixScaling(sX, 30.0f, sZ) * XMMatrixTranslation(pX, 25.0f, pZ)));
	tempRitem->ObjCBIndex = index++;
	tempRitem->Mat = mMaterials["grass"].get();
	tempRitem->Geo = mGeometries["wallGeo"].get();
	tempRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	tempRitem->IndexCount = tempRitem->Geo->DrawArgs["wall"].IndexCount;
	tempRitem->StartIndexLocation = tempRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	tempRitem->BaseVertexLocation = tempRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(tempRitem.get());	mAllRitems.push_back(std::move(tempRitem));
}

void TreeBillboardsApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };
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

	//there is abug with F2 key that is supposed to turn on the multisampling!
//Set4xMsaaState(true);
	//m4xMsaaState = true;

	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for transparent objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	//transparentPsoDesc.BlendState.AlphaToCoverageEnable = true;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	//
	// PSO for alpha tested objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	//step1
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void TreeBillboardsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void TreeBillboardsApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto brick = std::make_unique<Material>();
	brick->Name = "brick";
	brick->MatCBIndex = 2;
	brick->DiffuseSrvHeapIndex = 2;
	brick->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	brick->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	brick->Roughness = 0.25f;

	auto marble = std::make_unique<Material>();
	marble->Name = "marble";
	marble->MatCBIndex = 3;
	marble->DiffuseSrvHeapIndex = 3;
	marble->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	marble->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	marble->Roughness = 0.25f;

	auto wood = std::make_unique<Material>();
	wood->Name = "wood";
	wood->MatCBIndex = 4;
	wood->DiffuseSrvHeapIndex = 4;
	wood->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wood->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	wood->Roughness = 0.25f;

	auto crystal = std::make_unique<Material>();
	crystal->Name = "crystal";
	crystal->MatCBIndex = 5;
	crystal->DiffuseSrvHeapIndex = 5;
	crystal->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	crystal->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	crystal->Roughness = 0.25f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 6;
	treeSprites->DiffuseSrvHeapIndex = 6;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;

	

	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["brick"] = std::move(brick);
	mMaterials["marble"] = std::move(marble);
	mMaterials["wood"] = std::move(wood);
	mMaterials["crystal"] = std::move(crystal);
	mMaterials["treeSprites"] = std::move(treeSprites);
	
}

void TreeBillboardsApp::BuildRenderItems()
{
	// Create a single ObjCBIndex int to count each object being drawn
	int funcCBIndex = 0;

	auto wavesRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wavesRitem->World, XMMatrixScaling(6.0f, 1.0f, 6.0f));
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(30.0f, 30.0f, 1.0f));
	wavesRitem->ObjCBIndex = funcCBIndex++;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	gridRitem->ObjCBIndex = funcCBIndex++;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	auto boxRitem = std::make_unique<RenderItem>();
	
	XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(3.0f, 30.0f, -9.0f));
	//boxRitem->ObjCBIndex = funcCBIndex++;
	boxRitem->Mat = mMaterials["brick"].get();
	boxRitem->Geo = mGeometries["boxGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	//mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem.get());

	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = funcCBIndex++;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;



	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());

	mAllRitems.push_back(std::move(wavesRitem));
	mAllRitems.push_back(std::move(gridRitem));
	//mAllRitems.push_back(std::move(boxRitem));
	mAllRitems.push_back(std::move(treeSpritesRitem));

	// Build the base of the castle
	auto baseRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&baseRitem->TexTransform, (XMMatrixScaling(11.0f, 11.0f, 11.0f)));
	XMStoreFloat4x4(&baseRitem->World, (XMMatrixScaling(220.0f, 8.0f, 280.0f) * XMMatrixTranslation(0.0f, 6.0f, 0.0f)));
	baseRitem->ObjCBIndex = funcCBIndex++;
	baseRitem->Mat = mMaterials["brick"].get();
	baseRitem->Geo = mGeometries["wallGeo"].get();
	baseRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	baseRitem->IndexCount = baseRitem->Geo->DrawArgs["wall"].IndexCount;
	baseRitem->StartIndexLocation = baseRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	baseRitem->BaseVertexLocation = baseRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(baseRitem.get());	mAllRitems.push_back(std::move(baseRitem));

	// Build the drawbridge
	auto bridgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&bridgeRitem->World, (XMMatrixScaling(30.0f, 6.0f, 45.0f) * XMMatrixTranslation(0.0f, 9.0f, -137.5f)));
	bridgeRitem->ObjCBIndex = funcCBIndex++;
	bridgeRitem->Mat = mMaterials["wood"].get();
	bridgeRitem->Geo = mGeometries["wallGeo"].get();
	bridgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	bridgeRitem->IndexCount = bridgeRitem->Geo->DrawArgs["wall"].IndexCount;
	bridgeRitem->StartIndexLocation = bridgeRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	bridgeRitem->BaseVertexLocation = bridgeRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(bridgeRitem.get());	mAllRitems.push_back(std::move(bridgeRitem));

	// Build the pyramid inside the castle
	auto pyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem->World, (XMMatrixScaling(20.0f, 12.0f, 20.0f) * XMMatrixTranslation(0.0f, 16.0f, 100.0f)));
	pyramidRitem->ObjCBIndex = funcCBIndex++;
	pyramidRitem->Mat = mMaterials["crystal"].get();
	pyramidRitem->Geo = mGeometries["pyramidGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(pyramidRitem.get());	mAllRitems.push_back(std::move(pyramidRitem));

	// This loop draws 3 of the 4 main castle walls, specifically the ones without the gate
	for (int i = 0; i < 3; i++)
	{
		auto wallRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallRitem->TexTransform, (XMMatrixScaling(10.0f, 2.0f, 1.0f)));
		XMStoreFloat4x4(&wallRitem->World, (XMMatrixScaling(12.0f + (168.0f * (i % 2)), 40.0f, 12.0f + (228.0f * (1.0f - (i % 2)))) * XMMatrixTranslation(-90.0f + (i * 90.0f), 30.0f, 120.0f - 120.0f * (1 - (i % 2)))));
		//XMStoreFloat4x4(&wallRitem->TexTransform, (XMMatrixScaling(x, y, z)));
		wallRitem->ObjCBIndex = funcCBIndex++;
		wallRitem->Mat = mMaterials["brick"].get();
		wallRitem->Geo = mGeometries["wallGeo"].get();
		wallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallRitem->IndexCount = wallRitem->Geo->DrawArgs["wall"].IndexCount;
		wallRitem->StartIndexLocation = wallRitem->Geo->DrawArgs["wall"].StartIndexLocation;
		wallRitem->BaseVertexLocation = wallRitem->Geo->DrawArgs["wall"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(wallRitem.get());
		mAllRitems.push_back(std::move(wallRitem));
	}

	// This loop draws the walls surrounding the castle's gate
	for (int i = 0; i < 3; i++)
	{
		auto gateRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&gateRitem->TexTransform, (XMMatrixScaling(6.0f, 2.0f, 1.0f)));
		XMStoreFloat4x4(&gateRitem->World, (XMMatrixScaling(80.0f - (50.0f * (i % 2)), 40.0f - 25.0f * (1.0f * (i % 2)), 10.0f) * XMMatrixTranslation(-55.0f + (55.0f * i), 30.0f + 12.5f * (i % 2), -120.0f)));
		gateRitem->ObjCBIndex = funcCBIndex++;
		gateRitem->Mat = mMaterials["brick"].get();
		gateRitem->Geo = mGeometries["wallGeo"].get();
		gateRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		gateRitem->IndexCount = gateRitem->Geo->DrawArgs["wall"].IndexCount;
		gateRitem->StartIndexLocation = gateRitem->Geo->DrawArgs["wall"].StartIndexLocation;
		gateRitem->BaseVertexLocation = gateRitem->Geo->DrawArgs["wall"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(gateRitem.get());
		mAllRitems.push_back(std::move(gateRitem));
	}

	// Build castle merlons
	for (int i = 0; i < 5; i++)
	{
		// Left side
		auto leftRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&leftRitem->TexTransform, (XMMatrixScaling(0.5f, 0.25f, 1.0f)));
		XMStoreFloat4x4(&leftRitem->World, (XMMatrixScaling(12.0f, 5.0f, 15.0f) * XMMatrixTranslation(-90.0f, 52.5f, 80.0f - 40.0f * i)));
		leftRitem->ObjCBIndex = funcCBIndex++;
		leftRitem->Mat = mMaterials["brick"].get();
		leftRitem->Geo = mGeometries["wallGeo"].get();
		leftRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftRitem->IndexCount = leftRitem->Geo->DrawArgs["wall"].IndexCount;
		leftRitem->StartIndexLocation = leftRitem->Geo->DrawArgs["wall"].StartIndexLocation;
		leftRitem->BaseVertexLocation = leftRitem->Geo->DrawArgs["wall"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(leftRitem.get());
		mAllRitems.push_back(std::move(leftRitem));

		// Right side
		auto rightRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&rightRitem->TexTransform, (XMMatrixScaling(0.5f, 0.25f, 1.0f)));
		XMStoreFloat4x4(&rightRitem->World, (XMMatrixScaling(12.0f, 5.0f, 15.0f) * XMMatrixTranslation(90.0f, 52.5f, 80.0f - 40.0f * i)));
		rightRitem->ObjCBIndex = funcCBIndex++;
		rightRitem->Mat = mMaterials["brick"].get();
		rightRitem->Geo = mGeometries["wallGeo"].get();
		rightRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightRitem->IndexCount = rightRitem->Geo->DrawArgs["wall"].IndexCount;
		rightRitem->StartIndexLocation = rightRitem->Geo->DrawArgs["wall"].StartIndexLocation;
		rightRitem->BaseVertexLocation = rightRitem->Geo->DrawArgs["wall"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(rightRitem.get());
		mAllRitems.push_back(std::move(rightRitem));

		// Front
		auto frontRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&frontRitem->TexTransform, (XMMatrixScaling(0.5f, 0.25f, 1.0f)));
		XMStoreFloat4x4(&frontRitem->World, (XMMatrixScaling(15.0f, 5.0f, 10.0f) * XMMatrixTranslation(-60.0f + 30.0f * i, 52.5f, -120.0f)));
		frontRitem->ObjCBIndex = funcCBIndex++;
		frontRitem->Mat = mMaterials["brick"].get();
		frontRitem->Geo = mGeometries["wallGeo"].get();
		frontRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		frontRitem->IndexCount = frontRitem->Geo->DrawArgs["wall"].IndexCount;
		frontRitem->StartIndexLocation = frontRitem->Geo->DrawArgs["wall"].StartIndexLocation;
		frontRitem->BaseVertexLocation = frontRitem->Geo->DrawArgs["wall"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(frontRitem.get());
		mAllRitems.push_back(std::move(frontRitem));

		// Back
		auto backRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&backRitem->TexTransform, (XMMatrixScaling(0.5f, 0.25f, 1.0f)));
		XMStoreFloat4x4(&backRitem->World, (XMMatrixScaling(15.0f, 5.0f, 12.0f) * XMMatrixTranslation(-60.0f + 30.0f * i, 52.5f, 120.0f)));
		backRitem->ObjCBIndex = funcCBIndex++;
		backRitem->Mat = mMaterials["brick"].get();
		backRitem->Geo = mGeometries["wallGeo"].get();
		backRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		backRitem->IndexCount = backRitem->Geo->DrawArgs["wall"].IndexCount;
		backRitem->StartIndexLocation = backRitem->Geo->DrawArgs["wall"].StartIndexLocation;
		backRitem->BaseVertexLocation = backRitem->Geo->DrawArgs["wall"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(backRitem.get());
		mAllRitems.push_back(std::move(backRitem));

	}

	// Build the corners of the castle walls
	for (int i = 0; i < 4; i++)
	{
		auto cornerRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&cornerRitem->World, (XMMatrixScaling(10.0f, 70.0f, 10.0f) * XMMatrixTranslation(-90.0f + 180.0f * (i % 2), 30.0f, 120.0f - 240.0f * (i / 2))));
		cornerRitem->ObjCBIndex = funcCBIndex++;
		cornerRitem->Mat = mMaterials["marble"].get();
		cornerRitem->Geo = mGeometries["cornerGeo"].get();
		cornerRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		cornerRitem->IndexCount = cornerRitem->Geo->DrawArgs["corner"].IndexCount;
		cornerRitem->StartIndexLocation = cornerRitem->Geo->DrawArgs["corner"].StartIndexLocation;
		cornerRitem->BaseVertexLocation = cornerRitem->Geo->DrawArgs["corner"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(cornerRitem.get());
		mAllRitems.push_back(std::move(cornerRitem));
	}

	// Build the tips of the wall's towers
	for (int i = 0; i < 4; i++)
	{
		auto tipRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&tipRitem->World, (XMMatrixScaling(9.0f, 20.0f, 9.0f) * XMMatrixTranslation(-90.0f + 180.0f * (i % 2), 75.0f, 120.0f - 240.0f * (i / 2))));
		tipRitem->ObjCBIndex = funcCBIndex++;
		tipRitem->Mat = mMaterials["marble"].get();
		tipRitem->Geo = mGeometries["coneGeo"].get();
		tipRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		tipRitem->IndexCount = tipRitem->Geo->DrawArgs["cone"].IndexCount;
		tipRitem->StartIndexLocation = tipRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		tipRitem->BaseVertexLocation = tipRitem->Geo->DrawArgs["cone"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(tipRitem.get());
		mAllRitems.push_back(std::move(tipRitem));
	}

	// Build the diamonds above the tips of the wall's towers
	for (int i = 0; i < 4; i++)
	{
		auto diamondRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&diamondRitem->World, (XMMatrixScaling(5.0f, 8.0f, 5.0f) * XMMatrixTranslation(-90.0f + 180.0f * (i % 2), 100.0f, 120.0f - 240.0f * (i / 2))));
		diamondRitem->ObjCBIndex = funcCBIndex++;
		diamondRitem->Mat = mMaterials["crystal"].get();
		diamondRitem->Geo = mGeometries["diamondGeo"].get();
		diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
		diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
		diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(diamondRitem.get());
		mAllRitems.push_back(std::move(diamondRitem));
	}

	// Build the diamond above the pyramid
	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, (XMMatrixScaling(7.5f, 12.0f, 7.5f) * XMMatrixTranslation(0.0f, 30.0f, 100.0f)));
	diamondRitem->ObjCBIndex = funcCBIndex++;
	diamondRitem->Mat = mMaterials["crystal"].get();
	diamondRitem->Geo = mGeometries["diamondGeo"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(diamondRitem.get());
	mAllRitems.push_back(std::move(diamondRitem));


	// Build the maze, wall by wall
	// Outer walls
	BuildMazePart(54.0f, 1.5f, 40.0f, -90.0f, funcCBIndex++);
	BuildMazePart(54.0f, 1.5f, -40.0f, -90.0f, funcCBIndex++);
	BuildMazePart(54.0f, 1.5f, 40.0f, 90.0f, funcCBIndex++);
	BuildMazePart(54.0f, 1.5f, -40.0f, 90.0f, funcCBIndex++);
	BuildMazePart(1.5f, 180.0f, 67.0f, 0.0f, funcCBIndex++);
	BuildMazePart(1.5f, 180.0f, -67.0f, 0.0f, funcCBIndex++);
	
	BuildMazePart(20.0f, 1.5f, 21.0f, -80.0f, funcCBIndex++);
	BuildMazePart(22.0f, 1.5f, 56.0f, -80.0f, funcCBIndex++);
	BuildMazePart(42.0f, 1.5f, -34.0f, -80.0f, funcCBIndex++);
	
	BuildMazePart(1.5f, 31.5f, 45.0f, -65.0f, funcCBIndex++);
	BuildMazePart(1.5f, 41.5f, 31.0f, -60.0f, funcCBIndex++);
	BuildMazePart(1.5f, 31.5f, -13.0f, -65.0f, funcCBIndex++);
	BuildMazePart(1.5f, 31.5f, -55.0f, -65.0f, funcCBIndex++);
	BuildMazePart(1.5f, 26.5f, -16.0f, -103.0f, funcCBIndex++);
	BuildMazePart(1.5f, 26.5f, 16.0f, -103.0f, funcCBIndex++);
	
	BuildMazePart(30.0f, 1.5f, -40.0f, -60.0f, funcCBIndex++);
	BuildMazePart(10.0f, 1.5f, 50.0f, -65.0f, funcCBIndex++);
	BuildMazePart(42.5f, 1.5f, -46.0f, -30.0f, funcCBIndex++);
	BuildMazePart(67.5f, 1.5f, 20.0f, -30.0f, funcCBIndex++);
	
	BuildMazePart(1.5f, 31.5f, -25.0f, -45.0f, funcCBIndex++);
	BuildMazePart(1.5f, 35.5f, 12.0f, -48.0f, funcCBIndex++);
	BuildMazePart(20.0f, 1.5f, 21.5f, -55.0f, funcCBIndex++);
	
	BuildMazePart(42.5f, 1.5f, -46.0f, 0.0f, funcCBIndex++);
	BuildMazePart(42.5f, 1.5f, -34.0f, -15.0f, funcCBIndex++);
	BuildMazePart(1.5f, 31.5f, -13.0f, -15.0f, funcCBIndex++);
	
	BuildMazePart(1.5f, 41.5f, 31.0f, 5.0f, funcCBIndex++);
	BuildMazePart(24.0f, 1.5f, 55.0f, -15.0f, funcCBIndex++); 
	BuildMazePart(24.0f, 1.5f, 43.0f, 0.0f, funcCBIndex++);
	BuildMazePart(24.0f, 1.5f, 19.0f, -15.0f, funcCBIndex++);
	BuildMazePart(36.0f, 1.5f, 49.0f, 25.0f, funcCBIndex++);
	BuildMazePart(1.5f, 15.0f, 49.0f, 7.5f, funcCBIndex++);
	
	BuildMazePart(32.5f, 1.5f, 2.5f, 0.0f, funcCBIndex++);
	BuildMazePart(1.5f, 30.0f, 0.0f, 15.0f, funcCBIndex++);
	
	BuildMazePart(32.5f, 1.5f, -41.0f, 80.0f, funcCBIndex++);
	BuildMazePart(1.5f, 20.0f, -41.0f, 70.0f, funcCBIndex++);
	BuildMazePart(13.5f, 1.5f, -47.0f, 60.0f, funcCBIndex++);
	BuildMazePart(1.5f, 11.5f, -53.0f, 66.0f, funcCBIndex++);
	
	BuildMazePart(13.5f, 1.5f, -27.0f, 57.0f, funcCBIndex++);
	BuildMazePart(1.5f, 11.5f, -33.0f, 63.0f, funcCBIndex++);
	BuildMazePart(13.5f, 1.5f, -27.0f, 69.0f, funcCBIndex++);
	BuildMazePart(1.5f, 11.5f, -21.0f, 63.0f, funcCBIndex++);
	
	BuildMazePart(32.5f, 1.5f, -41.0f, 35.0f, funcCBIndex++);
	BuildMazePart(1.5f, 20.0f, -41.0f, 25.0f, funcCBIndex++);
	BuildMazePart(13.5f, 1.5f, -47.0f, 15.0f, funcCBIndex++);
	BuildMazePart(1.5f, 11.5f, -53.0f, 21.0f, funcCBIndex++);
	
	BuildMazePart(17.5f, 1.5f, -20.0f, 9.0f, funcCBIndex++);
	BuildMazePart(1.5f, 11.5f, -28.0f, 15.0f, funcCBIndex++);
	BuildMazePart(17.5f, 1.5f, -20.0f, 21.0f, funcCBIndex++);
	BuildMazePart(1.5f, 11.5f, -12.0f, 15.0f, funcCBIndex++);
	
	BuildMazePart(35.0f, 1.5f, -49.0f, 47.0f, funcCBIndex++);
	BuildMazePart(1.5f, 61.5f, -13.0f, 60.0f, funcCBIndex++);
	BuildMazePart(13.5f, 1.5f, -6.0f, 30.0f, funcCBIndex++);
	
	BuildMazePart(1.5f, 31.5f, 13.0f, 75.0f, funcCBIndex++);
	BuildMazePart(36.5f, 1.5f, 6.0f, 45.0f, funcCBIndex++);
	BuildMazePart(16.5f, 1.5f, 23.0f, 25.0f, funcCBIndex++);
	BuildMazePart(1.5f, 21.5f, 24.0f, 55.0f, funcCBIndex++);
	BuildMazePart(1.5f, 15.0f, 24.0f, 82.5f, funcCBIndex++);
	BuildMazePart(27.5f, 1.5f, 37.0f, 65.0f, funcCBIndex++);
	BuildMazePart(1.5f, 15.0f, 39.0f, 72.5f, funcCBIndex++);
	BuildMazePart(1.5f, 25.0f, 42.0f, 37.5f, funcCBIndex++);
	BuildMazePart(12.5f, 1.5f, 48.0f, 40.0f, funcCBIndex++);

	

}

void TreeBillboardsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		//step3
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

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TreeBillboardsApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

float TreeBillboardsApp::GetHillsHeight(float x, float z)const
{
    return 0.3f*(z*sinf(0.1f*x) + x*cosf(0.1f*z));
}

XMFLOAT3 TreeBillboardsApp::GetHillsNormal(float x, float z)const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
        1.0f,
        -0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
