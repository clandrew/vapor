#include "stdafx.h"
#include "Postprocess.h"
#include "CompiledShaders\PostprocessVS.hlsl.h"
#include "CompiledShaders\PostprocessPS.hlsl.h"

using namespace DirectX;

void Postprocess::CreatePostprocessPipelineState(ID3D12Device5* device)
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
	psoDesc.VS = CD3DX12_SHADER_BYTECODE((void*)g_pPostprocessVS, ARRAYSIZE(g_pPostprocessVS));
	psoDesc.PS = CD3DX12_SHADER_BYTECODE((void*)g_pPostprocessPS, ARRAYSIZE(g_pPostprocessPS));
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_postprocessPipelineState)));
}

void Postprocess::CreatePostprocessResources(ID3D12Device5* device)
{
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

void Postprocess::CreateRootSignature(ID3D12Device5* device)
{
	CD3DX12_DESCRIPTOR_RANGE srvTableRange[1]{};
	srvTableRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE samplerTableRange[1]{};
	samplerTableRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

	CD3DX12_ROOT_PARAMETER rootParameters[3];
	rootParameters[0].InitAsDescriptorTable(ARRAYSIZE(srvTableRange), srvTableRange, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[1].InitAsConstants(2, 0);
	rootParameters[2].InitAsDescriptorTable(ARRAYSIZE(samplerTableRange), samplerTableRange, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);

	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	SerializeAndCreateRootSignature(device, rootSignatureDesc, &m_postprocessRootSignature);
}

void Postprocess::CreateRaytracedInputUAV(ID3D12Resource* resource)
{
	m_raytracingOutputResourceUAVDescriptorHeapIndexDuringPostprocess = m_postprocessSRVHeap.CreateTextureUAV(resource);
}

ID3D12PipelineState* Postprocess::GetPipelineState() const
{
	return m_postprocessPipelineState.Get();
}

ID3D12RootSignature* Postprocess::GetRootSignature() const
{
	return m_postprocessRootSignature.Get();
}

DescriptorHeapWrapper* Postprocess::GetSRVHeap()
{
	return &m_postprocessSRVHeap;
}

ID3D12DescriptorHeap* Postprocess::GetSRVHeapResource() const
{
	return m_postprocessSRVHeap.GetResource();
}

D3D12_VERTEX_BUFFER_VIEW const* Postprocess::GetVertexBufferView() const
{
	return &m_postprocessVertexBufferView;
}