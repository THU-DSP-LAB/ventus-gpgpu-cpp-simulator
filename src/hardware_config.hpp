#pragma once

#include <cstdint>

inline constexpr unsigned SUBCORE_NUM = 2;
inline constexpr unsigned SUBCORE_WARP_NUM = 4;

inline constexpr int hw_num_warp = SUBCORE_WARP_NUM * SUBCORE_NUM; // 每个SM的硬件warp数量
inline constexpr unsigned MAX_CTA_PER_CORE
    = hw_num_warp; // 每个core支持的最大cta数目，不应大于hw_num_warp
inline constexpr int MAX_WARP_PER_BLOCK = hw_num_warp; // 每个block支持的最大warp数目
inline constexpr int NUM_SM = 2;

inline constexpr uint64_t RESIDENT_PDS_SLOT_COUNT =
    static_cast<uint64_t>(NUM_SM) * static_cast<uint64_t>(MAX_CTA_PER_CORE);
