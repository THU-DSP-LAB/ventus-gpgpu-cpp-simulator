#ifndef TAG_ARRAY_H
#define TAG_ARRAY_H

#include <iostream>
#include <iomanip>
#include <cmath>
#include <array>
#include <deque>
#include <cassert>
#include <bitset>

#include "utils.h"
#include "parameter.h"
#include "interfaces.h"

enum tag_access_status {
    HIT=1,
    MISS=0
};

class meta_entry_t : cache_building_block{
public:
    meta_entry_t(){
        m_valid = false;
        m_dirty = false;
        m_last_access_time = 0;
        m_last_fill_time = 0;
    }

    u_int32_t tag(){
        return m_tag;
    }

    bool is_valid(){
        return m_valid;
    }

    bool is_dirty(){
        return m_dirty;
    }

    bool is_hit(u_int32_t tag){
        return is_valid() && m_tag == tag;
    }

    void invalidate(){
        m_valid = false;
    }

    void clear_dirty(){
        m_dirty = false;
    }

    void allocate(u_int32_t tag){
        m_tag = tag;
        m_valid = true;
        m_dirty = false;
    }

    void write_dirty(){
        assert(m_valid);
        m_dirty = true;
    }

    void update_access_time(cycle_t new_time){
        m_last_access_time = new_time;
    }
    
    void update_fill_time(cycle_t new_time){
        m_last_fill_time = new_time;
    }

    unsigned int get_access_time(){
        return m_last_access_time;
    }

    unsigned int get_fill_time(){
        return m_last_fill_time;
    }
    
private:
    bool m_valid;
    bool m_dirty;

    u_int32_t m_tag;
    unsigned int m_last_access_time;
    unsigned int m_last_fill_time;

    friend class tag_array;
};

class tag_array : cache_building_block {
public:
    tag_array(){}

    void probe_in(u_int32_t block_idx);

    enum tag_access_status probe_out(u_int32_t& way_idx);

    void read_hit_update_access_time(u_int32_t way_idx, u_int32_t set_idx,cycle_t time);

    void write_hit_mark_dirty(u_int32_t way_idx, u_int32_t set_idx,cycle_t time);

    bool need_replace(u_int32_t& tag_replacement, u_int32_t& way_replacement, u_int32_t block_idx);

    void allocate(u_int32_t block_idx,u_int32_t way_idx,cycle_t time);

    void invalidate_chosen(u_int32_t set_idx,u_int32_t way_idx);

    void invalidate_all();

    bool line_is_dirty(u_int32_t block_idx, u_int32_t& way_idx);

    bool has_dirty(u_int32_t& tag_replacement, u_int32_t& set_idx, u_int32_t& way_idx);

    //返回值为0时说明当前周期由于memReq_Q阻塞而写入失败
    void flush_one(u_int32_t set_idx, u_int32_t way_idx);
    
    /*get the to-be-replaced way in a given set
    return: u_int32_t way_idx
    input u_int32_t set_idx
    */
    u_int32_t replace_choice(u_int32_t set_idx);
    
    //返回值为0时说明当前周期由于memReq_Q阻塞而写入失败
    //bool issue_memReq_write(memReq_Q& memReq_Q, meta_entry_t& line_to_issue, u_int32_t set_idx);

    //for test use only
    void DEBUG_random_initialize(cycle_t time);

    //for test use only
    void DEBUG_visualize_array(u_int32_t set_idx_start=0, u_int32_t set_idx_end=NSET);

    void DEBUG_print_title();

    void DEBUG_print_a_way(u_int32_t set_idx, u_int32_t way_idx);

private:
    std::array<std::array<meta_entry_t, NWAY>,NSET> m_tag;
    enum tag_access_status m_probe_result_reg;
    u_int32_t m_way_hit_reg;
};

#endif