#include <D3Dcompiler.h>
#include <WindowsX.h>
#include <windows.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <exception>
#include <d3d12.h>
#include <iostream>
#include <dxgi1_4.h>
#include "d3dx12.h"
using namespace Microsoft::WRL;
bool mEnableMSAA = false;
int mClientWidth = 800;
int mClinetHeight = 600;
const int SwapChainBufferCount = 2;
HWND mhMainWnd;

ComPtr<IDXGIFactory4> mdxgiFactory;
ComPtr<ID3D12Device> md3dDevice;
ComPtr<ID3D12Fence> mFence;
UINT mRTVDescSize;
UINT mDSVDescSize;
UINT mCbvDescSize;
DXGI_FORMAT mBackBufferFormat= DXGI_FORMAT_R8G8B8A8_UNORM;
DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
UINT m4xMsaaQuality = 0;
ComPtr<ID3D12CommandQueue> mCommandQueue;
ComPtr<ID3D12CommandAllocator> mCommandAlloc;
ComPtr<ID3D12GraphicsCommandList> mCommandList;
ComPtr<IDXGISwapChain>	mSwapChain;
ComPtr<ID3D12DescriptorHeap> mRtvHeap;
ComPtr<ID3D12DescriptorHeap> mDsvHeap;
ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
ComPtr<ID3D12Resource> mDepthStencilBuffer;




void InitD3DRHI()
{
	// debug
#if defined(DEBUG)||defined(_DEBUG)
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) )
		{
			debugController->EnableDebugLayer();
		}
		else
		{
			std::cout << "InitDebug Error";
			return;
		}
	}
#endif
	//DXGI Create
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory))))
	{
		std::cout << "Create DXGIFactory Error";
		return;
	}

	//device create
	HRESULT hardwareResult =D3D12CreateDevice(
	nullptr,
	D3D_FEATURE_LEVEL_11_0,
	IID_PPV_ARGS(&md3dDevice)
	);

	//software device create
	if (FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter> pWarpAdapter;
		mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter));

		if (
			FAILED(
			D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&md3dDevice)))
			)
		{
			std::cout << "Create Soft Warp Adapter Error";
			return;
		}
	}

	//Create Fence
	md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));

	//Get Desciption Size
	mRTVDescSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDSVDescSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvDescSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//Check&&Get 4xMSAA QualityLevel
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = mBackBufferFormat;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	msQualityLevels.SampleCount = 4;
	if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels))))
	{
		std::cout << "CheckFeatureSupport For MSAA Error";
		return;
	}
	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	if (m4xMsaaQuality<=0)
	{
		std::cout << "MSAAQualityLevel<0";
		return;
	}


	//CommandQueue && CommandAlloc && CommandList
	{
		//CommandQueue
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		if (FAILED(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue))))
		{
			std::cout << "Error CreateCommand Queue";
			return;
		}	
		//CommandAlloc
		if (FAILED(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mCommandAlloc.GetAddressOf()))))
		{
			std::cout << "Error Alloc";
			return;
		}
		//CommandList
		md3dDevice->CreateCommandList(0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mCommandAlloc.Get(),
			nullptr,
			IID_PPV_ARGS(mCommandList.GetAddressOf()));
	
		mCommandList->Close();
	
	}
	//Swap Chain
	{
		mSwapChain.Reset();
		DXGI_SWAP_CHAIN_DESC scDesc;
		scDesc.BufferDesc.Width  = mClientWidth;
		scDesc.BufferDesc.Height = mClinetHeight;
		scDesc.BufferDesc.RefreshRate.Numerator = 60;
		scDesc.BufferDesc.RefreshRate.Denominator = 1;
		scDesc.BufferDesc.Format = mBackBufferFormat;
		scDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		scDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		scDesc.SampleDesc.Count = mEnableMSAA ? 4 : 1;
		scDesc.SampleDesc.Quality = mEnableMSAA ? (m4xMsaaQuality - 1) : 0;
		scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scDesc.BufferCount = SwapChainBufferCount;
		scDesc.OutputWindow = mhMainWnd;
		scDesc.Windowed = true;
		scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		if (FAILED(mdxgiFactory->CreateSwapChain(mCommandQueue.Get(),&scDesc,mSwapChain.GetAddressOf())))
		{
			std::cout << "Crewate SwapChain Error";
			return;
		}
	}
	//Create Desc Heap
	{
		//Reset
		mRtvHeap.Reset();
		mDsvHeap.Reset();

		//Create Rtv Desc Heap
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
		rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NodeMask = 0;
		md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf()));

		//Create DSV Desc Heap
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDESC;
		dsvHeapDESC.NumDescriptors = 1;
		dsvHeapDESC.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDESC.NodeMask = 0;
		dsvHeapDESC.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		md3dDevice->CreateDescriptorHeap(&dsvHeapDESC, IID_PPV_ARGS(mDsvHeap.GetAddressOf()));
	}

	//Create RTV:Bind RTV Buffer With RTV Desc 
	{
		//Rest RTVBuufer
		for (UINT i=0;i<SwapChainBufferCount;i++)
		{
			mSwapChainBuffer[i].Reset();
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandler(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
		for (UINT i=0;i<SwapChainBufferCount;i++)
		{
			mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i]));
			md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandler);
			rtvHeapHandler.Offset(1, mRTVDescSize);
		}
	
	}

	//Create Depth Buffer && (CreateDSV: Bind Depth Buffer With DSV Desc)
	{
		mDepthStencilBuffer.Reset();
		//Create Depth Stencil Buffer
		D3D12_RESOURCE_DESC depthStencilDesc;
		depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilDesc.Width = mClientWidth;
		depthStencilDesc.Height = mClinetHeight;
		depthStencilDesc.DepthOrArraySize = 1;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthStencilDesc.SampleDesc.Count = mEnableMSAA ? 4 : 1;
		depthStencilDesc.SampleDesc.Quality = mEnableMSAA ? m4xMsaaQuality : 0;
		depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE optClear;
		optClear.Format = mDepthStencilFormat;
		optClear.DepthStencil.Depth = 1.0f;
		optClear.DepthStencil.Stencil = 0.0f;

		md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_COMMON,
			&optClear,
			IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())
		);

		//Bind Buffer With DSV 
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = mDepthStencilFormat;
		dsvDesc.Texture2D.MipSlice = 0;
		md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(),
			&dsvDesc, mDsvHeap->GetCPUDescriptorHandleForHeapStart());

		//Resouce Transition
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_DEPTH_WRITE));
	}
}


int main()
{

	return 0;
}