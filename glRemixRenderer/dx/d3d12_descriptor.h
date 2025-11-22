#pragma once

namespace glRemix::dx
{
class D3D12DescriptorHeap;

constexpr UINT CREATE_NEW_DESCRIPTOR = UINT_MAX;

struct D3D12Descriptor
{
    D3D12DescriptorHeap* heap = nullptr;
    UINT offset = CREATE_NEW_DESCRIPTOR;
};
}  // namespace glRemix::dx
