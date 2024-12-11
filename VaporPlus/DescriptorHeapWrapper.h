#pragma once

class DescriptorHeapWrapper
{
	ID3D12Device* m_device;
	UINT m_descriptorSize;
	ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
	UINT m_descriptorsAllocated;
	UINT m_raytracingOutputResourceUAVDescriptorHeapIndexDuringPostprocess;

public:
	void Initialize(ID3D12Device* device, UINT descriptorCount);

	// Returns the descriptor index
	UINT CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize);
	UINT CreateTextureUAV(ID3D12Resource* resource, UINT descriptorIndexToUse = UINT_MAX);
	UINT CreateTextureSRV(ID3D12Resource* resource, UINT descriptorIndexToUse = UINT_MAX);

	UINT AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse = UINT_MAX);
	void Reset();
	ID3D12DescriptorHeap* GetResource() const { return m_descriptorHeap.Get(); }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStart() { return m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(); }
};