#pragma once

#include "hardware_config.hpp"
#include "ventus_cyclesim.h"

inline uint64_t resident_pds_pool_size(const ventus_kernel_metadata_t& metadata) {
    return static_cast<uint64_t>(metadata.pdsSize) * static_cast<uint64_t>(metadata.wf_size)
        * static_cast<uint64_t>(metadata.wg_size) * RESIDENT_PDS_SLOT_COUNT;
}

inline uint64_t page_aligned_size_4k(uint64_t size) {
    constexpr uint64_t PAGE_MASK = 0xfffu;
    return (size + PAGE_MASK) & ~PAGE_MASK;
}

inline uint64_t resident_pds_pool_vmem_size(const ventus_kernel_metadata_t& metadata) {
    return page_aligned_size_4k(resident_pds_pool_size(metadata));
}
