#include "stdafx.h"
#include "DescriptorHeapWrapper.h"

void DescriptorHeapWrapper::Initialize(ID3D12Device* device, UINT descriptorCount)
{
	m_device = device;
	m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	descriptorHeapDesc.NumDescriptors = descriptorCount;

	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptorHeapDesc.NodeMask = 0;
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_descriptorHeap));
	NAME_D3D12_OBJECT(DescriptorHeapWrapper::m_descriptorHeap);
}

UINT DescriptorHeapWrapper::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
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
UINT DescriptorHeapWrapper::CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize)
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

UINT DescriptorHeapWrapper::CreateTextureUAV(ID3D12Resource* resource, UINT descriptorIndexToUse)
{
	// Allocate a descriptor to be read by the postprocess
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleUnused;
	UINT descriptorIndex = AllocateDescriptor(&cpuHandleUnused, descriptorIndexToUse);
	m_device->CreateUnorderedAccessView(resource, nullptr, &UAVDesc, m_descriptorHeap->GetCPUDescriptorHandleForHeapStart());
	return descriptorIndex;
}

UINT DescriptorHeapWrapper::CreateTextureSRV(ID3D12Resource* resource, UINT descriptorIndexToUse)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MipLevels = 1;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleUnused;
	UINT descriptorIndex = AllocateDescriptor(&cpuHandleUnused, descriptorIndexToUse);
	m_device->CreateShaderResourceView(resource, &SRVDesc, m_descriptorHeap->GetCPUDescriptorHandleForHeapStart());
	return descriptorIndex;
}

void DescriptorHeapWrapper::Reset()
{
	m_descriptorHeap.Reset();
	m_descriptorsAllocated = 0;
}