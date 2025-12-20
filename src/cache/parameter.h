#ifndef PARAMETER_H
#define PARAMETER_H

#include <iostream>

#include "utils.h"

typedef unsigned int cycle_t;
typedef uint32_t block_addr_t;
typedef uint32_t wshr_idx_t;


class cache_building_block{
public:

    uint32_t get_block_idx(uint32_t word_addr){
        return word_addr >> LOGB2(LINEWORDS);
    }

    uint32_t get_block_offset(uint32_t word_addr){
        return ((1 << LOGB2(LINEWORDS)) - 1) & word_addr;
    }

    uint32_t get_tag(uint32_t block_idx){
        return block_idx >> LOGB2(NSET);
    }
    uint32_t get_set_idx(uint32_t block_idx){
        return ((1 << LOGB2(NSET)) - 1) & block_idx;
    }

public:
  //                                       |  blockOffset  |
  //                                       |             wordOffset
  // |32      tag       12|11   setIdx    7|6 4|3         2|1 0|
    constexpr static uint32_t WORDSIZE = 4;//in bytes
    constexpr static uint32_t ADDR_LENGTH = 32;

    constexpr static uint32_t NLANE = 32;//32;

    // 注意：NSET 和 LINEWORDS 用于定义缓存内部结构大小（如 tag_array 的数组大小），不用于地址计算
    // 地址计算应使用 L1D_NUM_SET 和 L1D_BLOCK_NUM_WORD（定义在 src/parameters.h）
    constexpr static uint32_t NSET = 4;//32;//For 4K B per way
    constexpr static uint32_t NWAY = 2;

    constexpr static uint32_t NLINE = NSET * NWAY;
    // 注意：LINEWORDS 用于定义缓存内部结构大小，不用于地址计算
    // 地址计算应使用 L1D_BLOCK_NUM_WORD（定义在 src/parameters.h）
    constexpr static uint32_t LINEWORDS = NLANE;//TODO: decouple this param with NLANE
    constexpr static uint32_t LINESIZE = LINEWORDS * WORDSIZE;//in bytes
    //TODO assert(check LINESIZE to be the power of 2)
    constexpr static uint32_t CACHESIZE = NLINE *LINESIZE;

    const static uint32_t DATA_SRAM_LATENCY = 0;//in cycle

    constexpr static uint32_t N_MSHR_ENTRY = 4;
    constexpr static uint32_t N_MSHR_SUBENTRY = 2;//4;
    constexpr static uint32_t N_MSHR_SPECIAL_ENTRY = 4;
    uint32_t N_MSHR_WRITE_UNDER_READ_MISS = std::max(4U,N_MSHR_ENTRY/8);

    constexpr static uint32_t N_WSHR_ENTRY = 4;

    typedef std::array<uint32_t,NLANE> vec_nlane_t;

    constexpr static uint32_t CORE_RSP_Q_DEPTH = 2;
    constexpr static uint32_t MEM_REQ_Q_DEPTH = 10;
    constexpr static uint32_t MEM_RSP_Q_DEPTH = 4;

    typedef std::array<uint32_t,LINEWORDS> cache_line_t;
    static constexpr const long unsigned int hw_num_thread = 32;
};

#endif