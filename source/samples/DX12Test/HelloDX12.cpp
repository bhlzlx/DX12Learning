#include "HelloDX12.h"

// #include "d3dx12.h"

bool HelloDX12::initialize( void* _wnd, Nix::IArchive* ) {
	{
		//ComPtr<ID3D12Debug> debugController;
		ID3D12Debug* debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
			// Enable additional debug layers.
			// dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}

	// 1. create factory
	HRESULT factoryHandle = CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory));
	if (FAILED(factoryHandle)) {
		return false;
	}
    // 2.  enum all GPU devices and select a GPU that support DX12
	int adapterIndex = 0;
	bool adapterFound = false;
    while (m_dxgiFactory->EnumAdapters1(adapterIndex, &m_dxgiAdapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        m_dxgiAdapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { // if it's software
            ++adapterIndex;
            continue;
        }
        // test it can create a device that support dx12 or not
        HRESULT rst = D3D12CreateDevice(m_dxgiAdapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr );
        if (SUCCEEDED(rst)) {
            adapterFound = true;
            break;
        }
        ++adapterIndex;
    }
    if (!adapterFound) {
        return false;
    }
    // 3. create the device
    HRESULT rst = D3D12CreateDevice(
        m_dxgiAdapter,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_dxgiDevice)
    );
    if (FAILED(rst)){
        return false;
    }

    // 4. create command queue
    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc; {
        cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        cmdQueueDesc.NodeMask = 0;
    }
    rst = m_dxgiDevice->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(rst)) {
        return false;
    }
    // 5. swapchain / when windows is resize
    this->m_hwnd = _wnd;// handle of the window

    // 6. create descriptor heap
    // describe an rtv descriptor heap and create
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {}; {
        rtvHeapDesc.NumDescriptors = MaxFlightCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    }
       
    rst = m_dxgiDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvDescriptorHeap));
    if (FAILED(rst)) {
        return false;
    }
    m_rtvDescriptorSize = m_dxgiDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 8. create command allocators
    for (int i = 0; i < MaxFlightCount; i++) {
        rst = m_dxgiDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]));
        if (FAILED(rst)) {
            return false;
        }
    }
    // 9. create command list
    // create the command list with the first allocator
    rst = m_dxgiDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0], NULL, IID_PPV_ARGS(&m_commandList));
    if (FAILED(rst)) {
        return false;
    }
    m_commandList->Close();

    // create the fences
    for (int i = 0; i < MaxFlightCount; i++) {
        rst = m_dxgiDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence[i]));
        if (FAILED(rst)) {
            return false;
        }
        m_fenceValue[i] = 0; // set the initial fence value to 0
    }

    // create a handle to a fence event
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        return false;
    }
	return true;
}
    
void HelloDX12::resize(uint32_t _width, uint32_t _height) {
    {// re-create the swapchain!
        if (!this->m_swapchain) {
			//
			DXGI_MODE_DESC displayModeDesc = {}; {
				displayModeDesc.Width = _width;
				displayModeDesc.Height = _height;
				displayModeDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			}
			DXGI_SAMPLE_DESC sampleDesc = {}; {
				sampleDesc.Count = 1;
			}
			// Describe and create the swap chain.
			DXGI_SWAP_CHAIN_DESC swapchainDesc = {}; {
				swapchainDesc.BufferCount = MaxFlightCount;
				swapchainDesc.BufferDesc = displayModeDesc;
				swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				swapchainDesc.OutputWindow = (HWND)m_hwnd;
				swapchainDesc.SampleDesc = sampleDesc;
				swapchainDesc.Windowed = true; // fullsceen or not!!!
			}
			//
			IDXGISwapChain* swapchain = nullptr;
			HRESULT rst = this->m_dxgiFactory->CreateSwapChain(this->m_commandQueue, &swapchainDesc, &swapchain);
			this->m_swapchain = static_cast<IDXGISwapChain3*>(swapchain);
			this->m_flightIndex = m_swapchain->GetCurrentBackBufferIndex();

			// 7. create render targets & render target view
			// Create a RTV for each buffer (double buffering is two buffers, tripple buffering is 3).
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			for (int i = 0; i < MaxFlightCount; i++) {
				// first we get the n'th buffer in the swap chain and store it in the n'th
				// position of our ID3D12Resource array
				HRESULT rst = m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
				if (FAILED(rst)) {
					return;
				}
				// the we "create" a render target view which binds the swap chain buffer (ID3D12Resource[n]) to the rtv handle
				m_dxgiDevice->CreateRenderTargetView(m_renderTargets[i], nullptr, { rtvHandle.ptr + m_rtvDescriptorSize * i });
			}
        }
		else {
			for (uint32_t flightIndex = 0; flightIndex < MaxFlightCount; ++flightIndex) {
				if (m_fence[flightIndex]->GetCompletedValue() < m_fenceValue[flightIndex]) {
					HRESULT rst;
					rst = m_fence[flightIndex]->SetEventOnCompletion(m_fenceValue[flightIndex], m_fenceEvent);
					if (FAILED(rst)) {
						m_running = false;
					}
					WaitForSingleObject(m_fenceEvent, INFINITE);
				}
			}
		}
    }        

    printf("resized!");
}

void HelloDX12::release() {
	for (uint32_t flightIndex = 0; flightIndex < MaxFlightCount; ++flightIndex) {
		if (m_fence[flightIndex]->GetCompletedValue() < m_fenceValue[flightIndex]) {
            HRESULT rst;
			rst = m_fence[flightIndex]->SetEventOnCompletion(m_fenceValue[flightIndex], m_fenceEvent);
			if (FAILED(rst)) {
				m_running = false;
			}
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}
	m_swapchain->Release();
	m_swapchain = nullptr;
	m_rtvDescriptorHeap->Release();
	m_rtvDescriptorHeap = nullptr;
	m_commandList->Release();
	m_commandList = nullptr;

	for (uint32_t flightIndex = 0; flightIndex < MaxFlightCount; ++flightIndex) {
		//
		m_commandAllocators[flightIndex]->Release();
		m_commandAllocators[flightIndex] = nullptr;
		//
		m_fence[flightIndex]->Release();
		m_fence[flightIndex] = nullptr;
	}
    m_dxgiDevice->Release();
    m_dxgiDevice = nullptr;

	
    printf("destroyed");
}

void HelloDX12::tick() {
    if (!m_swapchain) {
        return;
    }
    {
        HRESULT rst;
        m_flightIndex = m_swapchain->GetCurrentBackBufferIndex();
		// 正常上一轮渲染完成 这个 m_fence的completeValue 应该是和 m_fenceValue[m_flightIndex]相等的
		// 所以这里小于就是指这个渲染操作还没完成，其实这里写不等于应该也是没问题的
        if (m_fence[m_flightIndex]->GetCompletedValue() < m_fenceValue[m_flightIndex]) {
			// SetEventOnCompletion : Specifies an event that should be fired when the fence reaches a certain value.
			// 所以这里我们要等它完成，怎么等它完成呢，这里有个 fence 有个功能就是fence的值到了某个值之后可以设置触发某个event
            rst = m_fence[m_flightIndex]->SetEventOnCompletion(m_fenceValue[m_flightIndex], m_fenceEvent);
            if (FAILED(rst)) {
                m_running = false;
            }
			// 然后我们一直在这里等待这个event被触发就行了
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    { //
        ID3D12CommandAllocator* commandAllocator = m_commandAllocators[m_flightIndex];
        HRESULT rst = commandAllocator->Reset();
        if (FAILED(rst)) {
            m_running = false;
        }
        rst = m_commandList->Reset(commandAllocator, NULL);
        if (FAILED(rst)) {
            m_running = false;
        }
        // transform the render target's layout
		D3D12_RESOURCE_BARRIER barrier; {
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.pResource = m_renderTargets[m_flightIndex];
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		}
        m_commandList->ResourceBarrier(1, &barrier);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {}; {
			rtvHandle.ptr = m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + m_flightIndex * m_rtvDescriptorSize;
		}
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        // clear color
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        // transfrom the render target's layout
		std::swap(barrier.Transition.StateAfter, barrier.Transition.StateBefore);
        m_commandList->ResourceBarrier(1, &barrier);
        //
        rst = m_commandList->Close();
        if (FAILED(rst)) {
            m_running = false;
        }
    }
    {
        ID3D12CommandList* ppCommandLists[] = { m_commandList };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        // queue is being executed on the GPU
		// Signal : Updates a fence to a specified value.
		// 队列执行完会给这个fence一个新的值
        HRESULT rst = m_commandQueue->Signal(m_fence[m_flightIndex], ++m_fenceValue[m_flightIndex]);
        if (FAILED(rst)) {
            m_running = false;
        }
        // present the current backbuffer
        rst = m_swapchain->Present(0, 0);
        if (FAILED(rst)) {
            m_running = false;
        }
    }
}

const char * HelloDX12::title() {
    return "DX12 Clear Screen";
}
	
uint32_t HelloDX12::rendererType() {
	return 0;
}

HelloDX12 theapp;

NixApplication* GetApplication() {
    return &theapp;
}