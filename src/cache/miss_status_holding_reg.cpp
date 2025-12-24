#include "miss_status_holding_reg.h"

// from special entry
void mshr::special_arrange_core_rsp(coreRsp_pipe_reg& pipe_reg, uint32_t req_id) {
    assert(!pipe_reg.is_valid());
    auto& the_entry = m_special_entry[req_id];
    std::array<bool, NLANE> mask_of_scalar = { true };
    vec_nlane_t data;
    data = pipe_reg.m_data;
    dcache_2_LSU_coreRsp new_rsp = dcache_2_LSU_coreRsp(
        req_id, data, the_entry.m_l1id, the_entry.m_pagetable_root, the_entry.m_instrId,
        the_entry.m_pc, the_entry.m_wid, mask_of_scalar
    );
    pipe_reg.update_with(new_rsp);
    // AMO,LR,SC都不引起data access写入。
    // AMO和LR向coreRsp.data写回数据
    // SC成功向coreRsp.data写0，失败写1
    // coreRsp.data的具体内容在本模型中不会体现
    m_special_entry.erase(req_id);
    return;
}

// 每次调用处理一个subentry。当前main entry清空时，返回true。
bool mshr::vec_arrange_core_rsp(
    coreRsp_pipe_reg& pipe_reg, block_addr_t block_idx,
    std::array<uint32_t, hw_num_thread>& missRsp_line
) {
    auto& current_main = m_vec_entry[block_idx].m_sub_en;
    assert(!current_main.empty());
    auto& wm_order = m_vec_entry[block_idx].m_order_of_wm_within_if_rm;
    if (wm_order == 0) {
        m_deal_with_wm = true;
        wm_order--;
        return false;
    } else {
        wm_order--;
        assert(!pipe_reg.is_valid());
        auto& current_sub = current_main.front();
        vec_nlane_t coreRsp_data;
        // Debug: Check data for traced instructions
        if (std::find(trace_pcs.begin(), trace_pcs.end(), current_sub.m_pc) != trace_pcs.end()) {
            std::cout << "[mshr::vec_arrange_core_rsp] TRACE @ pc=0x" << std::hex << current_sub.m_pc << std::dec << ": "
                      << "missRsp_line[0]=0x" << std::hex << missRsp_line[0]
                      << " missRsp_line[1]=0x" << missRsp_line[1] << " block_offset[0]=" << std::dec
                      << static_cast<int>(current_sub.m_block_offset[0]) << std::endl;
        }
        for (int i = 0; i < NLANE; ++i) {
            if (current_sub.m_mask[i] == true) { // mem order to core order crossbar
                coreRsp_data[i] = missRsp_line[current_sub.m_block_offset[i]];
                // Debug: Check data assignment (per-lane for vector)
                if (std::find(trace_pcs.begin(), trace_pcs.end(), current_sub.m_pc) != trace_pcs.end()) {
                    std::cout << "[mshr::vec_arrange_core_rsp] TRACE @ pc=0x" << std::hex << current_sub.m_pc << std::dec << ": "
                              << "lane=" << i
                              << " block_offset=" << static_cast<int>(current_sub.m_block_offset[i])
                              << " coreRsp_data[" << i << "]=0x" << std::hex << coreRsp_data[i]
                              << std::dec << std::endl;
                }
            }
        }
        dcache_2_LSU_coreRsp new_rsp = dcache_2_LSU_coreRsp(
            current_sub.m_req_id, coreRsp_data, current_sub.m_l1id, current_sub.m_pagetable_root,
            current_sub.m_instrId, current_sub.m_pc, current_sub.m_wid, current_sub.m_mask
        );
        pipe_reg.update_with(new_rsp);
        current_main.pop_front();
    }

    if (current_main.empty()) {
        // 为了让此时阻塞的coreReq可以进行
        if (m_vec_probe_status_reg == PRIMARY_FULL) {
            m_vec_probe_status_reg = PRIMARY_AVAIL;
        } else if (m_vec_probe_status_reg == SECONDARY_FULL) {
            m_vec_probe_status_reg = SECONDARY_FULL_RETURN;
        }
        m_vec_entry.erase(block_idx);
        wm_order = N_MSHR_SUBENTRY + 1;
        return true;
    }

    return false;
}

bool mshr::has_secondary_full_return() { return m_vec_probe_status_reg == SECONDARY_FULL_RETURN; }

void mshr::probe_vec_in(block_addr_t block_idx) {
    if (is_primary_miss(block_idx)) {
        assert(m_vec_entry.size() <= N_MSHR_ENTRY);
        if (m_vec_entry.size() == N_MSHR_ENTRY) {
            m_vec_probe_status_reg = PRIMARY_FULL;
            // std::cout << "primary miss + main entry full at " << time << std::endl;//TODO:
            // 分级debug info机制
        } else {
            m_vec_probe_status_reg = PRIMARY_AVAIL;
        }
    } else {
        assert(m_vec_entry.size() > 0);
        auto& the_main = m_vec_entry[block_idx];
        if (the_main.sub_is_full()) {
            m_vec_probe_status_reg = SECONDARY_FULL;
            // std::cout << "secondary miss + sub entry full at " << time << std::endl;
        } else {
            m_vec_probe_status_reg = SECONDARY_AVAIL;
        }
    }
}

enum vec_mshr_status mshr::probe_vec_out() { return m_vec_probe_status_reg; }

// memReq_Q 发射Wm之前检查是否有相同的Rm
bool mshr::w_s_protection_check(block_addr_t block_idx) { return !is_primary_miss(block_idx); }

void mshr::probe_spe_in(bool is_store_conditional) {
    assert(m_special_entry.size() <= N_MSHR_SPECIAL_ENTRY);
    if (is_store_conditional) {
        for (auto iter = m_special_entry.begin(); iter != m_special_entry.end(); ++iter) {
            if (iter->second.m_type == LOAD_RESRV)
                m_spe_probe_status_reg = FULL;
        }
    }
    if (m_special_entry.size() == N_MSHR_SPECIAL_ENTRY) {
        m_spe_probe_status_reg = FULL;
        // std::cout << "LR/SC AMO + special entry full at " << time << std::endl;
    } else {
        m_spe_probe_status_reg = AVAIL;
    }
}

enum spe_mshr_status mshr::probe_spe_out() { return m_spe_probe_status_reg; }

void mshr::allocate_vec_main(block_addr_t block_idx, vec_subentry& vec_sub) {
    vec_entry_target_info new_main = vec_entry_target_info(vec_sub.m_req_id, vec_sub);
    m_vec_entry.insert({ block_idx, new_main }); // deep copy?
}

void mshr::allocate_vec_sub(block_addr_t block_idx, vec_subentry& vec_sub) {
    auto& the_main = m_vec_entry[block_idx];
    the_main.allocate_sub(vec_sub);
}

void mshr::allocate_special(enum entry_target_type type, uint32_t req_id, uint32_t wid) {
    special_target_info new_special;
    new_special = special_target_info(type, wid);
    m_special_entry.insert({ req_id, new_special });
}

bool mshr::is_primary_miss(block_addr_t block_idx) {
    // 需要转换MSHR存储类型时（reg/SRAM）可以从这里着手考虑
    for (auto iter = m_vec_entry.begin(); iter != m_vec_entry.end(); ++iter) {
        if (iter->first == block_idx)
            return false;
    }
    return true;
}

enum entry_target_type mshr::detect_missRsp_type(block_addr_t& block_idx, uint32_t req_id) {
    auto spe_iter = m_special_entry.find(req_id);
    if (spe_iter != m_special_entry.end()) { // LR/SC/AMO
        return spe_iter->second.m_type;
    } else {                                 // confirm regular miss
        for (auto vec_iter = m_vec_entry.begin(); vec_iter != m_vec_entry.end(); ++vec_iter) {
            if (req_id == vec_iter->second.m_req_id) {
                block_idx = vec_iter->first;
                return REGULAR_READ_MISS;
            }
        }
        // Debug: Print available req_ids in MSHR
        std::cout << "[mshr::detect_missRsp_type] ERROR: req_id=" << req_id
                  << " not found in MSHR. Available req_ids: ";
        for (auto vec_iter = m_vec_entry.begin(); vec_iter != m_vec_entry.end(); ++vec_iter) {
            std::cout << vec_iter->second.m_req_id << " ";
        }
        std::cout << std::endl;
        assert(false && "missRsp no entry in mshr");
    }
}

// check if current main entry has no subentry
// normally, main entry with 0 subentry do not exist
// just check if main entry exists
bool mshr::current_main_0_sub(block_addr_t block_idx) {
    return m_vec_entry.find(block_idx) == m_vec_entry.end();
}

bool mshr::empty() { return m_vec_entry.empty(); }

bool mshr::has_protect_to_release() { return m_deal_with_wm; }

void mshr::release_wm() { m_deal_with_wm = false; }

bool mshr::write_under_miss_full() {
    return m_write_under_readmiss.size() == N_MSHR_WRITE_UNDER_READ_MISS;
}

void mshr::push_write_under_readmiss(block_addr_t block_addr, temp_write write_miss_data) {
    // write_miss_data already in mem order
    assert(!write_under_miss_full());
    m_write_under_readmiss.insert({ block_addr, write_miss_data });
    m_vec_entry[block_addr].set_order_of_wm_within();
}

void mshr::pop_write_under_readmiss(
    const block_addr_t block_addr, std::array<uint32_t, hw_num_thread>& write_data,
    std::array<bool, LINEWORDS>& write_mask
) {
    auto& content = m_write_under_readmiss[block_addr];
    write_data = content.m_data;
    write_mask = content.m_mask;
    m_write_under_readmiss.erase(block_addr);
}

void mshr::DEBUG_visualize_array() {
    DEBUG_print_title();
    if (m_vec_entry.size() == 0) {
        std::cout << "MSHR is empty" << std::endl;
    } else {
        for (const auto& main_entry : m_vec_entry) {
            std::cout << std::setw(9) << main_entry.first << " |";
            std::cout << std::setw(3) << main_entry.second.m_req_id << " |";
            std::cout << std::setw(2) << main_entry.second.m_sub_en.size() << std::endl;
        }
    }
}

void mshr::DEBUG_print_title() { std::cout << "block_addr | id | sub_count " << std::endl; }
