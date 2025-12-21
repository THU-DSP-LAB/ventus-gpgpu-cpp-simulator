#ifndef MISS_STATUS_HOLDING_REG_H
#define MISS_STATUS_HOLDING_REG_H

#include <deque>
#include <map>

#include "interfaces.h"
#include "parameter.h"
#include "utils.h"

enum entry_target_type { REGULAR_READ_MISS, LOAD_RESRV, STORE_COND, AMO };

enum vec_mshr_status {
    PRIMARY_AVAIL,
    PRIMARY_FULL,
    SECONDARY_AVAIL,
    SECONDARY_FULL,
    SECONDARY_FULL_RETURN // 仅用于cReq_st1，不作为probe返回结果
};
// 出现SECONDARY_FULL_RETURN时表示当前被阻塞的cReq_st1将从mRsp_st1寄存器完成cRsp

enum spe_mshr_status { AVAIL, FULL };

class vec_subentry : public cache_building_block {
public:
    vec_subentry() { }

    vec_subentry(
        uint32_t req_id, uint32_t wid, uint32_t l1id, uint32_t pagetable_root, uint8_t instrId,
        uint32_t pc, std::array<bool, NLANE> mask, vec_nlane_t block_offset
    ) //,vec_nlane_t word_offset)
        : m_req_id(req_id)
        , m_wid(wid)
        , m_l1id(l1id)
        , m_pagetable_root(pagetable_root)
        , m_instrId(instrId)
        , m_pc(pc)
        , m_mask(mask)
        , m_block_offset(block_offset) { } //,
    // m_word_offset(word_offset){}
private:
    uint32_t m_req_id;
    uint32_t m_wid;
    uint32_t m_l1id;
    uint32_t m_pagetable_root;
    uint8_t m_instrId;
    uint32_t m_pc;
    std::array<bool, NLANE> m_mask;
    vec_nlane_t m_block_offset;
    // vec_nlane_t m_word_offset;

    friend class special_target_info;
    friend class vec_entry_target_info;
    friend class mshr;
};

class vec_entry_target_info : public cache_building_block {
public:
    vec_entry_target_info() { }

    vec_entry_target_info(uint32_t req_id, vec_subentry sub)
        : m_req_id(req_id) {
        allocate_sub(sub);
    }

    bool sub_is_full() {
        assert(m_sub_en.size() <= N_MSHR_SUBENTRY);
        return m_sub_en.size() == N_MSHR_SUBENTRY;
    }

    void allocate_sub(const vec_subentry& sub) {
        assert(!sub_is_full());
        m_sub_en.push_back(sub);
    }

    void deallocate_sub() {
        assert(!m_sub_en.empty());
        m_sub_en.pop_back();
    }

    void set_order_of_wm_within() { m_order_of_wm_within_if_rm = m_sub_en.size(); }

private:
    // bool m_valid;
    // uint32_t m_block_addr;

    // 便于missRsp时索引，硬件上可能冗余
    uint32_t m_req_id;
    // order of write miss within in flight read miss
    // 如果不存在wm，该值大于sub容量，所以不会被触发
    // 该值不等于0，等于1时表示在第一个sub被处理前先处理
    uint32_t m_order_of_wm_within_if_rm = N_MSHR_SUBENTRY + 1;
    std::deque<vec_subentry> m_sub_en;

    friend class mshr;
};

class special_target_info {
public:
    // MSHR中不记录amo类型，因为MSHR中记录的信息仅用于coreRsp，不再用于missReq
    // missReq相关职能移入memReq_Q
    special_target_info() { }

    special_target_info(enum entry_target_type type, uint32_t wid)
        : m_type(type)
        , m_wid(wid) { }

private:
    enum entry_target_type m_type;
    // uint32_t m_req_id;作为speMSHR entry的索引了
    uint32_t m_wid;
    uint32_t m_l1id;
    uint32_t m_pagetable_root;
    uint8_t m_instrId;
    uint32_t m_pc;
    // enum LSU_cache_coreReq_type_amo m_amo_type;
    // block_addr_t m_block_idx;

    friend class mshr;
};

class temp_write : public cache_building_block {
public:
    temp_write() { }

    temp_write(std::array<uint32_t, hw_num_thread> data, std::array<bool, LINEWORDS> mask)
        : m_data(data)
        , m_mask(mask) { }

private:
    std::array<uint32_t, hw_num_thread> m_data;
    std::array<bool, LINEWORDS> m_mask;

    friend class mshr;
};

// 本类的成员变量和missRsp_process的入参相同
class mshr_miss_rsp : public cache_building_block {
public:
    mshr_miss_rsp() { }

    mshr_miss_rsp(
        enum entry_target_type type, uint32_t req_id, uint32_t l1id, uint8_t instrId, uint32_t pc,
        block_addr_t block_idx
    )
        : m_type(type)
        , m_req_id(req_id)
        , m_l1id(l1id)
        , m_pagetable_root(m_pagetable_root)
        , m_block_idx(block_idx) { }

    enum entry_target_type m_type;
    uint32_t m_req_id;
    uint32_t m_l1id;
    uint32_t m_pagetable_root;
    uint8_t m_instrId;
    uint32_t m_pc;
    block_addr_t m_block_idx;

    friend class mshr;
};

class mshr : public cache_building_block {
public:
    mshr() { }

    // from special entry
    void special_arrange_core_rsp(coreRsp_pipe_reg& pipe_reg, uint32_t req_id);

    // 每次调用处理一个subentry。当前main entry清空时，返回true。
    bool vec_arrange_core_rsp(
        coreRsp_pipe_reg& pipe_reg, block_addr_t block_idx,
        std::array<uint32_t, hw_num_thread>& missRsp_line
    );

    bool has_secondary_full_return();

    void probe_vec_in(block_addr_t block_idx);

    enum vec_mshr_status probe_vec_out();

    // memReq_Q 发射Wm之前检查是否有相同的Rm
    bool w_s_protection_check(block_addr_t block_idx);

    void probe_spe_in(bool is_store_conditional);

    enum spe_mshr_status probe_spe_out();

    void allocate_vec_main(block_addr_t block_idx, vec_subentry& vec_sub);

    void allocate_vec_sub(block_addr_t block_idx, vec_subentry& vec_sub);

    void allocate_special(enum entry_target_type type, uint32_t req_id, uint32_t wid);

    bool is_primary_miss(block_addr_t block_idx);

    enum entry_target_type detect_missRsp_type(block_addr_t& block_idx, uint32_t req_id);

    bool current_main_0_sub(block_addr_t block_idx);

    bool empty();

    bool has_protect_to_release();

    void release_wm();

    bool write_under_miss_full();

    void push_write_under_readmiss(block_addr_t block_addr, temp_write write_miss_data);

    void pop_write_under_readmiss(
        const block_addr_t block_addr, std::array<uint32_t, hw_num_thread>& write_data,
        std::array<bool, LINEWORDS>& write_mask
    );

    void DEBUG_visualize_array();

    void DEBUG_print_title();

private:
    std::map<block_addr_t, vec_entry_target_info> m_vec_entry;
    std::map<uint32_t, special_target_info> m_special_entry;
    // 寄存器，置高时，在清空sub之后，开始处理cReq阻塞的sub full之前，先完成mReq Q里Wmiss的data
    // array更新
    std::map<block_addr_t, temp_write> m_write_under_readmiss;
    bool m_deal_with_wm = false;
    enum vec_mshr_status m_vec_probe_status_reg;
    enum spe_mshr_status m_spe_probe_status_reg;
};

#endif