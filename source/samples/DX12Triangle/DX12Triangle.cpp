#include "DX12Triangle.h"
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <DirectXMath.h>
#include <wrl/client.h>
#include <kwheel/base/io/archive.h>


const char vertexShader[] = R"(
float4 main(float3 pos : POSITION) : SV_POSITION
{
	// just pass vertex position straight through
	return float4(pos, 1.0f);
}
)";

const char fragmentShader[] = R"(
float4 main() : SV_TARGET
{
    // return green
    return float4(0.0f, 1.0f, 0.0f, 1.0f);
}
)";

// #include "d3dx12.h"

bool DX12Triangle::initialize( void* _wnd, kwheel::IArchive* _arch ) {

	if (!_wnd || !_arch) {
		return false;
	}
	m_archive = _arch;

	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ID3D12Debug* debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	// 1. create factory
	HRESULT factoryHandle = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_dxgiFactory));
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
	//

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {}; {
		rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSigDesc.NumParameters = 0; // 这个例子不需要传任何资源（ uniform / sampler / texture ）
		rootSigDesc.NumStaticSamplers = 0;
		rootSigDesc.pParameters = nullptr;
		rootSigDesc.pStaticSamplers = nullptr;
	}
	ID3DBlob* signature = nullptr;
	ID3DBlob* error;
	rst = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
	if (FAILED(rst)) {
		return false;
	}
	rst = m_dxgiDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_pipelineRootSignature));
	if (FAILED(rst)) {
		return false;
	}
	ID3DBlob* vertexShader = nullptr;
	ID3DBlob* fragmentShader = nullptr;
	ID3DBlob* errorBuff = nullptr;
	//
	auto file = m_archive->open("shaders/triangle/vertexShader.hlsl", 1);
	if (!file) {
		return false;
	}
	rst = D3DCompile( 
		file->constData(),
		file->size(),
		"vertexShader.hlsl", 
		nullptr, nullptr, 
		"main", "vs_5_0", 
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&vertexShader,
		&errorBuff
	);

	if (FAILED(rst)) {
		OutputDebugStringA((char*)errorBuff->GetBufferPointer());
		return false;
	}
	file->release();
	//
	file = m_archive->open("shaders/triangle/fragmentShader.hlsl", 1);
	if (!file) {
		return false;
	}
	rst = D3DCompile(
		file->constData(),
		file->size(),
		"fragmentShader.hlsl",
		nullptr, nullptr,
		"main", "ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&fragmentShader,
		&errorBuff
	);

	if (FAILED(rst)) {
		OutputDebugStringA((char*)errorBuff->GetBufferPointer());
		return false;
	}

	D3D12_SHADER_BYTECODE pixelShaderBytecode = {}; {
		pixelShaderBytecode.BytecodeLength = fragmentShader->GetBufferSize();
		pixelShaderBytecode.pShaderBytecode = fragmentShader->GetBufferPointer();
	}

	D3D12_SHADER_BYTECODE vertexShaderBytecode = {}; {
		vertexShaderBytecode.BytecodeLength = vertexShader->GetBufferSize();
		vertexShaderBytecode.pShaderBytecode = vertexShader->GetBufferPointer();
	}

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {}; {
		inputLayoutDesc.NumElements = 1;
		inputLayoutDesc.pInputElementDescs = &inputLayout[0];
	}
	DXGI_SAMPLE_DESC sampleDesc = {}; {
		sampleDesc.Count = 1;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {}; {
		auto& desc = pipelineStateDesc;
		desc.InputLayout = inputLayoutDesc;
		desc.pRootSignature = m_pipelineRootSignature;
		desc.VS = vertexShaderBytecode;
		desc.PS = pixelShaderBytecode;
		desc.SampleDesc = sampleDesc;
		desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.DepthStencilState.DepthEnable = false;
		desc.DepthStencilState.StencilEnable = false;
		desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.RasterizerState.CullMode = D3D12_CULL_MODE::D3D12_CULL_MODE_NONE;
		desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.SampleMask = ~0;
	}
	rst = m_dxgiDevice->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pipelineStateObject));
	if (FAILED(rst)) {
		return false;
	}

	// 9. create command list
	// create the command list with the first allocator
	rst = m_dxgiDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0], m_pipelineStateObject, IID_PPV_ARGS(&m_commandList));
	if (FAILED(rst)) {
		return false;
	}
	m_commandList->Close();

	// a triangle

	struct Vertex {
		DirectX::XMFLOAT3 pos;
	};

	Vertex vList[] = {
		{ { 0.0f, 0.5f, 0.5f } },
		{ { 0.5f, -0.5f, 0.5f } },
		{ { -0.5f, -0.5f, 0.5f } },
	};

	int vBufferSize = sizeof(vList);

	// create default heap
	// default heap is memory on the GPU. Only the GPU has access to this memory
	// To get data into this heap, we will have to upload the data using
	// an upload heap
	m_dxgiDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), // a default heap
		D3D12_HEAP_FLAG_NONE, // no flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // resource description for a buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // we will start this heap in the copy destination state since we will copy data
										// from the upload heap to this heap
		nullptr, // optimized clear value must be null for this type of resource. used for render targets and depth/stencil buffers
		IID_PPV_ARGS(&m_vertexBuffer));

	// we can give resource heaps a name so when we debug with the graphics debugger we know what resource we are looking at
	m_vertexBuffer->SetName(L"Vertex Buffer Resource Heap");

	// create upload heap
	// upload heaps are used to upload data to the GPU. CPU can write to it, GPU can read from it
	// We will upload the vertex buffer using this heap to the default heap
	ID3D12Resource* vBufferUploadHeap;
	m_dxgiDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // upload heap
		D3D12_HEAP_FLAG_NONE, // no flags
		&CD3DX12_RESOURCE_DESC::Buffer(vBufferSize), // resource description for a buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // GPU will read from this buffer and copy its contents to the default heap
		nullptr,
		IID_PPV_ARGS(&vBufferUploadHeap));
	vBufferUploadHeap->SetName(L"Vertex Buffer Upload Resource Heap");

	m_commandAllocators[m_flightIndex]->Reset();
	m_commandList->Reset(m_commandAllocators[m_flightIndex], m_pipelineStateObject);

	// store vertex buffer in upload heap
	D3D12_SUBRESOURCE_DATA vertexData = {};
	vertexData.pData = reinterpret_cast<BYTE*>(vList); // pointer to our vertex array
	vertexData.RowPitch = vBufferSize; // size of all our triangle vertex data
	vertexData.SlicePitch = vBufferSize; // also the size of our triangle vertex data
										 // we are now creating a command with the command list to copy the data from
										 // the upload heap to the default heap
	UpdateSubresources( m_commandList, m_vertexBuffer, vBufferUploadHeap, 0, 0, 1, &vertexData);

	// transition the vertex buffer data from copy destination state to vertex buffer state
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

	// Now we execute the command list to upload the initial assets (triangle data)
	m_commandList->Close();
	ID3D12CommandList* ppCommandLists[] = { m_commandList };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// increment the fence value now, otherwise the buffer might not be uploaded by the time we start drawing
	++m_fenceValue[m_flightIndex];
	rst = m_commandQueue->Signal(m_fence[m_flightIndex], m_fenceValue[m_flightIndex]);
	if (FAILED(rst)) {
		m_running = false;
	}
	// create a vertex buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVirtualAddress() method
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.StrideInBytes = sizeof(Vertex);
	m_vertexBufferView.SizeInBytes = vBufferSize;
	//
	return true;
}
    
void DX12Triangle::resize(uint32_t _width, uint32_t _height) {
    {// re-create the swapchain!
	 // Fill out the Viewport
		m_viewport.TopLeftX = 0;
		m_viewport.TopLeftY = 0;
		m_viewport.Width = _width;
		m_viewport.Height = _height;
		m_viewport.MinDepth = 0.0f;
		m_viewport.MaxDepth = 1.0f;

		// Fill out a scissor rect
		m_scissor.left = 0;
		m_scissor.top = 0;
		m_scissor.right = _width;
		m_scissor.bottom = _height;
        if (!this->m_swapchain) {
			//
			DXGI_MODE_DESC displayModeDesc = {}; {
				displayModeDesc.Width = _width;
				displayModeDesc.Height = _height;
				displayModeDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 不需要提前获取支持什么格式？ 对于vk来说是必走流程
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
			m_swapchain->ResizeBuffers(MaxFlightCount, _width, _height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
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
    }        

    printf("resized!");
}

void DX12Triangle::release() {
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

void DX12Triangle::tick() {
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
        rst = m_commandList->Reset(commandAllocator, m_pipelineStateObject);
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
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_flightIndex, m_rtvDescriptorSize);
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        // clear color
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        // transfrom the render target's layout

		m_commandList->SetGraphicsRootSignature(m_pipelineRootSignature);
		m_commandList->SetPipelineState(m_pipelineStateObject);
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissor);
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		m_commandList->DrawInstanced(3, 1, 0, 0); // finally draw 3 vertices (draw the triangle)

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
		++m_fenceValue[m_flightIndex];
        HRESULT rst = m_commandQueue->Signal(m_fence[m_flightIndex], m_fenceValue[m_flightIndex]);
        if (FAILED(rst)) {
            m_running = false;
        }
        // present the current backbuffer
        rst = m_swapchain->Present(1, 0);
        if (FAILED(rst)) {
            m_running = false;
        }
    }
}

const char * DX12Triangle::title() {
    return "DX12 Triangle";
}
	
uint32_t DX12Triangle::rendererType() {
	return 0;
}

DX12Triangle theapp;

NixApplication* GetApplication() {
    return &theapp;
}