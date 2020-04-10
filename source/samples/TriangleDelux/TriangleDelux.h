#include <NixApplication.h>
//#include <nix/io/archieve.h>
#include <cstdio>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <algorithm>
#include <wrl/client.h>

constexpr uint32_t MaxFlightCount = 2;

using namespace Microsoft::WRL;

class DeviceDX12 {
private:
    ComPtr<ID3D12Debug>                 m_debugController;
    ComPtr<IDXGIFactory4>               m_factory;
    //          
    ComPtr<IDXGIAdapter>                m_warpAdapter;
    ComPtr<IDXGIAdapter1>               m_hardwareAdapter;
    //          
    ComPtr<ID3D12Device>                m_device;
    //
    ComPtr<ID3D12CommandQueue>          m_commandQueue;
    ComPtr<ID3D12GraphicsCommandList>   m_commandLists[MaxFlightCount];
    ComPtr<ID3D12CommandAllocator>      m_commandAllocators[MaxFlightCount];
    //
    ComPtr<ID3D12Fence>                 m_fences[MaxFlightCount];
    HANDLE                              m_fenceEvent;
    uint64_t                            m_fenceValues[MaxFlightCount];
public:
    DeviceDX12() {

    }

    bool initialize() {
        uint32_t dxgiFactoryFlags = 0;
#ifdef _DEBUG
        {
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController)))) {
                m_debugController->EnableDebugLayer();
                // Enable additional debug layers.
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif
        HRESULT rst = 0;
        ComPtr<IDXGIFactory4> factory;
        rst = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
        //
        if(FAILED(rst)){
            return false;
        }
        // Get a high performence software adapter if you want!
        // rst = factory->EnumWarpAdapter(IID_PPV_ARGS(&m_warpAdapter));
        // if(FAILED(rst)){
        //     return false;
        // }
        // Get a hardware adapter
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &m_hardwareAdapter); ++adapterIndex) {
            DXGI_ADAPTER_DESC1 desc;
            m_hardwareAdapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }
            if (SUCCEEDED(D3D12CreateDevice(m_hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
                break;
            }
        }

        // Create device with the adapter
        rst = D3D12CreateDevice(
            m_hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            );
        if(FAILED(rst)){
            return false;
        }
        // Create command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};{
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        }
        rst = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
        if(FAILED(rst)){
            return false;
        }
        // Create command allocator
        for( uint32_t i = 0; i<MaxFlightCount; ++i){
            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]));
            // Create command list
            m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[i].Get(), nullptr, IID_PPV_ARGS(&m_commandLists[i]));
            // Create fences & initialize fence values
            m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fences[i]));
            m_fenceValues[i] = 0;
        }

        // Create fence event
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        return true;
    }

    ComPtr<IDXGISwapChain3> createSwapchain( HWND _hwnd, uint32_t _width, uint32_t _height ) {
        ComPtr<IDXGISwapChain1> swapchain;
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};{
            swapChainDesc.BufferCount = MaxFlightCount;
            swapChainDesc.Width = _width;
            swapChainDesc.Height = _height;
            swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swapChainDesc.SampleDesc.Count = 1;
        }
        //
        m_factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        _hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapchain
        );
        HRESULT rst = m_factory->MakeWindowAssociation(_hwnd, DXGI_MWA_NO_ALT_ENTER);
        if(FAILED(rst)){
            return nullptr;
        }
        ComPtr<IDXGISwapChain3> finalSwapchain;
        swapchain.As(&finalSwapchain);
        return finalSwapchain;
    }

    ComPtr<ID3D12CommandList> onTick( uint64_t _dt, uint32_t _flightIndex ) {
        HRESULT rst;
        auto& cmdAllocator = m_commandAllocators[_flightIndex];
        auto& cmdList = m_commandLists[_flightIndex];
        cmdAllocator->Reset();
        rst = cmdList->Reset( cmdAllocator.Get(), nullptr );
        if( FAILED(rst)) {
            return nullptr;
        }
        return cmdList;
    }

    operator ComPtr<ID3D12Device> () {
        return m_device;
    }
};

class TriangleDelux : public NixApplication {
private:
	Nix::IArchive*				    m_archive;
    DeviceDX12                      m_device;
	//  
    void*						    m_hwnd;
    IDXGISwapChain3*			    m_swapchain;
    uint32_t					    m_flightIndex;
    //
    ComPtr<ID3D12DescriptorHeap>    m_pipelineDescriptorHeap;
    //
	ID3D12PipelineState*		    m_pipelineStateObject;
	ID3D12RootSignature*		    m_pipelineRootSignature;
	D3D12_VIEWPORT				    m_viewport;
	D3D12_RECT					    m_scissor;
	ID3D12Resource*				    m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW	    m_vertexBufferView;

public:
	virtual bool initialize(void* _wnd, Nix::IArchive*);
	virtual void resize(uint32_t _width, uint32_t _height);
	virtual void release();
	virtual void tick();
	virtual const char * title();
	virtual uint32_t rendererType();
};

NixApplication* GetApplication();