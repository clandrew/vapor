#include "stdafx.h"
#include "GeometryObject.h"
#include "DirectXRaytracingHelper.h"
#include "VaporPlus.h"
#include "ObjLoader.h"

void GeometryObject::Initialize(TextureIdentifier textureIdentifier, uint32_t material)
{
	m_textureID = textureIdentifier;

	m_material = material;
	m_floatAnimationCounter = 0;
	m_spinAnimationCounter = 0;
}

void GeometryObject::LoadCube(
	ObjLoader* loader,
	float xScale,
	float yScale,
	float zScale,
	float xTranslate,
	float yTranslate,
	float zTranslate,
	float uvScale,
	DX::DeviceResources* deviceResources,
	UINT descriptorSize,
	std::vector<Vertex>* vertices,
	std::vector<Index>* indices)
{
	size_t vertexBaseline = vertices->size();
	size_t indexBaseline = indices->size();

	m_baseTransform = XMMatrixScaling(xScale, yScale, zScale) * XMMatrixTranslation(xTranslate, yTranslate, zTranslate);

	loader->GetCubeVerticesAndIndices(1, 1, 1, 0, 0, 0, uvScale, vertices, indices);
	m_vertexCount = vertices->size() - vertexBaseline;
	m_indexCount = indices->size() - indexBaseline;
	m_vertexBufferOffset = vertexBaseline * sizeof(Vertex);
	m_indexBufferOffset = indexBaseline * sizeof(Index);

	assert(m_indexBufferOffset % 6 == 0); // Three two-byte indices should be written at a time

	CreateTransformBuffer(deviceResources, m_baseTransform);
}


void GeometryObject::LoadObjMesh(
	std::string name,
	float scale,
	ObjLoader * loader,
	DX::DeviceResources * deviceResources,
	UINT descriptorSize,
	XMMATRIX transform,
	std::vector<Vertex> * vertices,
	std::vector<Index> * indices)
{
	size_t vertexBaseline = vertices->size();
	size_t indexBaseline = indices->size();
	loader->GetObjectVerticesAndIndices(name, scale, vertices, indices);

	m_vertexCount = vertices->size() - vertexBaseline;
	m_indexCount = indices->size() - indexBaseline;
	m_vertexBufferOffset = vertexBaseline * sizeof(Vertex);
	m_indexBufferOffset = indexBaseline * sizeof(Index);

	assert(m_indexBufferOffset % 6 == 0);

	m_baseTransform = transform;
	CreateTransformBuffer(deviceResources, m_baseTransform);
}

struct Matrix3x4
{
	FLOAT m[12];
};

static Matrix3x4 ToMatrix3x4(XMMATRIX const& transform)
{
	Matrix3x4 transformBuffer;

	for (int x = 0; x < 4; ++x)
	{
		for (int y = 0; y < 3; ++y)
		{
			transformBuffer.m[y * 4 + x] = transform.r[x].m128_f32[y];
		}
	}
	transformBuffer.m[3] = transform.r[3].m128_f32[0];
	transformBuffer.m[7] = transform.r[3].m128_f32[1];
	transformBuffer.m[11] = transform.r[3].m128_f32[2];

	return transformBuffer;
}

void GeometryObject::CreateTransformBuffer(DX::DeviceResources * deviceResources, XMMATRIX transform)
{
	Matrix3x4 transformBuffer = {};
	size_t requiredBufferSize = sizeof(transformBuffer.m);

	// Allocate transform buffer
	auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto transformTextureDesc = CD3DX12_RESOURCE_DESC::Buffer(requiredBufferSize, D3D12_RESOURCE_FLAG_NONE);
	ThrowIfFailed(deviceResources->GetD3DDevice()->CreateCommittedResource(
		&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &transformTextureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_transformBuffer)));
	NAME_D3D12_OBJECT(m_transformBuffer);

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_transformBuffer.Get(), 0, 1);

	// Create the GPU upload buffer.
	ThrowIfFailed(deviceResources->GetD3DDevice()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_transformResourceUploadHeap)));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = &transformBuffer;
	transformBuffer = ToMatrix3x4(transform);

	textureData.RowPitch = requiredBufferSize;
	textureData.SlicePitch = textureData.RowPitch;

	deviceResources->PrepareOffscreen();

	UpdateSubresources(deviceResources->GetCommandList(), m_transformBuffer.Get(), m_transformResourceUploadHeap.Get(), 0, 0, 1, &textureData);
	deviceResources->GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_transformBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	deviceResources->ExecuteCommandList();
	deviceResources->WaitForGpu();
}

void GeometryObject::UpdateFloatyTransform(DX::DeviceResources * deviceResources)
{
	m_floatAnimationCounter = (m_floatAnimationCounter + 1) % 1000;
	float twoPi = 3.14159f * 2;
	float angle = (static_cast<float>(m_floatAnimationCounter) / 1000.0f) * twoPi;
	float height = (sin(angle) + 1.0f) * 0.1f;
	XMMATRIX transform = XMMatrixTranslation(0, height, 0);

	if (m_spin)
	{
		m_spinAnimationCounter = (m_spinAnimationCounter + 1) % 500;
		float angle = (-static_cast<float>(m_spinAnimationCounter) / 500.0f) * twoPi;
		XMVECTOR yAxis = { 0, 1, 0 };
		transform = transform * XMMatrixRotationAxis(yAxis, angle);
	}

	m_netTransform = transform * m_baseTransform;

	UpdateTransform(deviceResources, m_netTransform);
}

XMMATRIX GeometryObject::GetTransform() const
{
	return m_netTransform;
}

void GeometryObject::UpdateTransform(DX::DeviceResources * deviceResources, XMMATRIX const& transform)
{
	deviceResources->GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_transformBuffer.Get(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, // State changed after BLAS build
		D3D12_RESOURCE_STATE_COPY_DEST));

	D3D12_SUBRESOURCE_DATA textureData = {};
	Matrix3x4 transformBuffer = ToMatrix3x4(transform);
	textureData.pData = &transformBuffer;

	UpdateSubresources(deviceResources->GetCommandList(), m_transformBuffer.Get(), m_transformResourceUploadHeap.Get(), 0, 0, 1, &textureData);

	deviceResources->GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_transformBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

D3D12_RAYTRACING_GEOMETRY_DESC GeometryObject::GetRaytracingGeometryDesc(D3DBuffer * vertexBuffer, D3DBuffer * indexBuffer)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.IndexBuffer = indexBuffer->resource->GetGPUVirtualAddress() + m_indexBufferOffset;
	geometryDesc.Triangles.IndexCount = CheckCastUint(m_indexCount);
	geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
	geometryDesc.Triangles.Transform3x4 = m_transformBuffer->GetGPUVirtualAddress();
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer->resource->GetGPUVirtualAddress();
	geometryDesc.Triangles.VertexCount = CheckCastUint(m_vertexCount);
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

	// Mark the geometry as opaque. 
	// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
	// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	return geometryDesc;
}

uint32_t GeometryObject::GetIndexBufferOffset()
{
	assert(m_indexBufferOffset < UINT_MAX);
	return static_cast<uint32_t>(m_indexBufferOffset);
}

uint32_t GeometryObject::GetMaterial()
{
	return m_material;
}