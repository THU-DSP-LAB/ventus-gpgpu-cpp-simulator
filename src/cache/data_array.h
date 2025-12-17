#ifndef DATA_ARRAY_H
#define DATA_ARRAY_H

#include "utils.h"
#include "parameter.h"
#include "interfaces.h"
#include <iomanip>
#include <bitset>

class data_array : cache_building_block {
public:
    //return: 数据
    std::array<uint32_t, hw_num_thread> read(uint32_t set_idx,uint32_t way_idx){
        return m_data[set_idx][way_idx];
    }

    void read_in(uint32_t set_idx,uint32_t way_idx){
        m_read_data_o_r = m_data[set_idx][way_idx];
    }

    std::array<uint32_t, hw_num_thread> read_out(){
        return m_read_data_o_r;
    }

    void fill(uint32_t set_idx,uint32_t way_idx,std::array<uint32_t, hw_num_thread>& fill_line){
        m_data[set_idx][way_idx] = fill_line;
    }

    //用于正常的write hit
    void write_hit(uint32_t set_idx,uint32_t way_idx, 
        vec_nlane_t hit_data, vec_nlane_t block_offset, std::array<bool,NLANE> lane_mask){
        auto& selected_line = m_data[set_idx][way_idx];
        for(int i = 0;i<NLANE;++i){
            if(lane_mask[i]==true){//在硬件中，这里是offset矩阵转置的独热码
                selected_line[block_offset[i]] = hit_data[i];
            }
        }
    }

    //仅用于write miss under read miss的release
    void write_hit(uint32_t set_idx,uint32_t way_idx, 
        std::array<uint32_t, hw_num_thread> data, std::array<bool,LINEWORDS> mask){
        auto& selected_line = m_data[set_idx][way_idx];
        for(int i = 0;i<LINEWORDS;++i){
            if(mask[i]==true){
                selected_line[i] = data[i];
            }
        }
    }

    void DEBUG_visualize_array(uint32_t set_idx_start=0, uint32_t set_idx_end=NSET-1){
        DEBUG_print_title();
        for(int i=set_idx_start;i<set_idx_start+set_idx_end;++i){
            std::cout << std::setw(7) << i << " |";
            for (int j=0;j<NWAY;++j){
                DEBUG_print_a_way(i,j);
            }
            std::cout << std::endl;
        }
    }

    void DEBUG_print_title(){
        std::cout << "set_idx ";
        for(int i=0;i<NWAY;++i){
            std::cout << "| way "<< i <<"               ";
        }
        std::cout <<"|"<< std::endl << std::setw(8) <<" ";
        for(int i=0;i<NWAY;++i){
            std::cout << "| v d --tag--- lat lft";
        }
        std::cout << std::endl;
    }

    void DEBUG_print_a_way(uint32_t set_idx, uint32_t way_idx){
        auto& the_one = m_data[set_idx][way_idx];
        for(const auto& word : the_one){
            std::cout << word << ",";
        }
        std::cout << std::endl;
    }

private:
    std::array<std::array<std::array<uint32_t, hw_num_thread>, NWAY>,NSET> m_data;
    std::array<uint32_t, hw_num_thread> m_read_data_o_r;
};
#endif