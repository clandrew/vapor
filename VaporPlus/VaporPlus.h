#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "RaytracingHlslCompat.h"
#include "ObjLoader.h"
#include "GeometryObject.h"

namespace GlobalRootSignatureParams {
    enum Value {
        OutputViewSlot = 0,
        AccelerationStructureSlot,
        SceneConstantSlot,
        VertexBuffersSlot,
		CheckerboardTextureSlot,
		SamplerSlot,
		CityscapeTextureSlot,
		TextTextureSlot,
        Count 
    };
}

namespace LocalRootSignatureParams {
    enum Value {
        CubeConstantSlot = 0,
        Count 
    };
}

// The sample supports both Raytracing Fallback Layer and DirectX Raytracing APIs. 
// This is purely for demonstration purposes to show where the API differences are. 
// Real-world applications will implement only one or the other. 
// Fallback Layer uses DirectX Raytracing if a driver and OS supports it. 
// Otherwise, it falls back to compute pipeline to emulate raytracing.
// Developers aiming for a wider HW support should target Fallback Layer.
class VaporPlus : public DXSample
{
    enum class RaytracingAPI {
        FallbackLayer,
        DirectXRaytracing,
    };

public:
    VaporPlus(UINT width, UINT height, std::wstring name);

    // IDeviceNotify
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    // Messages
    virtual void OnInit();
    virtual void OnKeyDown(UINT8 key);
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnSizeChanged(UINT width, UINT height, bool minimized);
    virtual void OnDestroy();
    virtual IDXGISwapChain* GetSwapchain() { return m_deviceResources->GetSwapChain(); }

private:
    static const UINT FrameCount = 3;

    // We'll allocate space for several of these and they will need to be padded for alignment.
    static_assert(sizeof(SceneConstantBuffer) < (2 * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), "Checking the size here.");

    union AlignedSceneConstantBuffer
    {
        SceneConstantBuffer constants;
        uint8_t alignmentPadding[2 * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
    };
    AlignedSceneConstantBuffer*  m_mappedConstantData;
    ComPtr<ID3D12Resource>       m_perFrameConstants;

	// 2D resources
	ComPtr<ID3D11Device> m_device11;
	ComPtr<ID3D11DeviceContext> m_deviceContext11;
	ComPtr<ID2D1Factory7> m_d2dFactory;
	ComPtr<ID2D1Device> m_d2dDevice;
	ComPtr<ID2D1DeviceContext> m_d2dDeviceContext;
	ComPtr<ID3D11On12Device> m_device11on12;
	ComPtr<IWICImagingFactory> m_wicImagingFactory;
	ComPtr<ID2D1SolidColorBrush> m_cyanColorBrush;
	ComPtr<IDWriteFactory> m_dwriteFactory;
	ComPtr<IDWriteTextFormat> m_topTextFormat, m_bottomTextFormat, m_statsTextFormat;
	ComPtr<IDWriteTextLayout> m_topTextLayout, m_bottomTextLayout;
	std::wstring m_frameStatsText;
        
    // Raytracing Fallback Layer (FL) attributes
    ComPtr<ID3D12RaytracingFallbackDevice> m_fallbackDevice;
    ComPtr<ID3D12RaytracingFallbackCommandList> m_fallbackCommandList;
    ComPtr<ID3D12RaytracingFallbackStateObject> m_fallbackStateObject;
    WRAPPED_GPU_POINTER m_fallbackTopLevelAccelerationStructurePointer;

    // DirectX Raytracing (DXR) attributes
    ComPtr<ID3D12Device5> m_dxrDevice;
    ComPtr<ID3D12GraphicsCommandList5> m_dxrCommandList;
    ComPtr<ID3D12StateObject> m_dxrStateObject;
    bool m_isDxrSupported;

    // Root signatures
    ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
    ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;
#if USE_NON_NULL_LOCAL_ROOT_SIG 
    ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignatureEmpty;
#endif
	ComPtr<ID3D12RootSignature> m_postprocessRootSignature;
	
	// Postprocess pass resources
	ComPtr<ID3D12PipelineState> m_postprocessPipelineState;
	ComPtr<ID3D12Resource> m_postprocessVertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_postprocessVertexBufferView;

	// Textures
	struct TextureInfo
	{
		TextureIdentifier TextureID;
		std::wstring Filename;
		ComPtr<ID3D12Resource> Resource;
		D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptor;

		ComPtr<ID3D11Texture2D> Texture11;
		ComPtr<ID2D1Bitmap1> Texture2DTarget;
	};
	std::vector<TextureInfo> m_allTextures;

	class DescriptorHeapWrapper
	{
		ID3D12Device* m_device;
		UINT m_descriptorSize;
		ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
		UINT m_descriptorsAllocated;

	public:
		void Initialize(ID3D12Device* device, UINT descriptorCount);
		
		// Returns the descriptor index
		UINT CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize);
		UINT CreateTextureUAV(ID3D12Resource* resource, UINT descriptorIndexToUse = UINT_MAX);

		UINT AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse = UINT_MAX);
		void Reset();
		ID3D12DescriptorHeap* GetResource() { return m_descriptorHeap.Get(); }
		D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStart() { return m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(); }
	};

	DescriptorHeapWrapper m_raytracingDescriptorHeap;

	DescriptorHeapWrapper m_postprocessSRVHeap;

	ComPtr<ID3D12DescriptorHeap> m_samplerDescriptorHeap;
    UINT m_descriptorSize;
    
    // Raytracing scene
    SceneConstantBuffer m_sceneCB[FrameCount];
	CubeConstantBuffer m_perGeometryConstantBuffer;

	D3DBuffer m_indexBuffer;
	D3DBuffer m_vertexBuffer;

	GeometryObject m_floor;
	GeometryObject m_helios;
	GeometryObject m_cityscape;
	GeometryObject m_text;

	D3D12_GPU_DESCRIPTOR_HANDLE m_samplerDescriptor;

    // Acceleration structure
    ComPtr<ID3D12Resource> m_bottomLevelAccelerationStructure;
    ComPtr<ID3D12Resource> m_topLevelAccelerationStructure;
	ComPtr<ID3D12Resource> m_accelerationStructureScratchResource;

    // Raytracing output
    ComPtr<ID3D12Resource> m_raytracingOutput;
    D3D12_GPU_DESCRIPTOR_HANDLE m_raytracingOutputResourceUAVGpuDescriptor;
    UINT m_raytracingOutputResourceUAVDescriptorHeapIndexDuringRaytracing;
	UINT m_raytracingOutputResourceUAVDescriptorHeapIndexDuringPostprocess;

    // Shader tables
    static const wchar_t* c_hitGroupName;
    static const wchar_t* c_raygenShaderName;
    static const wchar_t* c_closestHitShaderName;
    static const wchar_t* c_missShaderName;
	static const wchar_t* c_missShaderName_Shadow;
	static const wchar_t* c_missShaderName_Reflection;
    ComPtr<ID3D12Resource> m_missShaderTable;
    ComPtr<ID3D12Resource> m_hitGroupShaderTable;
    ComPtr<ID3D12Resource> m_rayGenShaderTable;
	uint32_t m_hitGroupShaderRecordCount;
	uint32_t m_missShaderRecordCount;
    
    // Application state
    RaytracingAPI m_raytracingAPI;
    bool m_forceComputeFallback;
    StepTimer m_timer;
    float m_curRotationAngleRad;
    XMVECTOR m_eye;
    XMVECTOR m_at;
    XMVECTOR m_up;
	bool m_enableTextFrame;
	bool m_enablePostprocess;
	float m_floorTextureOffsetX = 0;
	float m_floorTextureOffsetY = 0;


	LARGE_INTEGER m_performanceCounter;
	LARGE_INTEGER m_performanceFrequency;
	
	// Asset loader
	ObjLoader m_objLoader;

    void EnableDXRExperimentalFeatures(IDXGIAdapter1* adapter);
    void ParseCommandLineArgs(WCHAR* argv[], int argc);
    void UpdateCameraMatrices();
    void InitializeScene();
    void RecreateD3D();
    void DoRaytracing();
    void CreateConstantBuffers();
    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();
    void CreateRaytracingInterfaces();
    void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig);
    void CreateRootSignatures();
    void CreateLocalRootSignatureSubobjects(CD3D12_STATE_OBJECT_DESC* raytracingPipeline);
    void CreateRaytracingPipelineStateObject();
	void CreatePostprocessPipelineState();
	void CreatePostprocessResources();
    void CreateDescriptorHeaps();
    void CreateRaytracingOutputResource();
	void CreateSampler();
    void BuildGeometry();
    void BuildAccelerationStructures();
	void UpdateBottomLevelAccelerationStructure();
    void BuildShaderTables();
    void SelectRaytracingAPI(RaytracingAPI type);
    void UpdateForSizeChange(UINT clientWidth, UINT clientHeight);
	void DrawRaytracingOutputToTarget();
    void CalculateFrameStats();

	void LoadTextures();

    WRAPPED_GPU_POINTER CreateFallbackWrappedPointer(ID3D12Resource* resource, UINT bufferNumElements);

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO CreateTopLevelPrebuild(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags);
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO CreateBottomLevelPrebuild(
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags,
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> const& bottomLevelGeometries);

	struct AccelerationStructureResources
	{
		ComPtr<ID3D12Resource> ScratchResource;
		ComPtr<ID3D12Resource> InstanceDescs;
	};
	AccelerationStructureResources m_accelerationStructureResources;

	void UpdateAnimation();

	TextureInfo LoadImageTextureAsset(
		TextureIdentifier textureID,
		wchar_t const* filename,
		DescriptorHeapWrapper* srvDescriptorHeap,
		UINT descriptorIndexToUse = UINT_MAX);

	TextureInfo Create2DTargetTextureAsset(TextureIdentifier textureID);
	TextureInfo& GetTextureInfo(TextureIdentifier textureID);
	void Draw2DTextToTexture(TextureInfo const& textTexture);
};
