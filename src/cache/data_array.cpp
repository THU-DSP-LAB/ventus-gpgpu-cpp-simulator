#include "data_array.h"

// read 方法：返回指定set和way的数据
std::array<uint32_t, hw_num_thread> data_array::read(uint32_t set_idx, uint32_t way_idx) {
    return m_data[set_idx][way_idx];
}

// read_in 方法：读取数据到输出寄存器
void data_array::read_in(uint32_t set_idx, uint32_t way_idx) {
    m_read_data_o_r = m_data[set_idx][way_idx];
}

// read_out 方法：返回读取的数据
std::array<uint32_t, hw_num_thread> data_array::read_out() { return m_read_data_o_r; }

// fill 方法：填充cache行
void data_array::fill(
    uint32_t set_idx, uint32_t way_idx, std::array<uint32_t, hw_num_thread>& fill_line
) {
    m_data[set_idx][way_idx] = fill_line;
}

// write_hit 方法（向量版本）：用于正常的write hit
void data_array::write_hit(
    uint32_t set_idx, uint32_t way_idx, vec_nlane_t hit_data, vec_nlane_t block_offset,
    std::array<bool, NLANE> lane_mask
) {
    auto& selected_line = m_data[set_idx][way_idx];
    for (int i = 0; i < NLANE; ++i) {
        if (lane_mask[i] == true) { // 在硬件中，这里是offset矩阵转置的独热码
            selected_line[block_offset[i]] = hit_data[i];
        }
    }
}

// write_hit 方法（标量版本）：仅用于write miss under read miss的release
void data_array::write_hit(
    uint32_t set_idx, uint32_t way_idx, std::array<uint32_t, hw_num_thread> data,
    std::array<bool, LINEWORDS> mask
) {
    auto& selected_line = m_data[set_idx][way_idx];
    for (int i = 0; i < LINEWORDS; ++i) {
        if (mask[i] == true) {
            selected_line[i] = data[i];
        }
    }
}

// DEBUG_visualize_array 方法：可视化数据数组
void data_array::DEBUG_visualize_array(uint32_t set_idx_start, uint32_t set_idx_end) {
    DEBUG_print_title();
    for (int i = set_idx_start; i < set_idx_start + set_idx_end; ++i) {
        std::cout << std::setw(7) << i << " |";
        for (int j = 0; j < NWAY; ++j) {
            DEBUG_print_a_way(i, j);
        }
        std::cout << std::endl;
    }
}

// DEBUG_print_title 方法：打印调试标题
void data_array::DEBUG_print_title() {
    std::cout << "set_idx ";
    for (int i = 0; i < NWAY; ++i) {
        std::cout << "| way " << i << "               ";
    }
    std::cout << "|" << std::endl << std::setw(8) << " ";
    for (int i = 0; i < NWAY; ++i) {
        std::cout << "| v d --tag--- lat lft";
    }
    std::cout << std::endl;
}

// DEBUG_print_a_way 方法：打印一个way的数据
void data_array::DEBUG_print_a_way(uint32_t set_idx, uint32_t way_idx) {
    auto& the_one = m_data[set_idx][way_idx];
    for (const auto& word : the_one) {
        std::cout << word << ",";
    }
    std::cout << std::endl;
}
