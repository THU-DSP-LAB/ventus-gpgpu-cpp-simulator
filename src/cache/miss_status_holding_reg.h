#ifndef MISS_STATUS_HOLDING_REG_H
#define MISS_STATUS_HOLDING_REG_H

#include <map>
#include <deque>

#include "utils.h"
#include "parameter.h"
#include "interfaces.h"

enum entry_target_type{
    REGULAR_READ_MISS,
    LOAD_RESRV,
    STORE_COND,
    AMO
};

enum vec_mshr_status{
    PRIMARY_AVAIL,
    PRIMARY_FULL,
    SECONDARY_AVAIL,
    SECONDARY_FULL,
    SECONDARY_FULL_RETURN//仅用于cReq_st1，不作为probe返回结果
};
//出现SECONDARY_FULL_RETURN时表示当前被阻塞的cReq_st1将从mRsp_st1寄存器完成cRsp

enum spe_mshr_status{
    AVAIL,
    FULL
};

class vec_subentry : public cache_building_block{
public:
    vec_subentry(){}

    vec_subentry(u_int32_t req_id, u_int32_t wid, std::array<bool,NLANE> mask,
    vec_nlane_t block_offset)//,vec_nlane_t word_offset)
    :m_req_id(req_id), m_wid(wid), m_mask(mask), m_block_offset(block_offset){}//,
    // m_word_offset(word_offset){}
private:
    u_int32_t m_req_id;
    u_int32_t m_wid;

    std::array<bool,NLANE> m_mask;
    vec_nlane_t m_block_offset;
    //vec_nlane_t m_word_offset;

friend class special_target_info;
friend class vec_entry_target_info;
friend class mshr;
};

class vec_entry_target_info : public cache_building_block{
public:
    vec_entry_target_info(){}

    vec_entry_target_info(u_int32_t req_id, vec_subentry sub): m_req_id(req_id){
        allocate_sub(sub);
    }

    bool sub_is_full(){
        assert(m_sub_en.size() <= N_MSHR_SUBENTRY);
        return m_sub_en.size() == N_MSHR_SUBENTRY;
    }

    void allocate_sub(const vec_subentry& sub){
        assert(!sub_is_full());
        m_sub_en.push_back(sub);
    }

    void deallocate_sub(){
        assert(!m_sub_en.empty());
        m_sub_en.pop_back();
    }

    void set_order_of_wm_within(){
        m_order_of_wm_within_if_rm = m_sub_en.size();
    }

private:
    //bool m_valid;
    //u_int32_t m_block_addr;

    //便于missRsp时索引，硬件上可能冗余
    u_int32_t m_req_id;
    //order of write miss within in flight read miss
    //如果不存在wm，该值大于sub容量，所以不会被触发
    //该值不等于0，等于1时表示在第一个sub被处理前先处理
    u_int32_t m_order_of_wm_within_if_rm = N_MSHR_SUBENTRY+1;
    std::deque<vec_subentry> m_sub_en;

    friend class mshr;
};

class special_target_info{
public:
    //MSHR中不记录amo类型，因为MSHR中记录的信息仅用于coreRsp，不再用于missReq
    //missReq相关职能移入memReq_Q
    special_target_info(){}

    special_target_info(enum entry_target_type type, u_int32_t wid):
        m_type(type),m_wid(wid){}

private:
    enum entry_target_type m_type;
    //u_int32_t m_req_id;作为speMSHR entry的索引了
    u_int32_t m_wid;
    //enum LSU_cache_coreReq_type_amo m_amo_type;
    //block_addr_t m_block_idx;
    
    friend class mshr;
};

class temp_write : public cache_building_block{
public:
    temp_write(){}
    
    temp_write(cache_line_t data, std::array<bool,LINEWORDS> mask):
    m_data(data), m_mask(mask){}
private:
    cache_line_t m_data;
    std::array<bool,LINEWORDS> m_mask;

    friend class mshr;
};

//本类的成员变量和missRsp_process的入参相同
class mshr_miss_rsp : public cache_building_block{
public:
    mshr_miss_rsp(){}

    mshr_miss_rsp(enum entry_target_type type, u_int32_t req_id, 
        block_addr_t block_idx):m_type(type), m_req_id(req_id), m_block_idx(block_idx){}

    enum entry_target_type m_type;
    u_int32_t m_req_id;
    block_addr_t m_block_idx;

friend class mshr;
};

class mshr : public cache_building_block{
public:
    mshr(){}

    //from special entry
    void special_arrange_core_rsp(coreRsp_pipe_reg& pipe_reg, u_int32_t req_id){
        assert(!pipe_reg.is_valid());
        auto& the_entry = m_special_entry[req_id];
        std::array<bool,NLANE> mask_of_scalar = {true };
        vec_nlane_t data{0};//无意义，cRsp此时不需要数据
        dcache_2_LSU_coreRsp new_rsp = dcache_2_LSU_coreRsp(
            req_id,data,the_entry.m_wid,mask_of_scalar);
        pipe_reg.update_with(new_rsp);
        //AMO,LR,SC都不引起data access写入。
        //AMO和LR向coreRsp.data写回数据
        //SC成功向coreRsp.data写0，失败写1
        //coreRsp.data的具体内容在本模型中不会体现
        m_special_entry.erase(req_id);
        return;
    }

    //每次调用处理一个subentry。当前main entry清空时，返回true。
    bool vec_arrange_core_rsp(coreRsp_pipe_reg& pipe_reg, block_addr_t block_idx, cache_line_t& missRsp_line){
        auto& current_main = m_vec_entry[block_idx].m_sub_en;
        assert(!current_main.empty());
        auto& wm_order = m_vec_entry[block_idx].m_order_of_wm_within_if_rm;
        if(wm_order == 0){
            m_deal_with_wm = true;
            wm_order--;
            return false;
        }else{
            wm_order--;
            assert(!pipe_reg.is_valid());
            auto& current_sub = current_main.front();
            vec_nlane_t coreRsp_data;
            for(int i = 0;i<NLANE;++i){
                if(current_sub.m_mask[i]==true){//mem order to core order crossbar
                    coreRsp_data[i] = missRsp_line[current_sub.m_block_offset[i]];
                }
            }
            dcache_2_LSU_coreRsp new_rsp = dcache_2_LSU_coreRsp(
                current_sub.m_req_id,
                coreRsp_data,
                current_sub.m_wid,
                current_sub.m_mask);
            pipe_reg.update_with(new_rsp);
            current_main.pop_front();
        }

        if (current_main.empty()){
            //为了让此时阻塞的coreReq可以进行
            if(m_vec_probe_status_reg == PRIMARY_FULL){
                m_vec_probe_status_reg = PRIMARY_AVAIL;
            }else if(m_vec_probe_status_reg == SECONDARY_FULL){
                m_vec_probe_status_reg = SECONDARY_FULL_RETURN;
            }
            m_vec_entry.erase(block_idx);
            wm_order = N_MSHR_SUBENTRY+1;
            return true;
        }
        
        return false;
    }

    bool has_secondary_full_return(){
        return m_vec_probe_status_reg == SECONDARY_FULL_RETURN;
    }

    void probe_vec_in(block_addr_t block_idx){
        if(is_primary_miss(block_idx)){
            assert(m_vec_entry.size() <= N_MSHR_ENTRY);
            if(m_vec_entry.size() == N_MSHR_ENTRY){
                m_vec_probe_status_reg = PRIMARY_FULL;
                //std::cout << "primary miss + main entry full at " << time << std::endl;//TODO: 分级debug info机制
            }else{
                m_vec_probe_status_reg = PRIMARY_AVAIL;
            }
        }else{
            assert(m_vec_entry.size() > 0);
            auto& the_main = m_vec_entry[block_idx];
            if (the_main.sub_is_full()){
                m_vec_probe_status_reg = SECONDARY_FULL;
                //std::cout << "secondary miss + sub entry full at " << time << std::endl;
            }else{
                m_vec_probe_status_reg = SECONDARY_AVAIL;
            }
        }
    }

    enum vec_mshr_status probe_vec_out(){
        return m_vec_probe_status_reg;
    }

    //memReq_Q 发射Wm之前检查是否有相同的Rm
    bool w_s_protection_check(block_addr_t block_idx){
        return !is_primary_miss(block_idx);
    }

    void probe_spe_in(bool is_store_conditional){
        assert(m_special_entry.size() <= N_MSHR_SPECIAL_ENTRY);
        if(is_store_conditional){
            for(auto iter = m_special_entry.begin(); iter != m_special_entry.end();++iter){
                if(iter->second.m_type == LOAD_RESRV)
                    m_spe_probe_status_reg = FULL;
            }
        }
        if(m_special_entry.size() == N_MSHR_SPECIAL_ENTRY){
            m_spe_probe_status_reg = FULL;
            //std::cout << "LR/SC AMO + special entry full at " << time << std::endl;
        }else{
            m_spe_probe_status_reg = AVAIL;
        }
    }

    enum spe_mshr_status probe_spe_out(){
        return m_spe_probe_status_reg;
    }

    void allocate_vec_main(block_addr_t block_idx, vec_subentry& vec_sub){
        vec_entry_target_info new_main = vec_entry_target_info(vec_sub.m_req_id,vec_sub);
        m_vec_entry.insert({block_idx,new_main});//deep copy?
    }

    void allocate_vec_sub(block_addr_t block_idx, vec_subentry& vec_sub){
        auto& the_main = m_vec_entry[block_idx];
        the_main.allocate_sub(vec_sub);
    }

    void allocate_special(enum entry_target_type type, 
        u_int32_t req_id, u_int32_t wid){
        special_target_info new_special;
        new_special = special_target_info(type, wid);
        m_special_entry.insert({req_id, new_special});
    }

    bool is_primary_miss(block_addr_t block_idx){
        //需要转换MSHR存储类型时（reg/SRAM）可以从这里着手考虑
        for (auto iter = m_vec_entry.begin(); iter != m_vec_entry.end(); ++iter) {
            if (iter->first == block_idx)
                return false;
        }
        return true;
    }

    enum entry_target_type detect_missRsp_type(block_addr_t& block_idx, u_int32_t req_id){
        auto spe_iter = m_special_entry.find(req_id);
        if(spe_iter != m_special_entry.end()){//LR/SC/AMO
            return spe_iter->second.m_type;
        }else{//confirm regular miss
            for (auto vec_iter = m_vec_entry.begin(); vec_iter != m_vec_entry.end(); ++vec_iter) {
                if(req_id == vec_iter->second.m_req_id){
                    block_idx = vec_iter->first;
                    return REGULAR_READ_MISS;
                }
            }
            assert(false && "missRsp no entry in mshr" );
        }
    }

    bool current_main_0_sub(block_addr_t block_idx){
        return m_vec_entry[block_idx].m_sub_en.size() == 0;
    }

    bool empty(){
        return m_vec_entry.empty();
    }

    bool has_protect_to_release(){
        return m_deal_with_wm;
    }

    void release_wm(){
        m_deal_with_wm = false;
    }

    bool write_under_miss_full(){
        return m_write_under_readmiss.size() == N_MSHR_WRITE_UNDER_READ_MISS;
    }

    void push_write_under_readmiss(block_addr_t block_addr, temp_write write_miss_data){
        //write_miss_data already in mem order
        assert(!write_under_miss_full());
        m_write_under_readmiss.insert({block_addr,write_miss_data});
        m_vec_entry[block_addr].set_order_of_wm_within();
    }

    void pop_write_under_readmiss(const block_addr_t block_addr, cache_line_t& write_data, std::array<bool,LINEWORDS>& write_mask){
        auto& content = m_write_under_readmiss[block_addr];
        write_data = content.m_data;
        write_mask = content.m_mask;
        m_write_under_readmiss.erase(block_addr);
    }

    void DEBUG_visualize_array(){
        DEBUG_print_title();
        if (m_vec_entry.size()==0){
            std::cout << "MSHR is empty" << std::endl;
        }else{
            for (const auto& main_entry : m_vec_entry){   
                std::cout << std::setw(9) << main_entry.first << " |";
                std::cout << std::setw(3) << main_entry.second.m_req_id << " |";
                std::cout << std::setw(2) << main_entry.second.m_sub_en.size() << std::endl;
            }
        }
    }

    void DEBUG_print_title(){
        std::cout << "block_addr | id | sub_count " << std::endl;
    }
    
    private:

    std::map<block_addr_t,vec_entry_target_info> m_vec_entry;
    std::map<uint32_t,special_target_info> m_special_entry;
    //寄存器，置高时，在清空sub之后，开始处理cReq阻塞的sub full之前，先完成mReq Q里Wmiss的data array更新
    std::map<block_addr_t,temp_write> m_write_under_readmiss;
    bool m_deal_with_wm=false;
    enum vec_mshr_status m_vec_probe_status_reg;
    enum spe_mshr_status m_spe_probe_status_reg;
};

#endif