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
    cache_line_t read(u_int32_t set_idx,u_int32_t way_idx){
        return m_data[set_idx][way_idx];
    }

    void read_in(u_int32_t set_idx,u_int32_t way_idx){
        m_read_data_o_r = m_data[set_idx][way_idx];
    }

    cache_line_t read_out(){
        return m_read_data_o_r;
    }

    void fill(u_int32_t set_idx,u_int32_t way_idx,cache_line_t& fill_line){
        m_data[set_idx][way_idx] = fill_line;
    }

    //用于正常的write hit
    void write_hit(u_int32_t set_idx,u_int32_t way_idx, 
        vec_nlane_t hit_data, vec_nlane_t block_offset, std::array<bool,NLANE> lane_mask){
        auto& selected_line = m_data[set_idx][way_idx];
        for(int i = 0;i<NLANE;++i){
            if(lane_mask[i]==true){//在硬件中，这里是offset矩阵转置的独热码
                selected_line[block_offset[i]] = hit_data[i];
            }
        }
    }

    //仅用于write miss under read miss的release
    void write_hit(u_int32_t set_idx,u_int32_t way_idx, 
        cache_line_t data, std::array<bool,LINEWORDS> mask){
        auto& selected_line = m_data[set_idx][way_idx];
        for(int i = 0;i<LINEWORDS;++i){
            if(mask[i]==true){
                selected_line[i] = data[i];
            }
        }
    }

    void DEBUG_visualize_array(u_int32_t set_idx_start=0, u_int32_t set_idx_end=NSET-1){
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

    void DEBUG_print_a_way(u_int32_t set_idx, u_int32_t way_idx){
        auto& the_one = m_data[set_idx][way_idx];
        for(const auto& word : the_one){
            std::cout << word << ",";
        }
        std::cout << std::endl;
    }

private:
    std::array<std::array<cache_line_t, NWAY>,NSET> m_data;
    cache_line_t m_read_data_o_r;
};
#endif