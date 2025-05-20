#include "BASE.h"
#include "../context_model.hpp"
#include "subcore.hpp"
#include <algorithm>
#include <fmt/core.h>
#include <functional>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>

BASE::BASE(
    sc_core::sc_module_name name, int _sm_id,
    const std::shared_ptr<const std::vector<instable_t>>& instruction_table,
    const std::shared_ptr<const std::map<OP_TYPE, decodedat>>& decode_table,
    std::shared_ptr<PhysicalMemoryInterface> gmem, mem_interface_t memif,
    std::shared_ptr<spdlog::logger> logger
)
    : sc_module(name)
    , sm_id(_sm_id)
    , m_mmu(gmem, logger)
    , l1d_request(memif)
    , m_logger(logger ? logger : spdlog::default_logger()) {

    for (int i = 0; i < m_subcores.size(); i++) {
        std::string subcore_name = fmt::format("{}_Subcore{}", name, i);
        m_subcores[i] = std::make_unique<Subcore>(
            subcore_name.c_str(), sm_id, i, instruction_table, decode_table,
            [this](
                bool valid, uint8_t subcore_id, uint8_t subcore_warp_id, I_TYPE instr,
                vaddr_t pds_base, paddr_t pagetable_root,
                std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data1,
                std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data2,
                std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data3
            ) {
                return lsu_subcore_req(
                    valid, subcore_id, subcore_warp_id, instr, pds_base, pagetable_root, src_data1,
                    src_data2, src_data3
                );
            },
            [this, i](int subcore_warp_id, int blk_slot_id, int warp_id_in_blk, vaddr_t pc) {
                warp_reach_barrier(i, subcore_warp_id, blk_slot_id, warp_id_in_blk, pc);
            },
            [this, i](int subcore_warp_id, int blk_slot_id, int warp_id_in_blk) {
                warp_endprg(i, subcore_warp_id, blk_slot_id, warp_id_in_blk);
            },
            m_mmu, m_logger
        );
        m_subcores[i]->clk(clk);
        m_subcores[i]->rst_n(rst_n);
    }

    SC_HAS_PROCESS(BASE);
    SC_THREAD(lsu_main);
}

int BASE::lsu_subcore_req(
    bool valid, uint32_t subcore_id, uint32_t subcore_warp_id, I_TYPE instr, vaddr_t pds_base,
    paddr_t pagetable_root, std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data1,
    std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data2,
    std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data3
) {
    assert(subcore_id < SUBCORE_NUM);
    assert(subcore_warp_id < SUBCORE_WARP_NUM);

    auto mshr_usage = std::count_if(
                          m_lsu_mshr.begin(), m_lsu_mshr.end(),
                          [](const lsu_mshr_t& mshr) { return mshr.valid; }
                      )
        + std::any_of(std::begin(emito_lsu), std::end(emito_lsu),
                      [](const sc_signal<bool>& sig) { return sig.read() == true; });

    if (mshr_usage >= m_lsu_mshr.size()) {
        emito_lsu[subcore_id].write(false);
        return -1; // mshr full, do not accept any request
    }

    if (valid) {
        lsu_subcore_req_valid[subcore_id] = true;
    }

    lsu_subcore_req_updated[subcore_id] = true;
    if (lsu_subcore_req_updated.all()) {
        if (lsu_subcore_req_valid.any()) {
            for (int i = 0; i < lsu_subcore_req_valid.size(); i++) { // round-robin arbiter
                int idx = (i + lsu_subcore_req_arbiter_last + 1) % lsu_subcore_req_valid.size();
                if (lsu_subcore_req_valid[idx]) {
                    lsu_subcore_req_arbiter_last = idx;
                    break;
                }
            }
            ev_lsu_subcore_req_all_updated.notify();
        }
        lsu_subcore_req_updated.reset();
    } else if (valid) {
        wait(ev_lsu_subcore_req_all_updated);
    }

    if (valid && subcore_id == lsu_subcore_req_arbiter_last) { // the selected requesting subcore
        m_lsu_subcore_req_queue.emplace(lsu_subcore_req_t { .subcore_id = subcore_id,
                                                            .subcore_warp_id = subcore_warp_id,
                                                            .instr = instr,
                                                            .pds_base = pds_base,
                                                            .pagetable_root = pagetable_root,
                                                            .src_data1 = std::move(src_data1),
                                                            .src_data2 = std::move(src_data2),
                                                            .src_data3 = std::move(src_data3) });
        emito_lsu[subcore_id].write(true);
        lsu_subcore_req_valid.reset();
        return 0;
    }
    emito_lsu[subcore_id].write(false);
    return -1;
}

void BASE::warp_reach_barrier(
    int subcore_id, int subcore_warp_id, int blk_slot_id, int warp_idx_in_blk, vaddr_t pc
) {
    auto hwarp_id = warpid_convert(subcore_id, subcore_warp_id);
    auto& hblkslot = m_block_slots.at(blk_slot_id);
    assert(hblkslot.valid);
    assert(hblkslot.num_warp > 0 && hblkslot.hw_warp_running[hwarp_id]);
    if (hblkslot.num_warp == 1) {
        return; // do not barrier
    } else if (std::all_of(
                   hblkslot.warp_reach_barrier.begin(),
                   hblkslot.warp_reach_barrier.begin() + hblkslot.num_warp,
                   [](bool i) { return i == false; }
               )) {
        // this is the first warp of this block that reaches barrier
        hblkslot.warp_reach_barrier[warp_idx_in_blk] = true;
        hblkslot.barrier_addr = pc;
        SPDLOG_LOGGER_TRACE(
            m_logger, "SM {} warp {} BARRIER reached at 0x{:x}", sm_id, hwarp_id, pc
        );
        m_subcores[subcore_id]->warp_barrier_set(subcore_warp_id, true);
        return;
    } else {
        // this is not the first warp of this block that reaches barrier
        if (hblkslot.barrier_addr != pc) {
            SPDLOG_LOGGER_ERROR(
                m_logger, "SM {} warp {} BARRIER address mismatch, expect pc=0x{:x} but pc=0x{:x}",
                sm_id, hwarp_id, hblkslot.barrier_addr, pc
            );
        }
        assert(pc == hblkslot.barrier_addr);
        hblkslot.warp_reach_barrier[warp_idx_in_blk] = true;
        m_subcores[subcore_id]->warp_barrier_set(subcore_warp_id, true);
        if (std::all_of(
                hblkslot.warp_reach_barrier.begin(),
                hblkslot.warp_reach_barrier.begin() + hblkslot.num_warp,
                [](bool i) { return i == true; }
            )) {
            // all warps of this block reach barrier
            SPDLOG_LOGGER_TRACE(
                m_logger, "SM {} warp scheduler: all warps of blkslot {} reach barrier pc=0x{:x}",
                sm_id, blk_slot_id, pc
            );
            // reset barrier
            hblkslot.warp_reach_barrier.fill(false);
            for (int hwarp_idx_ = 0; hwarp_idx_ < hblkslot.hw_warp_running.size(); hwarp_idx_++) {
                // release all hardware warps running this block
                if (hblkslot.hw_warp_running[hwarp_idx_]) {
                    auto [subcore_idx_, subcore_warp_idx_] = warpid_convert(hwarp_idx_);
                    m_subcores[subcore_idx_]->warp_barrier_set(subcore_warp_idx_, false);
                }
            }
        }
    }
}

void BASE::warp_endprg(int subcore_id, int subcore_warp_id, int blk_slot_id, int warp_idx_in_blk) {
    auto hwarp_id = warpid_convert(subcore_id, subcore_warp_id);
    auto& hblkslot = m_block_slots.at(blk_slot_id);
    assert(hblkslot.valid);
    assert(hblkslot.num_warp > 0 && hblkslot.hw_warp_running[hwarp_id]);
    if (m_warp_finish_callback) { // callback CTA Scheduler, return the finished warp
        m_warp_finish_callback(sm_id, blk_slot_id, warp_idx_in_blk);
    }
    // clear the block slot
    assert(hblkslot.num_warp > 0);
    assert(hblkslot.hw_warp_running[hwarp_id]);
    hblkslot.num_warp--;
    hblkslot.hw_warp_running[hwarp_id] = false;
    if (hblkslot.num_warp == 0) {
        // the last running warp of this block returns, reset its block_slot
        assert(std::all_of(
            hblkslot.hw_warp_running.begin(), hblkslot.hw_warp_running.end(),
            [](bool i) { return i == false; }
        ));
        hblkslot.valid = false;
        hblkslot.warp_reach_barrier.fill(false);
    }
}

// SM receive new block
void BASE::receive_warp(
    uint32_t blk_idx_in_kernel, uint32_t warp_idx_in_blk, std::shared_ptr<kernel_info_t> kernel,
    uint32_t blk_slot_idx, uint32_t lds_baseaddr
) {
    assert(kernel);
    assert(kernel->get_num_thread_per_warp() <= hw_num_thread);
    assert(blk_idx_in_kernel == kernel->get_next_cta_id_single());

    // Find a idle hardware warp
    WARP_BONE* hwarp = nullptr; // hardware warp
    uint32_t hw_warp_idx = 0xFFFFFFFF;
    for (uint32_t i = 0; i < hw_num_warp; i++) {
        uint32_t idx = (i + m_last_activated_hardware_warp_id + 1) % hw_num_warp; // round-robin
        auto [subcore_idx, subcore_warp_idx] = warpid_convert(idx);
        if (m_subcores.at(subcore_idx)->is_warp_idle(subcore_warp_idx)) {
            hw_warp_idx = idx;
            break;
        }
    }
    // should always find a idle warp, as CTA scheduler has checked warp_slot before
    assert(hw_warp_idx != 0xFFFFFFFF);
    m_last_activated_hardware_warp_id = hw_warp_idx;
    auto [subcore_idx, subcore_warp_idx] = warpid_convert(hw_warp_idx);

    // 线程束信息写入硬件warp
    m_subcores[subcore_idx]->receive_warp(
        blk_idx_in_kernel, warp_idx_in_blk, kernel, lds_baseaddr, blk_slot_idx, subcore_warp_idx
    );

    // 将线程块信息写入block slot（仅对于此块的首个线程束）
    auto& hblkslot = m_block_slots[blk_slot_idx];
    if (hblkslot.valid == false) {
        hblkslot.valid = true;
        hblkslot.num_warp = 0;
        hblkslot.warp_reach_barrier.fill(false);
        hblkslot.hw_warp_running.fill(false);
    }
    hblkslot.hw_warp_running[hw_warp_idx] = true;
    hblkslot.num_warp++;

    SPDLOG_LOGGER_TRACE(
        m_logger, "SM {} warp {} receive kernel {} {} block {} warp {}", sm_id, hw_warp_idx,
        kernel->get_kid(), kernel->get_kname(), blk_idx_in_kernel, warp_idx_in_blk
    );
}

void increment_x_then_y_then_z(dim3& i, const dim3& bound) {
    i.x++;
    if (i.x >= bound.x) {
        i.x = 0;
        i.y++;
        if (i.y >= bound.y) {
            i.y = 0;
            if (i.z < bound.z)
                i.z++;
        }
    }
}
