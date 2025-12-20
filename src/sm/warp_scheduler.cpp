#include "subcore.hpp"
#include <algorithm>
#include <spdlog/spdlog.h>

void Subcore::WARP_SCHEDULER() {
    bool find_dispatchwarp = 0;
    last_dispatch_warpid = 0;
    I_TYPE _newissueins;
    uint32_t wait_barrier_ins_pc;
    I_TYPE new_ins; // from opc, barrier ins
    int new_ins_warpid;
    bool end_this_kernel;
    bool reset_endprg_flush_pipe[hw_num_warp] = { false };
    while (true) {
        wait(clk.posedge_event());

        for (int warpidx = 0; warpidx < m_hw_warps.size(); warpidx++) {
            auto& hwarp = m_hw_warps[warpidx];
            if (hwarp->endprg_flush_pipe) { // a warp endprg && flush_pipe finished
                hwarp->endprg_flush_pipe.write(false);
                hwarp->will_warp_activate = false;
                wait_barrier[warpidx].write(false);
                // clear block_slot & callback to CTA scheduler
                f_warp_endprg(warpidx, hwarp->blk_slot_idx, hwarp->warp_idx_in_blk);
            }
        }

        ev_warp_assigned.notify();

        // barrier from opc

        if (emito_warpscheduler) {
            // std::cout << "SM" << sm_id << " WARP SCHEDULER receive emit ins" << emit_ins << ",
            // warpid=" << emitins_warpid << " at " << sc_time_stamp() << "," <<
            // sc_delta_count_at_current_time() << std::endl;
            new_ins = emit_ins;
            new_ins_warpid = emitins_warpid;
            auto& hwarp = m_hw_warps[new_ins_warpid];
            assert(hwarp->is_warp_activated);
            switch (new_ins.op) {
            case OP_TYPE::BARRIER_:
                f_warp_barrier_req(
                    new_ins_warpid, hwarp->blk_slot_idx, hwarp->warp_idx_in_blk, new_ins.currentpc
                );
                break;
            case OP_TYPE::ENDPRG_:
                hwarp->is_warp_activated = false;
                hwarp->initwarp(); // need 1 more cycle to flush pipe
                reset_endprg_flush_pipe[new_ins_warpid] = true;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} endprg", m_sm_id,
                    warpid_convert(m_subcore_id, new_ins_warpid), new_ins.currentpc, new_ins
                );
#endif
                break;
            default:
                SPDLOG_LOGGER_ERROR(
                    m_logger,
                    "SM {} warp {} 0x{:x} {} ERROR warp scheduler unrecognized instruction",
                    m_sm_id, warpid_convert(m_subcore_id, new_ins_warpid), new_ins.currentpc,
                    new_ins
                );
                assert(0);
                break;
            }
        }

        // wait for all warps dispatch
        wait(ev_warp_dispatch_list);
        // std::cout << "SM" << sm_id << " WARP SCHEDULER receive issue_list " << sc_time_stamp() <<
        // "," << sc_delta_count_at_current_time() << std::endl;

        if (opc_in_ready()) // 这是dispatch_ready，来自opc (ready-valid机制)
        {
            find_dispatchwarp = false; // 是否已经确定要dispatch的warp
            for (int i = 0; i < m_hw_warps.size(); i++) {
                int idx = (i + last_dispatch_warpid + 1) % m_hw_warps.size();
                auto& hwarp = m_hw_warps.at(idx);
                if (!find_dispatchwarp && hwarp->can_dispatch && !wait_barrier[idx]
                    && hwarp->is_warp_activated) {
                    // 调试：记录 WARP_SCHEDULER 选择 warp（针对 0x8000008c）
                    if (m_sm_id == 1 && !hwarp->ififo.isempty() && hwarp->ififo.front()->currentpc == 0x8000008c) {
                        uint32_t global_warp = warpid_convert(m_subcore_id, idx);
                        std::cout << "[WARP_SCHEDULER] SM" << m_sm_id << " subcore" << m_subcore_id
                                  << " selected warp" << idx << " (global_warp=" << global_warp << ")"
                                  << " ins=0x" << std::hex << hwarp->ififo.front()->currentpc << std::dec
                                  << " op=" << static_cast<int>(hwarp->ififo.front()->op)
                                  << " can_dispatch=" << hwarp->can_dispatch
                                  << " wait_barrier=" << wait_barrier[idx]
                                  << " is_warp_activated=" << hwarp->is_warp_activated
                                  << " dispatch_warp_valid=true"
                                  << " @ " << sc_time_stamp() << std::endl;
                    }
                    hwarp->dispatch_warp_valid = true;
                    dispatch_valid = true;
                    _newissueins = *hwarp->ififo.front();
                    _newissueins.mask = hwarp->current_mask;
                    // std::cout << "SM" << sm_id << " warp" << i % hw_num_warp << " 0x" << std::hex
                    //           << _newissueins.currentpc << std::dec << _newissueins
                    //           << " issue_ins mask=" << _newissueins.mask << " at " <<
                    //           sc_time_stamp() << ","
                    //           << sc_delta_count_at_current_time() << std::endl;
                    issue_ins = _newissueins;
                    issueins_warpid = idx;
                    find_dispatchwarp = true;
                    last_dispatch_warpid = idx;
                } else {
                    // 调试：记录 dispatch_warp_valid 被清除（针对 0x8000008c）
                    if (m_sm_id == 1 && idx == 1 && !hwarp->ififo.isempty() && hwarp->ififo.front()->currentpc == 0x8000008c) {
                        uint32_t global_warp = warpid_convert(m_subcore_id, idx);
                        std::cout << "[WARP_SCHEDULER] SM" << m_sm_id << " subcore" << m_subcore_id
                                  << " CLEAR dispatch_warp_valid: warp" << idx << " (global_warp=" << global_warp << ")"
                                  << " ins=0x" << std::hex << hwarp->ififo.front()->currentpc << std::dec
                                  << " can_dispatch=" << hwarp->can_dispatch
                                  << " wait_barrier=" << wait_barrier[idx]
                                  << " is_warp_activated=" << hwarp->is_warp_activated
                                  << " @ " << sc_time_stamp() << std::endl;
                    }
                    hwarp->dispatch_warp_valid = false;
                    // std::cout << "ISSUE: let warp" << i % hw_num_warp << "
                    // dispatch_warp_valid=false at " << sc_time_stamp() << "," <<
                    // sc_delta_count_at_current_time() << std::endl;
                }
            }
            if (!find_dispatchwarp)
                dispatch_valid = false;
        }
        
        // 注意：initwarp() 已经设置了 endprg_flush_pipe=true，所以这里不需要再次设置
        // reset_endprg_flush_pipe 标志可能是用于其他目的，暂时保留但不使用
        for (int warpidx = 0; warpidx < m_hw_warps.size(); warpidx++) {
            if (reset_endprg_flush_pipe[warpidx]) {
                // initwarp() 已经设置了 endprg_flush_pipe，这里只是重置标志
                reset_endprg_flush_pipe[warpidx] = false;
            }
        }
    }
}
