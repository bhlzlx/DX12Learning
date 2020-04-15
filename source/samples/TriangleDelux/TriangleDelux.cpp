#include "TriangleDelux.h"
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <nix/io/archive.h>

bool DeviceDX12::initialize() {
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
	//
	rst = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory));
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
	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != m_factory->EnumAdapters1(adapterIndex, &m_hardwareAdapter); ++adapterIndex) {
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
	// Create graphics command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};{
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	}
	rst = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_graphicsCommandQueue));
	if(FAILED(rst)){
		return false;
	}
	// Create graphics command allocator
	for( uint32_t i = 0; i<MaxFlightCount; ++i){
		m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_graphicsCommandAllocator[i]));
		m_graphicsCommandAllocator[i]->Reset();
		// Create graphics command list
		m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_graphicsCommandAllocator[i].Get(), nullptr, IID_PPV_ARGS(&m_graphicsCommandLists[i]));
		m_graphicsCommandLists[i]->Close();
		// Create fences & initialize fence values
		m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fences[i]));
		m_fenceValues[i] = 0;
	}
	// Create graphics fence event
	m_graphicsFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	// Create copy command queue, command list, command allocator, fence, fence event
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	rst = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_uploadQueue));
	if(FAILED(rst)) {
		return false;
	}
	m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT , IID_PPV_ARGS(&m_uploadCommandAllocator));
	m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_uploadCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_uploadCommandList));
	m_uploadCommandList->Close();
	m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_uploadFence));
	m_uploadFenceValue = 0;
	m_uploadFenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

	return true;
}

void 
DeviceDX12::executeCommand( ComPtr<ID3D12GraphicsCommandList>& _commandList ) {
	ID3D12CommandList* lists[] = {
		_commandList.Get()
	};
	m_graphicsCommandQueue->ExecuteCommandLists( 1, lists );
	//
	auto fenceVaue = ++m_fenceValues[m_flightIndex];
	HRESULT rst = m_graphicsCommandQueue->Signal( m_fences[m_flightIndex].Get(), fenceVaue );
	if (FAILED(rst)) {
		m_running = false;
	}
}

ComPtr<IDXGISwapChain3> DeviceDX12::createSwapchain( HWND _hwnd, uint32_t _width, uint32_t _height ) {
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
	HRESULT rst = m_factory->CreateSwapChainForHwnd(
		m_graphicsCommandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
		_hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapchain
	);
	if( FAILED(rst)) {
		if( rst == DXGI_ERROR_DEVICE_REMOVED) {
			rst = m_device->GetDeviceRemovedReason();
		}
		return nullptr;
	}
	rst = m_factory->MakeWindowAssociation(_hwnd, DXGI_MWA_NO_ALT_ENTER);
	if(FAILED(rst)){
		return nullptr;
	}
	ComPtr<IDXGISwapChain3> finalSwapchain;
	swapchain.As(&finalSwapchain);
	return finalSwapchain;
}

ComPtr<ID3D12GraphicsCommandList> 
DeviceDX12::onTick( uint64_t _dt, uint32_t _flightIndex ) {
	m_flightIndex = _flightIndex;
	HRESULT rst;
	auto& cmdAllocator = m_graphicsCommandAllocator[_flightIndex];
	auto& cmdList = m_graphicsCommandLists[_flightIndex];
	cmdAllocator->Reset();
	rst = cmdList->Reset( cmdAllocator.Get(), nullptr );
	if( FAILED(rst)) {
		return nullptr;
	}
	return cmdList;
}

void
DeviceDX12::waitForFlight( uint32_t _flight ) {
	if (m_fences[_flight]->GetCompletedValue() < m_fenceValues[_flight]) {
		HRESULT rst;
		rst = m_fences[_flight]->SetEventOnCompletion(m_fenceValues[_flight], m_graphicsFenceEvent);
		if (FAILED(rst)) {
			m_running = false;
		}
		WaitForSingleObject(m_graphicsFenceEvent, INFINITE);
	}
}

ComPtr<ID3D12Resource>
DeviceDX12::createVertexBuffer( const void* _data, size_t _length ) {
	D3D12_HEAP_PROPERTIES heapProps; {
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProps.CreationNodeMask = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_L1;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;
	}
	ComPtr<ID3D12Resource> buffer = nullptr;
	HRESULT rst = m_device->CreateCommittedResource( 
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(_length),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer)
	);
	if(FAILED(rst)) {
		return nullptr;
	}
	buffer->SetName(L"VertexBuffer");
	//
	uploadBuffer( buffer, _data, _length );
	//
	return buffer;
}

void
DeviceDX12::uploadBuffer( ComPtr<ID3D12Resource> _vertexBuffer, const void* _data, size_t _length ) {
	HRESULT rst = 0;
	// Create staging buffer
	ComPtr<ID3D12Resource> stagingBuffer;
	m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 							// upload heap
		D3D12_HEAP_FLAG_NONE, 														// no flags
		&CD3DX12_RESOURCE_DESC::Buffer(_length), 									// resource description for a buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, 											// GPU will read from this buffer and copy its contents to the default heap
		nullptr,
		IID_PPV_ARGS(&stagingBuffer));
	stagingBuffer->SetName(L"triangle vertex buffer staging buffer.");
	void* mappedPtr = nullptr;
	stagingBuffer->Map( 0, nullptr, &mappedPtr);
	// store vertex buffer in staging buffer
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT resourceFootprint;
	{
		D3D12_SUBRESOURCE_DATA vertexSubResourceData = {}; {
			vertexSubResourceData.pData = reinterpret_cast<const BYTE*>(_data); 	// pointer to our vertex array
			vertexSubResourceData.RowPitch = _length; 								// size of all our triangle vertex data
			vertexSubResourceData.SlicePitch = _length; 							// also the size of our triangle vertex data
		}
		uint32_t numRows[1];
		uint64_t rowSizesInBytes[1];
		uint64_t bytesTotal;
		m_device->GetCopyableFootprints(
			&_vertexBuffer->GetDesc(), 
			0, 
			1, 
			0,
			&resourceFootprint, 
			numRows, 
			rowSizesInBytes,
			&bytesTotal
		);
	}
	memcpy( mappedPtr, _data, _length );
	stagingBuffer->Unmap( 0, nullptr );
	//
	m_uploadCommandAllocator->Reset();
	m_uploadCommandList->Reset( m_uploadCommandAllocator.Get(), nullptr ); { 
		// record commands
		// copy buffer data
		m_uploadCommandList->CopyBufferRegion( _vertexBuffer.Get() , 0, stagingBuffer.Get(), 0, _length);
		// resource barrier
		auto barrierDesc = CD3DX12_RESOURCE_BARRIER::Transition( _vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );
		m_uploadCommandList->ResourceBarrier( 1, &barrierDesc );
	}
	m_uploadCommandList->Close();
	//
	ID3D12CommandList* lists[] = {
		m_uploadCommandList.Get()
	};
	m_uploadQueue->ExecuteCommandLists( 1, lists );
	m_uploadQueue->Signal( m_uploadFence.Get(), ++m_uploadFenceValue );
	//
	// Wait for upload operation
	if( m_uploadFence->GetCompletedValue() < m_uploadFenceValue ) {
		rst = m_uploadFence->SetEventOnCompletion( m_uploadFenceValue, m_uploadFenceEvent );
		if( FAILED(rst)) {
			assert(false);
		}
        WaitForSingleObject( m_uploadFenceEvent, INFINITE);
	}
	//
	// stagingBuffer->Release();
}

ComPtr<ID3D12Resource>
DeviceDX12::createTexture() {
	ComPtr<ID3D12Resource> texture = nullptr;
	// fill the texture description
	D3D12_RESOURCE_DESC textureDesc = {}; {
		auto& desc = textureDesc;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.Width = 8;
		desc.Height = 8;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE; 			// 用作什么用，比如说 RenderTarget, DepthStencil
		desc.DepthOrArraySize = 1;						// 这一点DX12处理得真的非常到位，Vulkan这里就处理得非常不好
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
	}
	CD3DX12_HEAP_PROPERTIES heapProperty ( D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT );
	// Create a texture object
	HRESULT rst = m_device->CreateCommittedResource( 
		&heapProperty, 
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texture)
	);
	if(FAILED(rst)) {
		return nullptr;
	}
	//
	UINT64 RequiredSize = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, nullptr, nullptr, &RequiredSize );
	// Create staging buffer
	ComPtr<ID3D12Resource> stagingBuffer = nullptr;
	//
	uint32_t pixelData[] = {
		0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff,
		0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff,
		0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff,
		0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff,
		0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff,
		0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff,
		0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff,
		0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff,
	};
	
	m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 							// upload heap
		D3D12_HEAP_FLAG_NONE, 														// no flags
		&CD3DX12_RESOURCE_DESC::Buffer(RequiredSize),							// resource description for a buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, 											// GPU will read from this buffer and copy its contents to the default heap
		nullptr,
		IID_PPV_ARGS(&stagingBuffer));
	stagingBuffer->SetName(L"simple texture staging buffer.");

	void* mappedPtr = nullptr;
	stagingBuffer->Map( 0, nullptr, &mappedPtr );
	memcpy( mappedPtr, pixelData, sizeof(pixelData) );
	stagingBuffer->Unmap( 0, nullptr );
	//
	// Copy pixel data into the texture object
	m_uploadCommandAllocator->Reset();
	m_uploadCommandList->Reset( m_uploadCommandAllocator.Get(), nullptr ); { 
		CD3DX12_TEXTURE_COPY_LOCATION dst (texture.Get(), 0);
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		m_device->GetCopyableFootprints( &texture->GetDesc(), 0, 1, 0, &footprint, nullptr, nullptr, nullptr );
		CD3DX12_TEXTURE_COPY_LOCATION src( stagingBuffer.Get(), footprint );
		m_uploadCommandList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr);
		// resource barrier
		auto barrierDesc = CD3DX12_RESOURCE_BARRIER::Transition( texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		m_uploadCommandList->ResourceBarrier( 1, &barrierDesc );
	}
	m_uploadCommandList->Close();
	//
	ID3D12CommandList* lists[] = {
		m_uploadCommandList.Get()
	};
	m_uploadQueue->ExecuteCommandLists( 1, lists );
	m_uploadQueue->Signal( m_uploadFence.Get(), ++m_uploadFenceValue );
	//
	// Wait for upload operation
	if( m_uploadFence->GetCompletedValue() < m_uploadFenceValue ) {
		rst = m_uploadFence->SetEventOnCompletion( m_uploadFenceValue, m_uploadFenceEvent );
		if( FAILED(rst)) {
			assert(false);
			return nullptr;
		}
        WaitForSingleObject( m_uploadFenceEvent, INFINITE);
	}
	//
	// stagingBuffer->Release();
	// Return!!
	return texture;
}

DeviceDX12::operator ComPtr<ID3D12Device> () const {
	return m_device;
}

bool TriangleDelux::initialize( void* _wnd, Nix::IArchive* _arch ) {

	if (!_wnd || !_arch) {
		return false;
	}
	m_archive = _arch;

	if(!this->m_device.initialize()){
		return false;
	}
    // 5. swapchain / when windows is resize
    this->m_hwnd = _wnd;// handle of the window

	HRESULT rst = 0;

	ComPtr<ID3D12Device> device = (ComPtr<ID3D12Device>)m_device;

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};{
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        rst = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_pipelineDescriptorHeap));
		if( FAILED(rst)) {
			return false;
		}
	}

//	CD3DX12_DESCRIPTOR_RANGE1 vertexDescriptorRanges[1];{
//		vertexDescriptorRanges[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
//	}
//
//	CD3DX12_DESCRIPTOR_RANGE1 pixelDescriptorRanges[2];{
//		// uniform buffer
//		pixelDescriptorRanges[0].Init(
//			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,				// descriptor type
//			1,												// descriptor count
//			0,												// base shader register
//			0,												// register space
//			D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC				// descriptor data type
//		);
//		// sampler & texture
//		pixelDescriptorRanges[1].Init(
//			D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,			// descriptor type
//			1,												// descriptor count
//			0,												// base shader register
//			0,												// register space
//			D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE// descriptor data type
//		);
//	}
//
//	constexpr int vertexRangeCount = sizeof(vertexDescriptorRanges)/sizeof(CD3DX12_DESCRIPTOR_RANGE1);
//	constexpr int pixelRangeCount = sizeof(pixelDescriptorRanges)/sizeof(CD3DX12_DESCRIPTOR_RANGE1);
//	//
//	CD3DX12_ROOT_PARAMETER1 rootParameters[3];{
//		rootParameters[0].InitAsDescriptorTable( vertexRangeCount , &vertexDescriptorRanges[0], D3D12_SHADER_VISIBILITY_VERTEX );
//		rootParameters[1].InitAsDescriptorTable( 1 , &pixelDescriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL );
//		rootParameters[2].InitAsDescriptorTable( 1 , &pixelDescriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL );
//	}

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc {};{
		// rootSignatureDesc.Init_1_1( 2, rootParameters, 0, nullptr );
		rootSignatureDesc.Init_1_1( 0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT );
	}

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	// Create root signature
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if ( FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}
	
	rst = D3DX12SerializeVersionedRootSignature( &rootSignatureDesc, featureData.HighestVersion, &signature, &error);
	if (FAILED(rst)) {
		const char * msg = (const char *)error->GetBufferPointer();
		OutputDebugStringA(msg);
		return false;
	}
	rst = device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_pipelineRootSignature));
	if (FAILED(rst)) {
		return false;
	}
	//
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

	// create pipeline state

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
	rst = device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS( &m_pipelineStateObject));
	if (FAILED(rst)) {
		return false;
	}

	// a triangle
	struct Vertex {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT2 uv;
	};

	Vertex vertexData[] = {
		{ { 0.0f, 0.5f, 0.5f }, { 0.5f, 0.0f } },
		{ { 0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f } },
		{ { -0.5f, -0.5f, 0.5f }, { 0.0f, 1.0f } },
	};

	int vertexBufferSize = sizeof(vertexData);

	this->m_vertexBuffer = m_device.createVertexBuffer( vertexData, vertexBufferSize);
	{ // create vertex buffer view
		this->m_vertexBufferView.BufferLocation = 0;
		this->m_vertexBufferView.SizeInBytes = sizeof(vertexData);
		this->m_vertexBufferView.StrideInBytes = sizeof(Vertex);
	}
	
	this->m_simpleTexture = m_device.createTexture();
	// create a vertex buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVirtualAddress() method
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.StrideInBytes = sizeof(Vertex);
	m_vertexBufferView.SizeInBytes = vertexBufferSize;
	//
	return true;
}
    
void TriangleDelux::resize(uint32_t _width, uint32_t _height) {
    {// re-create the swapchain!
	 // Fill out the Viewport
		m_viewport.TopLeftX = 0;
		m_viewport.TopLeftY = 0;
		m_viewport.Width = static_cast<float>(_width);
		m_viewport.Height = static_cast<float>(_height);
		m_viewport.MinDepth = 0.0f;
		m_viewport.MaxDepth = 1.0f;

		// Fill out a scissor rect
		m_scissor.left = 0;
		m_scissor.top = 0;
		m_scissor.right = _width;
		m_scissor.bottom = _height;
        if (!this->m_swapchain) {
			
			HRESULT rst = 0;
			DXGI_MODE_DESC displayModeDesc = {}; {
				displayModeDesc.Width = _width;
				displayModeDesc.Height = _height;
				displayModeDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 不需要提前获取支持什么格式？ 对于vk来说是必走流程
			}
			DXGI_SAMPLE_DESC sampleDesc = {}; {
				sampleDesc.Count = 1;
			}
			m_swapchain = m_device.createSwapchain((HWND)m_hwnd, _width, _height );

			ComPtr<ID3D12Device> device = (ComPtr<ID3D12Device>)m_device;

			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {}; {
				rtvHeapDesc.NumDescriptors = MaxFlightCount;
				rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			}
			//
			rst = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvDescriptorHeap));
			if( FAILED(rst)) {
				return;
			}
			m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			// 7. create render targets & render target view
			// Create a RTV for each buffer (double buffering is two buffers, tripple buffering is 3).
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			for (int i = 0; i < MaxFlightCount; i++) {
				// first we get the n'th buffer in the swap chain and store it in the n'th
				// position of our ID3D12Resource array
				HRESULT rst = m_swapchain->GetBuffer( i, IID_PPV_ARGS(&m_renderTargets[i]));
				m_renderTargets[i]->SetName(L"swapchain rt");
				if (FAILED(rst)) {
					return;
				}
				// the we "create" a render target view which binds the swap chain buffer (ID3D12Resource[n]) to the rtv handle
				device->CreateRenderTargetView( m_renderTargets[i].Get(), nullptr, { rtvHandle.ptr + m_rtvDescriptorSize * i });
			}
        }
		else {
			ComPtr<ID3D12Device> device = (ComPtr<ID3D12Device>)m_device;
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			// wait for graphics queue
			for (uint32_t flightIndex = 0; flightIndex < MaxFlightCount; ++flightIndex) {
				m_device.waitForFlight( flightIndex );
				// m_device.onTick(1, flightIndex);
				m_renderTargets[flightIndex].Reset();
			}
			HRESULT rst = m_swapchain->ResizeBuffers( MaxFlightCount, _width, _height, DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
			if( FAILED(rst)) {
				OutputDebugString(L"Error!");
			}
			//for (int i = 0; i < MaxFlightCount; i++) {
			//	HRESULT rst = m_swapchain->GetBuffer( i, IID_PPV_ARGS(&m_renderTargets[i]));
			//	if (FAILED(rst)) {
			//		return;
			//	}
			//	// the we "create" a render target view which binds the swap chain buffer (ID3D12Resource[n]) to the rtv handle
			//	device->CreateRenderTargetView( m_renderTargets[i].Get(), nullptr, { rtvHandle.ptr + m_rtvDescriptorSize * i });
			//}
		}
    }
    printf("resized!");
}

void TriangleDelux::release() {
	for (uint32_t flightIndex = 0; flightIndex < MaxFlightCount; ++flightIndex) {
		m_device.waitForFlight( flightIndex );
	}
	m_swapchain->Release();
	m_swapchain = nullptr;
	m_rtvDescriptorHeap->Release();
	m_rtvDescriptorHeap = nullptr;
	//	
    printf("destroyed");
}

void TriangleDelux::tick() {
    if (!m_swapchain) {
        return;
    }
	HRESULT rst;
    {
        m_flightIndex = m_swapchain->GetCurrentBackBufferIndex();
		m_device.waitForFlight(m_flightIndex);
    }

    {
		ComPtr<ID3D12GraphicsCommandList> commandList = m_device.onTick( 0, m_flightIndex );
        // transform the render target's layout
		D3D12_RESOURCE_BARRIER barrier; {
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.pResource = m_renderTargets[m_flightIndex].Get();
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		}
        commandList->ResourceBarrier(1, &barrier);
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_flightIndex, m_rtvDescriptorSize);
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        // clear color
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        // transfrom the render target's layout

		commandList->SetGraphicsRootSignature(m_pipelineRootSignature);
		commandList->SetPipelineState(m_pipelineStateObject);
		commandList->RSSetViewports(1, &m_viewport);
		commandList->RSSetScissorRects(1, &m_scissor);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		commandList->DrawInstanced(3, 1, 0, 0); // finally draw 3 vertices (draw the triangle)

		std::swap(barrier.Transition.StateAfter, barrier.Transition.StateBefore);
        commandList->ResourceBarrier(1, &barrier);
        //
        rst = commandList->Close();
		//
		m_device.executeCommand(commandList);
    }
    {
        // present the current backbuffer
        rst = m_swapchain->Present(1, 0);
    }
}

const char * TriangleDelux::title() {
    return "DX12 Triangle";
}
	
uint32_t TriangleDelux::rendererType() {
	return 0;
}

TriangleDelux theapp;

NixApplication* GetApplication() {
    return &theapp;
}