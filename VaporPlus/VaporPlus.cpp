#include "stdafx.h"
#include "VaporPlus.h"
#include "DirectXRaytracingHelper.h"
#include "CheckCast.h"
#include "CompiledShaders\Raytracing.hlsl.h"
#include "CompiledShaders\PostprocessVS.hlsl.h"
#include "CompiledShaders\PostprocessPS.hlsl.h"

const wchar_t* VaporPlus::c_hitGroupName = L"MyHitGroup";
const wchar_t* VaporPlus::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* VaporPlus::c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t* VaporPlus::c_missShaderName = L"MyMissShader";
const wchar_t* VaporPlus::c_missShaderName_Shadow = L"MyMissShader_ShadowRay";
const wchar_t* VaporPlus::c_missShaderName_Reflection = L"MyMissShader_Reflection";

DWORD WINAPI MusicThread(LPVOID lpParam)
{
	PlaySoundW(L"Plus.wav", nullptr, SND_FILENAME);
	return 0;
}

VaporPlus::VaporPlus(UINT width, UINT height, std::wstring name)
    : DXSample(width, height, name)
    , m_raytracingOutputResourceUAVDescriptorHeapIndexDuringRaytracing(UINT_MAX)
    , m_curRotationAngleRad(0.0f)
    , m_isDxrSupported(false)
	, m_enableTextFrame(false)
	, m_enablePostprocess(false)
{
    m_forceComputeFallback = false;
    SelectRaytracingAPI(RaytracingAPI::FallbackLayer);
    UpdateForSizeChange(width, height);

	QueryPerformanceFrequency(&m_performanceCounter);
	QueryPerformanceFrequency(&m_performanceFrequency);
}

void VaporPlus::EnableDXRExperimentalFeatures(IDXGIAdapter1* adapter)
{
    // DXR is an experimental feature and needs to be enabled before creating a D3D12 device.
    m_isDxrSupported = EnableRaytracing(adapter);

    if (!m_isDxrSupported)
    {
        OutputDebugString(
            L"Could not enable raytracing driver (D3D12EnableExperimentalFeatures() failed).\n" \
            L"Possible reasons:\n" \
            L"  1) your OS is not in developer mode.\n" \
            L"  2) your GPU driver doesn't match the D3D12 runtime loaded by the app (d3d12.dll and friends).\n" \
            L"  3) your D3D12 runtime doesn't match the D3D12 headers used by your app (in particular, the GUID passed to D3D12EnableExperimentalFeatures).\n\n");

        OutputDebugString(L"Enabling compute based fallback raytracing support.\n");
        ThrowIfFalse(EnableComputeRaytracingFallback(adapter), L"Could not enable compute based fallback raytracing support (D3D12EnableExperimentalFeatures() failed).\n");
		m_raytracingAPI = RaytracingAPI::FallbackLayer;
    }
}

void VaporPlus::OnInit()
{
    m_deviceResources = std::make_unique<DX::DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        FrameCount,
        D3D_FEATURE_LEVEL_11_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the Fallback Layer requires Fall Creator's update (RS3), we don't need to handle non-tearing cases.
		DX::DeviceResources::c_RequireTearingSupport,
        m_adapterIDoverride
        );
    m_deviceResources->RegisterDeviceNotify(this);
    m_deviceResources->SetWindow(Win32Application::GetHwnd(), m_width, m_height);
    m_deviceResources->InitializeDXGIAdapter();
    EnableDXRExperimentalFeatures(m_deviceResources->GetAdapter());

    m_deviceResources->CreateDeviceResources();
    m_deviceResources->CreateWindowSizeDependentResources();

    InitializeScene();

    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

// Update camera matrices passed into the shader.
void VaporPlus::UpdateCameraMatrices()
{
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

    m_sceneCB[frameIndex].cameraPosition = m_eye;
    float fovAngleY = 45.0f;
    XMMATRIX view = XMMatrixLookAtLH(m_eye, m_at, m_up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovAngleY), m_aspectRatio, 1.0f, 125.0f);
    XMMATRIX viewProj = view * proj;

    m_sceneCB[frameIndex].projectionToWorld = XMMatrixInverse(nullptr, viewProj);

	m_sceneCB[frameIndex].perGeometryTransform[0] = m_floor.GetTransform();
	m_sceneCB[frameIndex].perGeometryTransform[1] = m_helios.GetTransform();
	m_sceneCB[frameIndex].perGeometryTransform[2] = m_cityscape.GetTransform();
	m_sceneCB[frameIndex].perGeometryTransform[3] = m_text.GetTransform();

	static const float floorAnimationXIncrement = 0.01f / 8.0f;
	static const float floorAnimationYIncrement = 0.01f;

	m_sceneCB[frameIndex].floorUVDisp.x = m_floorTextureOffsetX;
	m_sceneCB[frameIndex].floorUVDisp.y = m_floorTextureOffsetY;

	m_floorTextureOffsetX += floorAnimationXIncrement;
	m_floorTextureOffsetY += floorAnimationYIncrement;

	if (m_floorTextureOffsetX > 1000)
		m_floorTextureOffsetX = 0;

	if (m_floorTextureOffsetY > 1000)
		m_floorTextureOffsetY = 0;
}

// Initialize scene rendering parameters.
void VaporPlus::InitializeScene()
{
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

    // Setup materials.
    {
		m_perGeometryConstantBuffer.albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Setup camera.
    {
        // Initialize the view and projection inverse matrices.
		float viewerHeight = 0.0f;
        m_eye = { 0.0f, viewerHeight, -5.0f, 1.0f };
        m_at = { 0.0f, 0.0f, 0.0f, 1.0f };
        XMVECTOR right = { 1.0f, 0.0f, 0.0f, 0.0f };

        XMVECTOR direction = XMVector4Normalize(m_at - m_eye);
        m_up = XMVector3Normalize(XMVector3Cross(direction, right));

        // Rotate camera around Y axis.
        XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(0.0f));
        m_eye = XMVector3Transform(m_eye, rotate);
        m_up = XMVector3Transform(m_up, rotate);
        
        UpdateCameraMatrices();
    }

    // Setup lights.
    {
        // Initialize the lighting parameters.
        XMFLOAT4 lightPosition;
        XMFLOAT4 lightAmbientColor;
        XMFLOAT4 lightDiffuseColor;

        lightPosition = XMFLOAT4(-5.0f, 24.8f, -26.0f, 0.0f);
        m_sceneCB[frameIndex].lightPosition = XMLoadFloat4(&lightPosition);

        lightAmbientColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
        m_sceneCB[frameIndex].lightAmbientColor = XMLoadFloat4(&lightAmbientColor);

        lightDiffuseColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
        m_sceneCB[frameIndex].lightDiffuseColor = XMLoadFloat4(&lightDiffuseColor);
    }

    // Apply the initial values to all frames' buffer instances.
    for (auto& sceneCB : m_sceneCB)
    {
        sceneCB = m_sceneCB[frameIndex];
    }
}

// Create constant buffers.
void VaporPlus::CreateConstantBuffers()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto frameCount = m_deviceResources->GetBackBufferCount();
    
    // Create the constant buffer memory and map the CPU and GPU addresses
    const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    // Allocate one constant buffer per frame, since it gets updated every frame.
    size_t cbSize = frameCount * sizeof(AlignedSceneConstantBuffer);
    const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &constantBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_perFrameConstants)));

    // Map the constant buffer and cache its heap pointers.
    // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(m_perFrameConstants->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData)));
}

// Create resources that depend on the device.
void VaporPlus::CreateDeviceDependentResources()
{
    // Initialize raytracing pipeline.

    // Create raytracing interfaces: raytracing device and commandlist.
    CreateRaytracingInterfaces();

    // Create root signatures for the shaders.
    CreateRootSignatures();

    // Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
    CreateRaytracingPipelineStateObject();

	CreatePostprocessPipelineState();
	CreatePostprocessResources();

	m_floor.Initialize(TextureID_Checkerboard, CHECKERBOARD_FLOOR_MATERIAL);
	m_helios.Initialize(TextureID_None, STATUE_MATERIAL);
	m_cityscape.Initialize(TextureID_Cityscape, CITYSCAPE_MATERIAL);
	m_cityscape.SetFloatAnimationCounter(400);

	m_text.Initialize(TextureID_Text, TEXT_MATERIAL);
	m_text.SetFloatAnimationCounter(200);

    // Create a heap for descriptors.
    CreateDescriptorHeaps();

	m_objLoader.Load(L"helios.obj");

	LoadTextures();

    // Build geometry to be used in the sample.
    BuildGeometry();

    // Build raytracing acceleration structures from the generated geometry.
    BuildAccelerationStructures();

    // Create constant buffers for the geometry and the scene.
    CreateConstantBuffers();

    // Build shader tables, which define shaders and their local root arguments.
    BuildShaderTables();

    // Create an output 2D texture to store the raytracing result to.
    CreateRaytracingOutputResource();

	// Create a texture sampler.
	CreateSampler();
}

void VaporPlus::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
{
    auto device = m_deviceResources->GetD3DDevice();
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        ThrowIfFailed(m_fallbackDevice->D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
        ThrowIfFailed(m_fallbackDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
    }
    else // DirectX Raytracing
    {
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
        ThrowIfFailed(device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
    }
}

void VaporPlus::CreateRootSignatures()
{
    auto device = m_deviceResources->GetD3DDevice();

    // Global Root Signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    {
        CD3DX12_DESCRIPTOR_RANGE ranges[6]; // Perfomance TIP: Order from most frequent to least frequent.
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // 1 output texture
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);  // 2 static index and vertex buffers.
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // a static input texture
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0); // a sampler
		ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);  // a static input texture
		ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);  // a static input texture

        CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
        rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
        rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
        rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);
        rootParameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable(1, &ranges[1]);
		rootParameters[GlobalRootSignatureParams::CheckerboardTextureSlot].InitAsDescriptorTable(1, &ranges[2]);
		rootParameters[GlobalRootSignatureParams::SamplerSlot].InitAsDescriptorTable(1, &ranges[3]);
		rootParameters[GlobalRootSignatureParams::CityscapeTextureSlot].InitAsDescriptorTable(1, &ranges[4]);
		rootParameters[GlobalRootSignatureParams::TextTextureSlot].InitAsDescriptorTable(1, &ranges[5]);
        CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);

        SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
    }

    // Local Root Signature
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    {
        CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
        rootParameters[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_perGeometryConstantBuffer), 1);
        CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
        localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
    }
#if USE_NON_NULL_LOCAL_ROOT_SIG 
    // Empty local root signature
    {
        CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(D3D12_DEFAULT);
        localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignatureEmpty);
    }
#endif

	{
		CD3DX12_DESCRIPTOR_RANGE ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

		CD3DX12_ROOT_PARAMETER rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstants(2, 0);

		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		rootSignatureDesc.NumStaticSamplers = 1;
		rootSignatureDesc.pStaticSamplers = &sampler;

		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		SerializeAndCreateRaytracingRootSignature(rootSignatureDesc, &m_postprocessRootSignature);
	}
}

// Create raytracing device and command list.
void VaporPlus::CreateRaytracingInterfaces()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();

    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        CreateRaytracingFallbackDeviceFlags createDeviceFlags = m_forceComputeFallback ? 
                                                    CreateRaytracingFallbackDeviceFlags::ForceComputeFallback : 
                                                    CreateRaytracingFallbackDeviceFlags::None;
        ThrowIfFailed(D3D12CreateRaytracingFallbackDevice(device, createDeviceFlags, 0, IID_PPV_ARGS(&m_fallbackDevice)));
        m_fallbackDevice->QueryRaytracingCommandList(commandList, IID_PPV_ARGS(&m_fallbackCommandList));
    }
	else // DirectX Raytracing
	{
		ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
		ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
	}
}

// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void VaporPlus::CreateLocalRootSignatureSubobjects(CD3D12_STATE_OBJECT_DESC* raytracingPipeline)
{
    // Local root signature to be used in a hit group.
    auto localRootSignature = raytracingPipeline->CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
    // Define explicit shader association for the local root signature. 
    // In this sample, this could be ommited for convenience since it matches the default association.
    {
        auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        rootSignatureAssociation->AddExport(c_hitGroupName);
    }

#if USE_NON_NULL_LOCAL_ROOT_SIG 
    // Empty local root signature to be used in a ray gen and a miss shader.
    {
        auto localRootSignature = raytracingPipeline->CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        localRootSignature->SetRootSignature(m_raytracingLocalRootSignatureEmpty.Get());
        // Shader association
        auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        rootSignatureAssociation->AddExport(c_raygenShaderName);
        rootSignatureAssociation->AddExport(c_missShaderName);
		rootSignatureAssociation->AddExport(c_missShaderName_Shadow);
		rootSignatureAssociation->AddExport(c_missShaderName_Reflection);
    }
#endif
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a DispatchRays() call,
// with all configuration options resolved, such as local signatures and other state.
void VaporPlus::CreateRaytracingPipelineStateObject()
{
    // Create 7 subobjects that combine into a RTPSO:
    // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
    // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
    // This simple sample utilizes default shader association except for local root signature subobject
    // which has an explicit association specified purely for demonstration purposes.
    // 1 - DXIL library
    // 1 - Triangle hit group
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config
    CD3D12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };


    // DXIL library
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
    auto lib = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void *)g_pRaytracing, ARRAYSIZE(g_pRaytracing));
    lib->SetDXILLibrary(&libdxil);
    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
    // In this sample, this could be ommited for convenience since the sample uses all shaders in the library. 
    {
        lib->DefineExport(c_raygenShaderName);
        lib->DefineExport(c_closestHitShaderName);
        lib->DefineExport(c_missShaderName);
		lib->DefineExport(c_missShaderName_Shadow);
		lib->DefineExport(c_missShaderName_Reflection);
    }
    
    // Triangle hit group
    // A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB.
    // In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
    auto hitGroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetClosestHitShaderImport(c_closestHitShaderName);
    hitGroup->SetHitGroupExport(c_hitGroupName);
    
    // Shader config
    // Defines the maximum sizes in bytes for the ray payload and attribute structure.
    auto shaderConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payloadSize = sizeof(XMFLOAT4);    // float4 pixelColor
	payloadSize += sizeof(uint32_t);		// recursionDepth
    UINT attributeSize = sizeof(XMFLOAT2);  // float2 barycentrics
    shaderConfig->Config(payloadSize, attributeSize);

    // Local root signature and shader association
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    CreateLocalRootSignatureSubobjects(&raytracingPipeline);

    // Global root signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3D12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

    // Pipeline config
    // Defines the maximum TraceRay() recursion depth.
    auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();

    // PERFOMANCE TIP: Set max recursion depth as low as needed 
    // as drivers may apply optimization strategies for low recursion depths.
    UINT maxRecursionDepth = 2;
    pipelineConfig->Config(maxRecursionDepth);

#if _DEBUG
    PrintStateObjectDesc(raytracingPipeline);
#endif

    // Create the state object.
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        ThrowIfFailed(m_fallbackDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_fallbackStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
    }
    else // DirectX Raytracing
    {
        ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
    }
}

void VaporPlus::CreatePostprocessPipelineState()
{
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;
	
	UINT compileFlags = 0;
	
	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = m_postprocessRootSignature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE((void *)g_pPostprocessVS, ARRAYSIZE(g_pPostprocessVS));
	psoDesc.PS = CD3DX12_SHADER_BYTECODE((void *)g_pPostprocessPS, ARRAYSIZE(g_pPostprocessPS));
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	auto device = m_deviceResources->GetD3DDevice();
	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_postprocessPipelineState)));
}

void VaporPlus::CreatePostprocessResources()
{
	auto device = m_deviceResources->GetD3DDevice();

	{
		struct PostprocessVertex
		{
			XMFLOAT3 position;
			XMFLOAT2 uv;
		};

		// Define the geometry for a triangle.
		PostprocessVertex triangleVertices[] =
		{
			{ { -1, 1, 0.0f },{ 0.0f, 0.0f } }, // Top left
			{ { 1, 1, 0.0f },{ 1.0f, 0.0f } }, // Top right
			{ { -1, -1, 0.0f },{ 0.0f, 1.0f } }, // Bottom left
			
			{ { -1, -1, 0.0f },{ 0.0f, 1.0f } }, // Bottom left
			{ { 1, 1, 0.0f },{ 1.0f, 0.0f } }, // Top right
			{ { 1, -1, 0.0f },{ 1.0f, 1.0f } } // Bottom right
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);

		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_postprocessVertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_postprocessVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		m_postprocessVertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_postprocessVertexBufferView.BufferLocation = m_postprocessVertexBuffer->GetGPUVirtualAddress();
		m_postprocessVertexBufferView.StrideInBytes = sizeof(PostprocessVertex);
		m_postprocessVertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Descriptors:
	//	 - Raytracing output
	//   - TV noise
	m_postprocessSRVHeap.Initialize(device, 2);
}

// Create 2D output texture for raytracing.
void VaporPlus::CreateRaytracingOutputResource()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

    // Create the output resource. The dimensions and format should match the swap-chain.
    auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(backbufferFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput)));
    NAME_D3D12_OBJECT(m_raytracingOutput);

	{
		// Allocate a descriptor to be written by the raytracing operation
		D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
		m_raytracingOutputResourceUAVDescriptorHeapIndexDuringRaytracing = m_raytracingDescriptorHeap.AllocateDescriptor(&uavDescriptorHandle, m_raytracingOutputResourceUAVDescriptorHeapIndexDuringRaytracing);

		D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
	}
    m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_raytracingDescriptorHeap.GetGPUDescriptorHandleForHeapStart(), m_raytracingOutputResourceUAVDescriptorHeapIndexDuringRaytracing, m_descriptorSize);
	
	m_raytracingOutputResourceUAVDescriptorHeapIndexDuringPostprocess = m_postprocessSRVHeap.CreateTextureUAV(m_raytracingOutput.Get());
}

void VaporPlus::CreateSampler()
{
	D3D12_CPU_DESCRIPTOR_HANDLE samplerDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;

	auto device = m_deviceResources->GetD3DDevice();
	device->CreateSampler(&sampler, samplerDescriptorHandle);
	m_samplerDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_samplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

}

void VaporPlus::CreateDescriptorHeaps()
{
    auto device = m_deviceResources->GetD3DDevice();

	// Allocate a heap with:
	// 2 - vertex and index buffer SRVs
	// 1 - raytracing output texture SRV
	// 2 - bottom and top level acceleration structure fallback wrapped pointer UAVs
	// Then five more for textures
	m_raytracingDescriptorHeap.Initialize(device, 10);

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	{
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
		descriptorHeapDesc.NumDescriptors = 1;
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		descriptorHeapDesc.NodeMask = 0;
		device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_samplerDescriptorHeap));
		NAME_D3D12_OBJECT(m_samplerDescriptorHeap);
	}
}

// Build geometry used in the sample.
void VaporPlus::BuildGeometry()
{
	std::vector<Vertex> allVertices;
	std::vector<Index> indices;

	{
		XMVECTOR xAxis = { 1, 0, 0 };
		XMVECTOR yAxis = { 0, 1, 0 };
		XMVECTOR zAxis = { 0, 0, 1 };
		XMMATRIX transform = XMMatrixRotationAxis(xAxis, 3.14159f / 2.0f) * XMMatrixRotationAxis(yAxis, 3.14159f / 12.0f) * XMMatrixRotationAxis(zAxis, 3.14159f) * XMMatrixTranslation(-1.5f, 0, 0);
		m_helios.LoadObjMesh(
			"Plane001", 
			0.007f, 
			&m_objLoader, 
			m_deviceResources.get(), 
			m_descriptorSize, 
			transform, 
			&allVertices, 
			&indices);
	}

	{
		float floorSize = 30.0f;
		float lowered = -4.0f;
		m_floor.LoadCube(
			&m_objLoader,
			50.0f, 0.1f, floorSize,
			0, lowered, 30,
			10.0f,
			m_deviceResources.get(),
			m_descriptorSize,
			&allVertices,
			&indices);
	}
	{
		float aspect = 1.354f;
		m_cityscape.LoadCube(
			&m_objLoader,
			aspect, 1, 0.1f,
			1.5f, -0.5, 3,
			1.0f,
			m_deviceResources.get(),
			m_descriptorSize,
			&allVertices,
			&indices);
	}
	{
		float aspect = 1.911f;
		m_text.LoadCube(
			&m_objLoader,
			aspect, 1, 0.1f,
			1.5f, 2.0f, 3,
			1.0f,
			m_deviceResources.get(),
			m_descriptorSize,
			&allVertices,
			&indices);
	}

	size_t indexBufferSize = indices.size() * sizeof(Index);
	
	auto device = m_deviceResources->GetD3DDevice();
	AllocateUploadBuffer(device, indices.data(), indexBufferSize, &m_indexBuffer.resource);
	AllocateUploadBuffer(device, allVertices.data(), allVertices.size() * sizeof(allVertices[0]), &m_vertexBuffer.resource);

	// Vertex buffer is passed to the shader along with index buffer as a descriptor table.
	// Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
	UINT descriptorIndexIB = m_raytracingDescriptorHeap.CreateBufferSRV(&m_indexBuffer, CheckCastUint(indexBufferSize) / 4, 0);
	UINT descriptorIndexVB = m_raytracingDescriptorHeap.CreateBufferSRV(&m_vertexBuffer, CheckCastUint(allVertices.size()), sizeof(allVertices[0]));
	ThrowIfFalse(descriptorIndexVB == descriptorIndexIB + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor index!");
}
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO VaporPlus::CreateTopLevelPrebuild(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC prebuildInfoDesc = {};
	prebuildInfoDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	prebuildInfoDesc.Inputs.Flags = buildFlags;
	prebuildInfoDesc.Inputs.NumDescs = 1;
	prebuildInfoDesc.Inputs.pGeometryDescs = nullptr;
	prebuildInfoDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfoDesc.Inputs, &topLevelPrebuildInfo);
	}
	else // DirectX Raytracing
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfoDesc.Inputs, &topLevelPrebuildInfo);
	}
	ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	return topLevelPrebuildInfo;
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO VaporPlus::CreateBottomLevelPrebuild(
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags,
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> const& bottomLevelGeometries)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC prebuildInfoDesc = {};
	prebuildInfoDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	prebuildInfoDesc.Inputs.Flags = buildFlags;
	prebuildInfoDesc.Inputs.NumDescs = CheckCastUint(bottomLevelGeometries.size());
	prebuildInfoDesc.Inputs.pGeometryDescs = bottomLevelGeometries.data();
	prebuildInfoDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfoDesc.Inputs, &bottomLevelPrebuildInfo);
	}
	else // DirectX Raytracing
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfoDesc.Inputs, &bottomLevelPrebuildInfo);
	}
	ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	return bottomLevelPrebuildInfo;
}

void VaporPlus::UpdateBottomLevelAccelerationStructure()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	auto commandAllocator = m_deviceResources->GetCommandAllocator();

	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
	geometryDescs.push_back(m_floor.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));
	geometryDescs.push_back(m_helios.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));
	geometryDescs.push_back(m_cityscape.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));
	geometryDescs.push_back(m_text.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));

	// Get required sizes for an acceleration structure.
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS bottomLevelBuildFlags =
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
	bottomLevelBuildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	bottomLevelBuildDesc.Inputs.Flags = bottomLevelBuildFlags;
	bottomLevelBuildDesc.Inputs.NumDescs = CheckCastUint(geometryDescs.size());
	bottomLevelBuildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	bottomLevelBuildDesc.Inputs.pGeometryDescs = geometryDescs.data();

	{
		bottomLevelBuildDesc.ScratchAccelerationStructureData = m_accelerationStructureScratchResource->GetGPUVirtualAddress();
		bottomLevelBuildDesc.DestAccelerationStructureData = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();
	}

	auto BuildAccelerationStructure = [&](auto* raytracingCommandList)
	{
		raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_bottomLevelAccelerationStructure.Get()));
	};

	// Build acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		// Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
		ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_raytracingDescriptorHeap.GetResource() };
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		BuildAccelerationStructure(m_fallbackCommandList.Get());
	}
	else // DirectX Raytracing
	{
		BuildAccelerationStructure(m_dxrCommandList.Get());
	}
}

// Build acceleration structures needed for raytracing.
void VaporPlus::BuildAccelerationStructures()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();
    auto commandAllocator = m_deviceResources->GetCommandAllocator();

    // Reset the command list for the acceleration structure construction.
    commandList->Reset(commandAllocator, nullptr);

	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
	geometryDescs.push_back(m_floor.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));
	geometryDescs.push_back(m_helios.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));
	geometryDescs.push_back(m_cityscape.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));
	geometryDescs.push_back(m_text.GetRaytracingGeometryDesc(&m_vertexBuffer, &m_indexBuffer));

	// Get required sizes for an acceleration structure.
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS bottomLevelBuildFlags = 
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS topLevelbuildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &bottomLevelInputs = bottomLevelBuildDesc.Inputs;
	bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	bottomLevelInputs.Flags = bottomLevelBuildFlags;
	bottomLevelInputs.NumDescs = CheckCastUint(geometryDescs.size());
	bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	bottomLevelInputs.pGeometryDescs = geometryDescs.data();

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = bottomLevelBuildDesc;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &topLevelInputs = topLevelBuildDesc.Inputs;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = topLevelbuildFlags;
	topLevelInputs.NumDescs = 1;
	topLevelInputs.pGeometryDescs = nullptr;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}
	else // DirectX Raytracing
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}
	ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
	}
	else // DirectX Raytracing
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
	}
	ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	AllocateUAVBuffer(
		device, 
		max(topLevelPrebuildInfo.ScratchDataSizeInBytes, 
			bottomLevelPrebuildInfo.ScratchDataSizeInBytes), &m_accelerationStructureScratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

	// Allocate resources for acceleration structures.
	// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
	// Default heap is OK since the application doesn’t need CPU read/write access to them. 
	// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
	// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
	//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
	//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
	{
		D3D12_RESOURCE_STATES initialResourceState;
		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			initialResourceState = m_fallbackDevice->GetAccelerationStructureResourceState();
		}
		else // DirectX Raytracing
		{
			initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		}

		AllocateUAVBuffer(device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_bottomLevelAccelerationStructure, initialResourceState, L"BottomLevelAccelerationStructure");
		AllocateUAVBuffer(device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_topLevelAccelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
	}

	// Note on Emulated GPU pointers (AKA Wrapped pointers) requirement in Fallback Layer:
	// The primary point of divergence between the DXR API and the compute-based Fallback layer is the handling of GPU pointers. 
	// DXR fundamentally requires that GPUs be able to dynamically read from arbitrary addresses in GPU memory. 
	// The existing Direct Compute API today is more rigid than DXR and requires apps to explicitly inform the GPU what blocks of memory it will access with SRVs/UAVs.
	// In order to handle the requirements of DXR, the Fallback Layer uses the concept of Emulated GPU pointers, 
	// which requires apps to create views around all memory they will access for raytracing, 
	// but retains the DXR-like flexibility of only needing to bind the top level acceleration structure at DispatchRays.
	//
	// The Fallback Layer interface uses WRAPPED_GPU_POINTER to encapsulate the underlying pointer
	// which will either be an emulated GPU pointer for the compute - based path or a GPU_VIRTUAL_ADDRESS for the DXR path.

	// Create an instance desc for the bottom-level acceleration structure.
	ComPtr<ID3D12Resource> instanceDescs;
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC instanceDesc = {};
		instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
		instanceDesc.InstanceMask = 1;
		UINT numBufferElements = static_cast<UINT>(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes) / sizeof(UINT32);
		instanceDesc.AccelerationStructure = CreateFallbackWrappedPointer(m_bottomLevelAccelerationStructure.Get(), numBufferElements);
		AllocateUploadBuffer(device, &instanceDesc, sizeof(instanceDesc), &instanceDescs, L"InstanceDescs");
	}
	else // DirectX Raytracing
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
		instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
		instanceDesc.InstanceMask = 1;
		instanceDesc.AccelerationStructure = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();
		AllocateUploadBuffer(device, &instanceDesc, sizeof(instanceDesc), &instanceDescs, L"InstanceDescs");
	}

	// Create a wrapped pointer to the acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		UINT numBufferElements = static_cast<UINT>(topLevelPrebuildInfo.ResultDataMaxSizeInBytes) / sizeof(UINT32);
		m_fallbackTopLevelAccelerationStructurePointer = CreateFallbackWrappedPointer(m_topLevelAccelerationStructure.Get(), numBufferElements);
	}

	// Bottom Level Acceleration Structure desc
	{
		bottomLevelBuildDesc.ScratchAccelerationStructureData = m_accelerationStructureScratchResource->GetGPUVirtualAddress();
		bottomLevelBuildDesc.DestAccelerationStructureData = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();
	}

	// Top Level Acceleration Structure desc
	{
		topLevelBuildDesc.DestAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
		topLevelBuildDesc.ScratchAccelerationStructureData = m_accelerationStructureScratchResource->GetGPUVirtualAddress();
		topLevelBuildDesc.Inputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
	}

	auto BuildAccelerationStructure = [&](auto* raytracingCommandList)
	{
		raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_bottomLevelAccelerationStructure.Get()));
		raytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
	};

	// Build acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		// Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
		ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_raytracingDescriptorHeap.GetResource() };
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		BuildAccelerationStructure(m_fallbackCommandList.Get());
	}
	else // DirectX Raytracing
	{
		BuildAccelerationStructure(m_dxrCommandList.Get());
	}

	// Kick off acceleration structure construction.
	m_deviceResources->ExecuteCommandList();

	// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
	m_deviceResources->WaitForGpu();
}

// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
void VaporPlus::BuildShaderTables()
{
    auto device = m_deviceResources->GetD3DDevice();

    void* rayGenShaderIdentifier;
    void* missShaderIdentifier;
	void* missShaderIdentifier_Shadow;
	void* missShaderIdentifier_Reflection;
    void* hitGroupShaderIdentifier;

    auto GetShaderIdentifiers = [&](auto* stateObjectProperties)
    {
        rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
        missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
		missShaderIdentifier_Shadow = stateObjectProperties->GetShaderIdentifier(c_missShaderName_Shadow);
		missShaderIdentifier_Reflection = stateObjectProperties->GetShaderIdentifier(c_missShaderName_Reflection);
        hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_hitGroupName);
    };

    // Get shader identifiers.
    UINT shaderIdentifierSize;
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        GetShaderIdentifiers(m_fallbackStateObject.Get());
        shaderIdentifierSize = m_fallbackDevice->GetShaderIdentifierSize();
    }
    else // DirectX Raytracing
    {
        ComPtr<ID3D12StateObjectPropertiesPrototype> stateObjectProperties;
        ThrowIfFailed(m_dxrStateObject.As(&stateObjectProperties));
        GetShaderIdentifiers(stateObjectProperties.Get());
		shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    // Ray gen shader table
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;
        ShaderTable rayGenShaderTable(device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
        rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
        m_rayGenShaderTable = rayGenShaderTable.GetResource();
    }

    // Miss shader table
    {
		m_missShaderRecordCount = 3;
        UINT shaderRecordSize = shaderIdentifierSize;
        ShaderTable missShaderTable(device, m_missShaderRecordCount, shaderRecordSize, L"MissShaderTable");
        missShaderTable.push_back(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
		missShaderTable.push_back(ShaderRecord(missShaderIdentifier_Shadow, shaderIdentifierSize));
		missShaderTable.push_back(ShaderRecord(missShaderIdentifier_Reflection, shaderIdentifierSize));
        m_missShaderTable = missShaderTable.GetResource();
    }

    // Hit group shader table
    {
        struct HitGroupArgument
		{
			CubeConstantBuffer cb;
		};

		m_hitGroupShaderRecordCount = 8;
		uint32_t m_recordSize = shaderIdentifierSize + sizeof(HitGroupArgument);
        ShaderTable hitGroupShaderTable(device, m_hitGroupShaderRecordCount, m_recordSize, L"HitGroupShaderTable");
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_floor.GetMaterial();
			argument.cb.indexBufferOffset = m_floor.GetIndexBufferOffset();
			argument.cb.geometryID = 0;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_floor.GetMaterial();
			argument.cb.indexBufferOffset = m_floor.GetIndexBufferOffset();
			argument.cb.geometryID = 0;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_helios.GetMaterial();
			argument.cb.indexBufferOffset = m_helios.GetIndexBufferOffset();
			argument.cb.geometryID = 1;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_helios.GetMaterial();
			argument.cb.indexBufferOffset = m_helios.GetIndexBufferOffset();
			argument.cb.geometryID = 1;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_cityscape.GetMaterial();
			argument.cb.indexBufferOffset = m_cityscape.GetIndexBufferOffset();
			argument.cb.geometryID = 2;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_cityscape.GetMaterial();
			argument.cb.indexBufferOffset = m_cityscape.GetIndexBufferOffset();
			argument.cb.geometryID = 2;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_text.GetMaterial();
			argument.cb.indexBufferOffset = m_text.GetIndexBufferOffset();
			argument.cb.geometryID = 3;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
		{
			HitGroupArgument argument;
			argument.cb = m_perGeometryConstantBuffer;
			argument.cb.material = m_text.GetMaterial();
			argument.cb.indexBufferOffset = m_text.GetIndexBufferOffset();
			argument.cb.geometryID = 3;
			hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &argument, sizeof(argument)));
		}
        m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
    }
}

void VaporPlus::SelectRaytracingAPI(RaytracingAPI type)
{
    if (type == RaytracingAPI::FallbackLayer)
    {
        m_raytracingAPI = type;
    }
    else // DirectX Raytracing
    {
        if (m_isDxrSupported)
        {
            m_raytracingAPI = type;
        }
        else
        {
            OutputDebugString(L"Invalid selection - DXR is not available.\n");
        }
    }
}

void VaporPlus::OnKeyDown(UINT8 key)
{
    // Store previous values.
    RaytracingAPI previousRaytracingAPI = m_raytracingAPI;
    bool previousForceComputeFallback = m_forceComputeFallback;

    switch (key)
    {
    case VK_NUMPAD1:
    case '1': // Fallback Layer
        m_forceComputeFallback = false;
        SelectRaytracingAPI(RaytracingAPI::FallbackLayer);
        break;
    case VK_NUMPAD2:
    case '2': // Fallback Layer + force compute path
        m_forceComputeFallback = true;
        SelectRaytracingAPI(RaytracingAPI::FallbackLayer);
        break;
    case VK_NUMPAD3:
    case '3': // DirectX Raytracing
        SelectRaytracingAPI(RaytracingAPI::DirectXRaytracing);
        break;
	case 'A':
		m_helios.SetSpinEnabled(!m_helios.IsSpinEnabled());
		m_cityscape.SetSpinEnabled(!m_cityscape.IsSpinEnabled());
		m_text.SetSpinEnabled(!m_text.IsSpinEnabled());
		break;
	case 'W':
		m_enableTextFrame = !m_enableTextFrame;
		break;
	case 'P':
		m_enablePostprocess = !m_enablePostprocess;
		break;
	case 'M':
		{
			DWORD threadID;
			CreateThread(nullptr, 0, MusicThread, nullptr, 0, &threadID);
		}
		break;
    default:
        break;
    }
    
    if (m_raytracingAPI != previousRaytracingAPI ||
        m_forceComputeFallback != previousForceComputeFallback)
    {
        // Raytracing API selection changed, recreate everything.
        RecreateD3D();
    }
}

// Update frame-based values.
void VaporPlus::OnUpdate()
{
    m_timer.Tick();
    CalculateFrameStats();
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    auto prevFrameIndex = m_deviceResources->GetPreviousFrameIndex();

    {
        UpdateCameraMatrices();
    }

    // Rotate the second light around Y axis.
    {
        float secondsToRotateAround = 8.0f;
        float angleToRotateBy = -360.0f;
        XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(angleToRotateBy));
        const XMVECTOR& prevLightPosition = m_sceneCB[prevFrameIndex].lightPosition;
    }
}


// Parse supplied command line args.
void VaporPlus::ParseCommandLineArgs(WCHAR* argv[], int argc)
{
    DXSample::ParseCommandLineArgs(argv, argc);

    if (argc > 1)
    {
        if (_wcsnicmp(argv[1], L"-FL", wcslen(argv[1])) == 0 )
        {
            m_forceComputeFallback = true;
			m_raytracingAPI = RaytracingAPI::FallbackLayer;
        }
        else if (_wcsnicmp(argv[1], L"-DXR", wcslen(argv[1])) == 0)
        {
			m_raytracingAPI = RaytracingAPI::DirectXRaytracing;
        }
    }
}

void VaporPlus::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    
    auto DispatchRays = [&](auto* commandList, auto* stateObject, auto* dispatchDesc)
    {
        // Since each shader table has only one shader record, the stride is same as the size.
        dispatchDesc->HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
        dispatchDesc->HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
        dispatchDesc->HitGroupTable.StrideInBytes = m_hitGroupShaderTable->GetDesc().Width / m_hitGroupShaderRecordCount;
        dispatchDesc->MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
        dispatchDesc->MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
		dispatchDesc->MissShaderTable.StrideInBytes = m_missShaderTable->GetDesc().Width / m_missShaderRecordCount;
        dispatchDesc->RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
        dispatchDesc->RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;
        dispatchDesc->Width = m_width;
        dispatchDesc->Height = m_height;
		commandList->SetPipelineState1(stateObject);
		commandList->DispatchRays(dispatchDesc);
    };

    auto SetCommonPipelineState = [&](auto* descriptorSetCommandList)
    {
		ID3D12DescriptorHeap* descriptorHeaps[] = { m_raytracingDescriptorHeap.GetResource(), m_samplerDescriptorHeap.Get() };
        descriptorSetCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        // Set index and successive vertex buffer decriptor tables
        commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffersSlot, m_indexBuffer.gpuDescriptorHandle);
        commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, m_raytracingOutputResourceUAVGpuDescriptor);
		commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::CheckerboardTextureSlot, GetTextureInfo(TextureID_Checkerboard).ResourceDescriptor);
		commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::SamplerSlot, m_samplerDescriptor);
		commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::CityscapeTextureSlot, GetTextureInfo(TextureID_Cityscape).ResourceDescriptor);
		commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::TextTextureSlot, GetTextureInfo(TextureID_Text).ResourceDescriptor);
    };

    commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

    // Copy the updated scene constant buffer to GPU.
    memcpy(&m_mappedConstantData[frameIndex].constants, &m_sceneCB[frameIndex], sizeof(m_sceneCB[frameIndex]));
    auto cbGpuAddress = m_perFrameConstants->GetGPUVirtualAddress() + frameIndex * sizeof(m_mappedConstantData[0]);
    commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);
   
    // Bind the heaps, acceleration structure and dispatch rays.    
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        SetCommonPipelineState(m_fallbackCommandList.Get());
        m_fallbackCommandList->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, m_fallbackTopLevelAccelerationStructurePointer);
        DispatchRays(m_fallbackCommandList.Get(), m_fallbackStateObject.Get(), &dispatchDesc);
    }
    else // DirectX Raytracing
    {
        SetCommonPipelineState(commandList);
        commandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, m_topLevelAccelerationStructure->GetGPUVirtualAddress());
        DispatchRays(m_dxrCommandList.Get(), m_dxrStateObject.Get(), &dispatchDesc);
    }
}

// Update the application state with the new resolution.
void VaporPlus::UpdateForSizeChange(UINT width, UINT height)
{
    DXSample::UpdateForSizeChange(width, height);
}

void VaporPlus::DrawRaytracingOutputToTarget() 
{
	auto commandList = m_deviceResources->GetCommandList();
	auto renderTarget = m_deviceResources->GetRenderTarget();

	// Set necessary state.
	commandList->ClearState(m_postprocessPipelineState.Get());
	commandList->SetGraphicsRootSignature(m_postprocessRootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_postprocessSRVHeap.GetResource() };
	commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	D3D12_RESOURCE_BARRIER preDrawBarriers[1];
	preDrawBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(ARRAYSIZE(preDrawBarriers), preDrawBarriers);

	commandList->SetGraphicsRootDescriptorTable(0, m_postprocessSRVHeap.GetGPUDescriptorHandleForHeapStart());

	float timer = static_cast<float>(m_timer.GetTotalSeconds());
	UINT time = *(reinterpret_cast<UINT*>(&timer));
	UINT constants[2] = { m_enablePostprocess? 1u : 0u, time };
	commandList->SetGraphicsRoot32BitConstants(1, _countof(constants), &constants, 0);

	auto viewport = m_deviceResources->GetScreenViewport();
	commandList->RSSetViewports(1, &viewport);

	auto scissorRect = m_deviceResources->GetScissorRect();
	commandList->RSSetScissorRects(1, &scissorRect);

	// deviceResources->Prepare should have already put the render target into D3D12_RESOURCE_STATE_RENDER_TARGET.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_deviceResources->GetRenderTargetView();
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &m_postprocessVertexBufferView);

	UINT triangleCount = 2;
	UINT vertexCount = triangleCount * 3;
	commandList->DrawInstanced(vertexCount, 1, 0, 0);

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	
	D3D12_RESOURCE_BARRIER postDrawBarriers[1];
	postDrawBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	commandList->ResourceBarrier(ARRAYSIZE(postDrawBarriers), postDrawBarriers);
}

// Create resources that are dependent on the size of the main window.
void VaporPlus::CreateWindowSizeDependentResources()
{
    CreateRaytracingOutputResource();
    UpdateCameraMatrices();
}

// Release resources that are dependent on the size of the main window.
void VaporPlus::ReleaseWindowSizeDependentResources()
{
    m_raytracingOutput.Reset();
}

// Release all resources that depend on the device.
void VaporPlus::ReleaseDeviceDependentResources()
{
    m_fallbackDevice.Reset();
    m_fallbackCommandList.Reset();
    m_fallbackStateObject.Reset();
    m_raytracingGlobalRootSignature.Reset();
    m_raytracingLocalRootSignature.Reset();
#if USE_NON_NULL_LOCAL_ROOT_SIG 
    m_raytracingLocalRootSignatureEmpty.Reset();
#endif
	    
    m_dxrDevice.Reset();
    m_dxrCommandList.Reset();
    m_dxrStateObject.Reset();

	m_postprocessPipelineState.Reset();
	m_postprocessRootSignature.Reset();
	m_postprocessVertexBuffer.Reset();

	m_raytracingDescriptorHeap.Reset();
    m_raytracingOutputResourceUAVDescriptorHeapIndexDuringRaytracing = UINT_MAX;
	m_indexBuffer.resource.Reset();
	m_vertexBuffer.resource.Reset();
    m_perFrameConstants.Reset();
    m_rayGenShaderTable.Reset();
    m_missShaderTable.Reset();
    m_hitGroupShaderTable.Reset();

    m_bottomLevelAccelerationStructure.Reset();
    m_topLevelAccelerationStructure.Reset();
}

void VaporPlus::RecreateD3D()
{
    // Give GPU a chance to finish its execution in progress.
    try
    {
        m_deviceResources->WaitForGpu();
    }
    catch (HrException&)
    {
        // Do nothing, currently attached adapter is unresponsive.
    }
    m_deviceResources->HandleDeviceLost();
}

void VaporPlus::UpdateAnimation()
{
	// Check if it's time to animate
	LARGE_INTEGER performanceCounter;
	QueryPerformanceCounter(&performanceCounter);

	LONGLONG ticksSinceUpdate = performanceCounter.QuadPart - m_performanceCounter.QuadPart;
	LONGLONG millisecondsSinceUpdate = ticksSinceUpdate * 1000 / m_performanceFrequency.QuadPart;
	if (millisecondsSinceUpdate < 15)
		return;

	UpdateBottomLevelAccelerationStructure();

	m_helios.UpdateFloatyTransform(m_deviceResources.get());
	m_cityscape.UpdateFloatyTransform(m_deviceResources.get());
	m_text.UpdateFloatyTransform(m_deviceResources.get());

	m_performanceCounter = performanceCounter;
}

// Render the scene.
void VaporPlus::OnRender()
{
    if (!m_deviceResources->IsWindowVisible())
    {
        return;
    }

    m_deviceResources->Prepare();

	Draw2DTextToTexture(GetTextureInfo(TextureID_Text));

	UpdateAnimation();
    
	DoRaytracing();
	DrawRaytracingOutputToTarget();

    m_deviceResources->Present(D3D12_RESOURCE_STATE_PRESENT);
}

void VaporPlus::OnDestroy()
{
    // Let GPU finish before releasing D3D resources.
    m_deviceResources->WaitForGpu();
    OnDeviceLost();
}

// Release all device dependent resouces when a device is lost.
void VaporPlus::OnDeviceLost()
{
    ReleaseWindowSizeDependentResources();
    ReleaseDeviceDependentResources();
}

// Create all device dependent resources when a device is restored.
void VaporPlus::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

// Compute the average frames per second and million rays per second.
void VaporPlus::CalculateFrameStats()
{
    static int frameCnt = 0;
    static double elapsedTime = 0.0f;
    double totalTime = m_timer.GetTotalSeconds();
    frameCnt++;

    // Compute averages over one second period.
    if ((totalTime - elapsedTime) >= 1.0f)
    {
        float diff = static_cast<float>(totalTime - elapsedTime);
        float fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

        frameCnt = 0;
        elapsedTime = totalTime;

        float MRaysPerSecond = (m_width * m_height * fps) / static_cast<float>(1e6);

        std::wstringstream windowText;

        if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
        {
            if (m_fallbackDevice->UsingRaytracingDriver())
            {
                windowText << L"(FL-DXR)";
            }
            else
            {
                windowText << L"(FL)";
            }
        }
        else
        {
            windowText << L"(DXR)";
        }
        windowText << std::setprecision(2) << std::fixed
            << L" FPS: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
			<< "\n"
            << L" GPU[" << m_deviceResources->GetAdapterID() << L"]: " << m_deviceResources->GetAdapterDescription();
        
		m_frameStatsText = windowText.str();
    }
}

// Handle OnSizeChanged message event.
void VaporPlus::OnSizeChanged(UINT width, UINT height, bool minimized)
{
    if (!m_deviceResources->WindowSizeChanged(width, height, minimized))
    {
        return;
    }

    UpdateForSizeChange(width, height);

    ReleaseWindowSizeDependentResources();
    CreateWindowSizeDependentResources();
}

// Create a wrapped pointer for the Fallback Layer path.
WRAPPED_GPU_POINTER VaporPlus::CreateFallbackWrappedPointer(ID3D12Resource* resource, UINT bufferNumElements)
{
    auto device = m_deviceResources->GetD3DDevice();

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawBufferUavDesc = {};
    rawBufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawBufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    rawBufferUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawBufferUavDesc.Buffer.NumElements = bufferNumElements;

    D3D12_CPU_DESCRIPTOR_HANDLE bottomLevelDescriptor;
   
    // Only compute fallback requires a valid descriptor index when creating a wrapped pointer.
    UINT descriptorHeapIndex = 0;
    if (!m_fallbackDevice->UsingRaytracingDriver())
    {
        descriptorHeapIndex = m_raytracingDescriptorHeap.AllocateDescriptor(&bottomLevelDescriptor);
        device->CreateUnorderedAccessView(resource, nullptr, &rawBufferUavDesc, bottomLevelDescriptor);
    }
    return m_fallbackDevice->GetWrappedPointerSimple(descriptorHeapIndex, resource->GetGPUVirtualAddress());
}

void VaporPlus::DescriptorHeapWrapper::Initialize(ID3D12Device* device, UINT descriptorCount)
{
	m_device = device;
	m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	descriptorHeapDesc.NumDescriptors = descriptorCount;

	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptorHeapDesc.NodeMask = 0;
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_descriptorHeap));
	NAME_D3D12_OBJECT(m_descriptorHeap);
}

UINT VaporPlus::DescriptorHeapWrapper::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
	auto descriptorHeapCpuBase = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	if (descriptorIndexToUse >= m_descriptorHeap->GetDesc().NumDescriptors)
	{
		descriptorIndexToUse = m_descriptorsAllocated++;
	}
	*cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, m_descriptorSize);
	return descriptorIndexToUse;
}

// Create SRV for a buffer.
UINT VaporPlus::DescriptorHeapWrapper::CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize)
{
	// SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = numElements;
	if (elementSize == 0)
	{
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		srvDesc.Buffer.StructureByteStride = 0;
	}
	else
	{
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.StructureByteStride = elementSize;
	}
	UINT descriptorIndex = AllocateDescriptor(&buffer->cpuDescriptorHandle);
	m_device->CreateShaderResourceView(buffer->resource.Get(), &srvDesc, buffer->cpuDescriptorHandle);
	buffer->gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_descriptorSize);
	return descriptorIndex;
};

UINT VaporPlus::DescriptorHeapWrapper::CreateTextureUAV(ID3D12Resource* resource, UINT descriptorIndexToUse)
{
	// Allocate a descriptor to be read by the postprocess
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleUnused;
	UINT descriptorIndex = AllocateDescriptor(&cpuHandleUnused, descriptorIndexToUse);
	m_device->CreateUnorderedAccessView(resource, nullptr, &UAVDesc, m_descriptorHeap->GetCPUDescriptorHandleForHeapStart());
	return descriptorIndex;
}

void VaporPlus::DescriptorHeapWrapper::Reset()
{
	m_descriptorHeap.Reset();
	m_descriptorsAllocated = 0;
}

void VaporPlus::LoadTextures()
{
	CoInitialize(NULL);

	ThrowIfFailed(CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&m_wicImagingFactory)));

	uint32_t d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef DEBUG
	d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	IUnknown* commandQueues[] = { m_deviceResources->GetCommandQueue() };
	ThrowIfFailed(D3D11On12CreateDevice(
		m_deviceResources->GetD3DDevice(),
		d3d11DeviceFlags,
		nullptr,
		0,
		commandQueues,
		1,
		0,
		&m_device11,
		&m_deviceContext11,
		nullptr));

	ThrowIfFailed(m_device11.As(&m_device11on12));

	ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), &m_dwriteFactory));
	ThrowIfFailed(m_dwriteFactory->CreateTextFormat(
		L"Arial",
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_ULTRA_CONDENSED,
		24.0f,
		L"en-us",
		&m_topTextFormat));

	ThrowIfFailed(m_dwriteFactory->CreateTextFormat(
		L"Arial",
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_ULTRA_CONDENSED,
		16.0f,
		L"en-us",
		&m_bottomTextFormat));

	ThrowIfFailed(m_dwriteFactory->CreateTextFormat(
		L"Arial",
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		10.0f,
		L"en-us",
		&m_statsTextFormat));

	std::wstring topText = L"D X R プ ラ ス";
	ThrowIfFailed(m_dwriteFactory->CreateTextLayout(topText.c_str(), CheckCastUint(topText.length()), m_topTextFormat.Get(), 1000, 1000, &m_topTextLayout));

	std::wstring bottomText = L"レ イ ト レ ー シ ン グ の 基 本";
	ThrowIfFailed(m_dwriteFactory->CreateTextLayout(bottomText.c_str(), CheckCastUint(bottomText.length()), m_bottomTextFormat.Get(), 1000, 1000, &m_bottomTextLayout));

	D2D1_FACTORY_OPTIONS d2dFactoryOptions{};
	D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
	ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &d2dFactoryOptions, &m_d2dFactory));
	ComPtr<IDXGIDevice> dxgiDevice;
	ThrowIfFailed(m_device11.As(&dxgiDevice));

	ThrowIfFailed(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice));
	ThrowIfFailed(m_d2dDevice->CreateDeviceContext(deviceOptions, &m_d2dDeviceContext));
	ThrowIfFailed(m_d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.44f, 0.99f, 0.73f, 1.0f), &m_cyanColorBrush));
	
	m_allTextures.push_back(LoadImageTextureAsset(TextureID_Checkerboard, L"checker.png", &m_raytracingDescriptorHeap));
	m_allTextures.push_back(LoadImageTextureAsset(TextureID_Cityscape, L"Cityscape.png", &m_raytracingDescriptorHeap));
	m_allTextures.push_back(Create2DTargetTextureAsset(TextureID_Text));
	m_allTextures.push_back(LoadImageTextureAsset(TextureID_TVNoise, L"TVNoise.png", &m_postprocessSRVHeap, 1));
}

void VaporPlus::Draw2DTextToTexture(TextureInfo const& textTexture)
{
	m_deviceResources->GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		textTexture.Resource.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	static bool flag = true;

	flag = !flag;

	ID3D11Resource* wrappedResources[] = { textTexture.Texture11.Get() };
	m_device11on12->AcquireWrappedResources(wrappedResources, ARRAYSIZE(wrappedResources));

	m_d2dDeviceContext->SetTarget(GetTextureInfo(TextureID_Text).Texture2DTarget.Get());
	m_d2dDeviceContext->BeginDraw();
	m_d2dDeviceContext->Clear(D2D1::ColorF(1.0f, 0.51f, 0.61f, 1.0f));
	m_d2dDeviceContext->DrawTextLayout(D2D1::Point2F(13, 37), m_topTextLayout.Get(), m_cyanColorBrush.Get());
	m_d2dDeviceContext->DrawLine(D2D1::Point2F(0, 74), D2D1::Point2F(237, 74), m_cyanColorBrush.Get(), 10.0f);
	m_d2dDeviceContext->DrawTextLayout(D2D1::Point2F(13, 80), m_bottomTextLayout.Get(), m_cyanColorBrush.Get());

	auto renderTargetSize = m_d2dDeviceContext->GetSize();
	m_d2dDeviceContext->DrawTextW(
		m_frameStatsText.c_str(), 
		CheckCastUint(m_frameStatsText.size()), 
		m_statsTextFormat.Get(), 
		D2D1::RectF(5, 100, renderTargetSize.width, 500), 
		m_cyanColorBrush.Get());
	
	if (m_enableTextFrame)
	{
		float margin = 4.0f;
		m_d2dDeviceContext->DrawRectangle(D2D1::RectF(margin, margin, renderTargetSize.width - margin, renderTargetSize.height - margin), m_cyanColorBrush.Get(), 2.0f);
	}

	ThrowIfFailed(m_d2dDeviceContext->EndDraw());

	m_d2dDeviceContext->SetTarget(nullptr);

	m_device11on12->ReleaseWrappedResources(wrappedResources, ARRAYSIZE(wrappedResources));

	m_deviceContext11->Flush();
}

VaporPlus::TextureInfo VaporPlus::Create2DTargetTextureAsset(
	TextureIdentifier textureID)
{
	TextureInfo textureInfo{};
	textureInfo.TextureID = textureID;
	textureInfo.Filename = L"{no file}";
	
	auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_B8G8R8A8_UNORM, 400, 130, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateCommittedResource(
		&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&textureInfo.Resource)));
	NAME_D3D12_OBJECT(textureInfo.Resource);
	
	D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET };
	ThrowIfFailed(m_device11on12->CreateWrappedResource(
		textureInfo.Resource.Get(),
		&d3d11Flags,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		IID_PPV_ARGS(&textureInfo.Texture11)));

	ComPtr<IDXGISurface> textDxgiSurface;
	ThrowIfFailed(textureInfo.Texture11.As(&textDxgiSurface));

	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
	ThrowIfFailed(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(textDxgiSurface.Get(), bitmapProperties, textureInfo.Texture2DTarget.GetAddressOf()));

	// Describe and create a SRV for the texture.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	auto device = m_deviceResources->GetD3DDevice();

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptorHandle;
	UINT descriptorIndex = m_raytracingDescriptorHeap.AllocateDescriptor(&srvDescriptorHandle);
	device->CreateShaderResourceView(textureInfo.Resource.Get(), &srvDesc, srvDescriptorHandle);
	textureInfo.ResourceDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_raytracingDescriptorHeap.GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_descriptorSize);

	return textureInfo;
}

VaporPlus::TextureInfo VaporPlus::LoadImageTextureAsset(
	TextureIdentifier textureID,
	wchar_t const* filename,
	DescriptorHeapWrapper* srvDescriptorHeap,
	UINT descriptorIndexToUse)
{
	TextureInfo textureInfo{};
	textureInfo.TextureID = textureID;
	textureInfo.Filename = filename;
	
	ComPtr<IWICBitmapDecoder> decoder;
	ThrowIfFailed(m_wicImagingFactory->CreateDecoderFromFilename(
		filename,
		NULL,
		GENERIC_READ,
		WICDecodeMetadataCacheOnLoad, &decoder));

	// Create the initial frame.
	CComPtr<IWICBitmapFrameDecode> source = NULL;
	ThrowIfFailed(decoder->GetFrame(0, &source));

	WICPixelFormatGUID originalFormat;
	ThrowIfFailed(source->GetPixelFormat(&originalFormat));

	// Convert the image format to 32bppPBGRA, equiv to DXGI_FORMAT_B8G8R8A8_UNORM
	CComPtr<IWICFormatConverter> converter;
	ThrowIfFailed(m_wicImagingFactory->CreateFormatConverter(&converter));

	BOOL canConvertTo32bppPBGRA = false;
	ThrowIfFailed(converter->CanConvert(originalFormat, GUID_WICPixelFormat32bppPBGRA, &canConvertTo32bppPBGRA));
	assert(canConvertTo32bppPBGRA);

	ThrowIfFailed(converter->Initialize(
		source,
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone,
		NULL,
		0.f,
		WICBitmapPaletteTypeMedianCut
	));

	UINT width, height;
	ThrowIfFailed(converter->GetSize(&width, &height));
	const UINT bpp = 4;
	const UINT pitch = bpp * width;

	std::vector<UINT> imageData;
	imageData.resize(width * height);

	ThrowIfFailed(converter->CopyPixels(NULL, pitch, bpp * width * height, reinterpret_cast<BYTE*>(&(imageData[0]))));

	// Create the output resource. The dimensions and format should match the swap-chain.
	auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_B8G8R8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	auto device = m_deviceResources->GetD3DDevice();

	auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(
		&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textureInfo.Resource)));
	NAME_D3D12_OBJECT(textureInfo.Resource);

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(textureInfo.Resource.Get(), 0, 1);

	ComPtr<ID3D12Resource> textureUploadHeap;

	// Create the GPU upload buffer.
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureUploadHeap)));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = &imageData[0];
	textureData.RowPitch = width * bpp;
	textureData.SlicePitch = textureData.RowPitch * height;

	// Describe and create a SRV for the texture.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptorHandle;
	UINT descriptorIndex = srvDescriptorHeap->AllocateDescriptor(&srvDescriptorHandle, descriptorIndexToUse);
	device->CreateShaderResourceView(textureInfo.Resource.Get(), &srvDesc, srvDescriptorHandle);
	textureInfo.ResourceDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_descriptorSize);

	m_deviceResources->PrepareOffscreen();

	UpdateSubresources(m_deviceResources->GetCommandList(), textureInfo.Resource.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
	m_deviceResources->GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(textureInfo.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	m_deviceResources->ExecuteCommandList();
	m_deviceResources->WaitForGpu();

	return textureInfo;
}

VaporPlus::TextureInfo& VaporPlus::GetTextureInfo(TextureIdentifier textureID)
{
	for (size_t i = 0; i < m_allTextures.size(); ++i)
	{
		if (m_allTextures[i].TextureID == textureID)
		{
			return m_allTextures[i];
		}
	}
	return m_allTextures[0];
}