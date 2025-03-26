#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <iostream>

#include "utils.h"

typedef unsigned int cycle_t;
typedef u_int32_t block_addr_t;
typedef u_int32_t wshr_idx_t;

class cache_building_block{
public:

    u_int32_t get_block_idx(u_int32_t word_addr){
        return word_addr >> LOGB2(LINEWORDS);
    }

    u_int32_t get_block_offset(u_int32_t word_addr){
        return ((1 << LOGB2(LINEWORDS)) - 1) & word_addr;
    }

    u_int32_t get_tag(u_int32_t block_idx){
        return block_idx >> LOGB2(NSET);
    }
    u_int32_t get_set_idx(u_int32_t block_idx){
        return ((1 << LOGB2(NSET)) - 1) & block_idx;
    }

public:
  //                                       |  blockOffset  |
  //                                       |             wordOffset
  // |32      tag       12|11   setIdx    7|6 4|3         2|1 0|
    constexpr static u_int32_t WORDSIZE = 4;//in bytes
    constexpr static u_int32_t ADDR_LENGTH = 32;

    constexpr static u_int32_t NLANE = 4;//32;

    constexpr static u_int32_t NSET = 4;//32;//For 4K B per way
    constexpr static u_int32_t NWAY = 2;

    constexpr static u_int32_t NLINE = NSET * NWAY;
    constexpr static u_int32_t LINEWORDS = NLANE;//TODO: decouple this param with NLANE
    constexpr static u_int32_t LINESIZE = LINEWORDS * WORDSIZE;//in bytes
    //TODO assert(check LINESIZE to be the power of 2)
    constexpr static u_int32_t CACHESIZE = NLINE *LINESIZE;

    const static u_int32_t DATA_SRAM_LATENCY = 0;//in cycle

    constexpr static u_int32_t N_MSHR_ENTRY = 4;
    constexpr static u_int32_t N_MSHR_SUBENTRY = 2;//4;
    constexpr static u_int32_t N_MSHR_SPECIAL_ENTRY = 4;
    u_int32_t N_MSHR_WRITE_UNDER_READ_MISS = std::max(4U,N_MSHR_ENTRY/8);

    constexpr static u_int32_t N_WSHR_ENTRY = 4;

    typedef std::array<u_int32_t,NLANE> vec_nlane_t;

    constexpr static u_int32_t CORE_RSP_Q_DEPTH = 2;
    constexpr static u_int32_t MEM_REQ_Q_DEPTH = 10;
    constexpr static u_int32_t MEM_RSP_Q_DEPTH = 4;

    typedef std::array<u_int32_t,LINEWORDS> cache_line_t;
};

#endif