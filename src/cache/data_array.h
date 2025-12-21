#ifndef DATA_ARRAY_H
#define DATA_ARRAY_H

#include "interfaces.h"
#include "parameter.h"
#include "utils.h"
#include <bitset>
#include <iomanip>

class data_array : cache_building_block {
public:
    // return: 数据
    std::array<uint32_t, hw_num_thread> read(uint32_t set_idx, uint32_t way_idx);

    void read_in(uint32_t set_idx, uint32_t way_idx);

    std::array<uint32_t, hw_num_thread> read_out();

    void fill(uint32_t set_idx, uint32_t way_idx, std::array<uint32_t, hw_num_thread>& fill_line);

    // 用于正常的write hit
    void write_hit(
        uint32_t set_idx, uint32_t way_idx, vec_nlane_t hit_data, vec_nlane_t block_offset,
        std::array<bool, NLANE> lane_mask
    );

    // 仅用于write miss under read miss的release
    void write_hit(
        uint32_t set_idx, uint32_t way_idx, std::array<uint32_t, hw_num_thread> data,
        std::array<bool, LINEWORDS> mask
    );

    void DEBUG_visualize_array(uint32_t set_idx_start = 0, uint32_t set_idx_end = NSET - 1);

    void DEBUG_print_title();

    void DEBUG_print_a_way(uint32_t set_idx, uint32_t way_idx);

private:
    std::array<std::array<std::array<uint32_t, hw_num_thread>, NWAY>, NSET> m_data;
    std::array<uint32_t, hw_num_thread> m_read_data_o_r;
};
#endif