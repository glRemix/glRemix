#pragma once

#include <array>
#include <deque>

#include <dx/d3d12_descriptor_heap.h>
#include <dx/d3d12_context.h>

namespace glRemix::dx
{
class DescriptorPager
{
public:
    // Try to arrange in order of frequency of new page allocations (least to most)
    enum PageType : UINT8
    {
        MATERIALS,

        TEXTURES,
        VB_IB,
        MESH_RECORDS,

        END,
    };

private:
    // CPU descriptor heaps
    // Use deque instead of vector to ensure pointer stability
    std::array<std::deque<D3D12DescriptorHeap>, END> m_pages{};

#define PAGE_CATEGORIES 4

    const std::array<UINT, PAGE_CATEGORIES> m_descriptors_per_page{
        64,  // MATERIALS
        64,  // TEXTURES
        64,  // VB_IB
        64,  // MESH_RECORDS
    };

    static_assert(END == PAGE_CATEGORIES);

#ifdef PAGE_CATEGORIES
#undef PAGE_CATEGORIES
#endif

    // All pages including and following that index are dirty and need to be re-uploaded to GPU heap
    PageType m_dirty_index = END;

public:
    // Returns page index
    UINT allocate_descriptor(const D3D12Context& context, PageType type,
                             D3D12Descriptor* descriptor);

    void free_descriptor(PageType type, D3D12Descriptor* descriptor);

    // Calculates global offset of page.
    // For example if type=VB_IB and page_index = 1 this would return (number of pages for
    // MATERIALS) * (MATERIALS page size) + (VB_IB page size * 1)
    UINT calculate_global_offset(PageType type, UINT page_index) const;

    // This should only be called once per frame
    void copy_pages_to_gpu(const D3D12Context& context, D3D12DescriptorHeap* gpu_heap, UINT offset);
};
}  // namespace glRemix::dx
