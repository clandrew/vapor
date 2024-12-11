#pragma once
#include "DescriptorHeapWrapper.h"

class Postprocess
{
	ComPtr<ID3D12RootSignature> m_postprocessRootSignature;
	ComPtr<ID3D12PipelineState> m_postprocessPipelineState;
	ComPtr<ID3D12Resource> m_postprocessVertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_postprocessVertexBufferView;

	DescriptorHeapWrapper m_postprocessSRVHeap;

	UINT m_raytracingOutputResourceUAVDescriptorHeapIndexDuringPostprocess;

public:
	void CreatePostprocessPipelineState(ID3D12Device5* device);
	void CreatePostprocessResources(ID3D12Device5* device);
	void CreateRootSignature(ID3D12Device5* device);
	void CreateRaytracedInputSRV(ID3D12Resource* resource);

	ID3D12PipelineState* GetPipelineState() const;
	ID3D12RootSignature* GetRootSignature() const;
	DescriptorHeapWrapper* GetSRVHeap();
	ID3D12DescriptorHeap* GetSRVHeapResource() const;
	D3D12_VERTEX_BUFFER_VIEW const* GetVertexBufferView() const;
};