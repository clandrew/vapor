#pragma once
#include "RaytracingHlslCompat.h"
#include "CheckCast.h"

class ObjLoader;

class GeometryObject
{
	ComPtr<ID3D12Resource> m_transformResourceUploadHeap;

	TextureIdentifier m_textureID;

	ComPtr<ID3D12Resource> m_transformBuffer;

	size_t m_vertexCount;
	size_t m_vertexBufferOffset;
	size_t m_indexCount;
	size_t m_indexBufferOffset;

	uint32_t m_material;

	int m_floatAnimationCounter;
	int m_spinAnimationCounter;
	bool m_spin;
	XMMATRIX m_baseTransform;
	XMMATRIX m_netTransform;

public:
	void Initialize(TextureIdentifier textureIdentifier, uint32_t material);

	void LoadCube(
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
		std::vector<Vertex>* floorVertices,	
		std::vector<Index>* indices);

	void LoadObjMesh(
		std::string name,
		float scale,
		ObjLoader* loader,
		DX::DeviceResources* deviceResources,
		UINT descriptorSize,
		XMMATRIX transform,
		std::vector<Vertex>* floorVertices,
		std::vector<Index>* indices);
	
	D3D12_RAYTRACING_GEOMETRY_DESC GetRaytracingGeometryDesc(D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer);

	TextureIdentifier GetTextureIdentifier() const
	{
		return m_textureID;
	}

	uint32_t GetIndexBufferOffset();

	uint32_t GetMaterial();

	void UpdateFloatyTransform(DX::DeviceResources* deviceResources);
	void UpdateTransform(DX::DeviceResources* deviceResources, XMMATRIX const& transform);

	XMMATRIX GetTransform() const;

	void SetFloatAnimationCounter(uint32_t animationCounter)
	{
		m_floatAnimationCounter = animationCounter;
	}

	void SetSpinEnabled(bool spin)
	{
		m_spin = spin;
		m_spinAnimationCounter = 0;
	}

	bool IsSpinEnabled() const
	{
		return m_spin;
	}

private:
	void CreateTransformBuffer(DX::DeviceResources* deviceResources, XMMATRIX transform);
};