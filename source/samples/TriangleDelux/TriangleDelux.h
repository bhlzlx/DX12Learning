#include <NixApplication.h>
//#include <nix/io/archieve.h>
#include <cstdio>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include "d3dx12.h"
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
    // Graphics queue
    ComPtr<ID3D12CommandQueue>          m_graphicsCommandQueue;
    ComPtr<ID3D12GraphicsCommandList>   m_graphicsCommandLists[MaxFlightCount];
    ComPtr<ID3D12CommandAllocator>      m_graphicsCommandAllocator[MaxFlightCount];
    // Upload queue
    ComPtr<ID3D12CommandQueue>          m_uploadQueue;
    ComPtr<ID3D12CommandAllocator>      m_uploadCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList>   m_uploadCommandList;
    //
    ComPtr<ID3D12Fence>                 m_graphicsFences[MaxFlightCount];
    HANDLE                              m_graphicsFenceEvent;
    uint64_t                            m_graphicsFenceValues[MaxFlightCount];
    //
    bool                                m_running;
    //
    ComPtr<ID3D12Fence>                 m_uploadFence;
    HANDLE                              m_uploadFenceEvent;
    uint64_t                            m_uploadFenceValue;
    //
    uint32_t                            m_flightIndex;
public:
    DeviceDX12() {
    }

    bool 
    initialize();

    ComPtr<IDXGISwapChain3> 
    createSwapchain( HWND _hwnd, uint32_t _width, uint32_t _height );

    ComPtr<ID3D12GraphicsCommandList> 
    onTick( uint64_t _dt, uint32_t _flightIndex );

    void 
    executeCommand( ComPtr<ID3D12GraphicsCommandList>& _commandList );

    void
    waitForFlight( uint32_t _flight );

    void
    waitCopyQueue();

    void 
    flushGraphicsQueue();

    ComPtr<ID3D12Resource>
    createVertexBuffer( const void* _data, size_t _length );

    void
    uploadBuffer( ComPtr<ID3D12Resource> _vertexBuffer, const void* _data, size_t _length );

    // Create a simple texture
    ComPtr<ID3D12Resource>
    createTexture();

    operator ComPtr<ID3D12Device> () const;
};

class TriangleDelux : public NixApplication {
private:
	Nix::IArchive*                  m_archive;
    DeviceDX12                      m_device;
	//  
    void*                           m_hwnd;
    ComPtr<IDXGISwapChain3>         m_swapchain;
    uint32_t                        m_flightIndex;
    //
    ComPtr<ID3D12DescriptorHeap>    m_rtvDescriptorHeap;
    uint32_t                        m_rtvDescriptorSize;
    ComPtr<ID3D12DescriptorHeap>    m_samplerDescriptorHeap;
    uint32_t                        m_samplerDescriptorSize;
    ComPtr<ID3D12DescriptorHeap>    m_pipelineDescriptorHeap;
    ComPtr<ID3D12Resource>          m_renderTargets[MaxFlightCount];
    //
	ID3D12PipelineState*		    m_pipelineStateObject;
	ID3D12RootSignature*		    m_pipelineRootSignature;
	D3D12_VIEWPORT				    m_viewport;
	D3D12_RECT					    m_scissor;
    // Resources
	ComPtr<ID3D12Resource>		    m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW	    m_vertexBufferView;
    // Constant buffer
    ComPtr<ID3D12Resource>          m_constantBuffer;
    CD3DX12_GPU_DESCRIPTOR_HANDLE   m_constantBufferGPUDescriptorHandle;
    size_t                          m_constantBufferFlightSize;
    // Texture
	ComPtr<ID3D12Resource>		    m_simpleTexture;
    CD3DX12_GPU_DESCRIPTOR_HANDLE   m_simpleTextureViewGPUDescriptorHandle;
    // Sampler 其实我们能发现，sampler 对程序员来说无需管理GPU资源，实际可能占也可能不占，我们能管理的也只有 heap/handle
    D3D12_GPU_DESCRIPTOR_HANDLE     m_samplerGPUDescriptorHandle;
public:
	virtual bool initialize(void* _wnd, Nix::IArchive*) override;

	virtual void resize(uint32_t _width, uint32_t _height) override;

	virtual void release() override;

	virtual void tick() override;

	virtual const char * title() override;

	virtual uint32_t rendererType() override;
};

NixApplication* GetApplication();