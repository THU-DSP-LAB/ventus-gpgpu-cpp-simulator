#include "BASE.h"
#include <algorithm>

void BASE::WARP_SCHEDULER() {
    bool find_dispatchwarp = 0;
    last_dispatch_warpid = 0;
    I_TYPE _newissueins;
    uint32_t wait_barrier_ins_pc;
    I_TYPE new_ins; // from opc, barrier ins
    int new_ins_warpid;
    bool end_this_kernel;
    bool reset_endprg_flush_pipe[hw_num_warp];
    while (true) {
        wait(clk.posedge_event());

        for (int i = 0; i < hw_num_warp; i++) {
            if (reset_endprg_flush_pipe[i]) {
                m_hw_warps[i]->endprg_flush_pipe.write(false);
                reset_endprg_flush_pipe[i] = false;
            }
        }

        // std::cout << "SM" << sm_id << " WARP SCHEDULER start at " << sc_time_stamp() << "," <<
        // sc_delta_count_at_current_time() << std::endl;

        // handle warp end

        // if (m_kernel && m_kernel->no_more_ctas_to_run() && m_current_kernel_running.read())
        //{
        //     end_this_kernel = true;
        //     for (auto &warp : m_hw_warps)
        //     {
        //         if (warp->is_warp_activated.read() == true)
        //             end_this_kernel = false;
        //     }
        //     if (end_this_kernel)
        //     {
        //         m_current_kernel_running.write(false);
        //         m_current_kernel_completed.write(true);
        //         std::cout << "SM" << sm_id << " Warp Scheduler: finish current kernel at " << sc_time_stamp() << ","
        //         << sc_delta_count_at_current_time() << std::endl;
        //     }
        // }

        ev_warp_assigned.notify();

        // barrier from opc

        if (emito_warpscheduler) {
            // std::cout << "SM" << sm_id << " WARP SCHEDULER receive emit ins" << emit_ins << ", warpid=" <<
            // emitins_warpid << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
            new_ins = emit_ins;
            new_ins_warpid = emitins_warpid;
            auto& hwarp = m_hw_warps[new_ins_warpid];
            auto& hblkslot = m_block_slots[hwarp->blk_slot_idx];
            assert(hwarp->is_warp_activated);
            assert(hblkslot.valid);
            switch (new_ins.op) {
            case OP_TYPE::BARRIER_:
                if (std::any_of(hblkslot.warp_reach_barrier.begin(), hblkslot.warp_reach_barrier.end(),
                                [](bool i) { return i == true; })) {
                    // this is the first warp of this block that reaches barrier
                    hblkslot.warp_reach_barrier[hwarp->warp_idx_in_blk] = true;
                    wait_barrier[new_ins_warpid] = true;
                    hblkslot.barrier_addr = new_ins.currentpc;
                } else {
                    // this is not the first warp of this block that reaches barrier
                    assert(new_ins.currentpc == hblkslot.barrier_addr);
                    hblkslot.warp_reach_barrier[hwarp->warp_idx_in_blk] = true;
                    wait_barrier[new_ins_warpid] = true;
                    if (std::all_of(hblkslot.warp_reach_barrier.begin(), hblkslot.warp_reach_barrier.end(),
                                    [](bool i) { return i == true; })) {
                        // all warps of this block reach barrier
                        std::cout << "SM" << sm_id << " warp scheduler: all warps reach barrier pc=0x" << std::hex
                                  << new_ins.currentpc << " " << new_ins << std::dec << " at " << sc_time_stamp() << ","
                                  << sc_delta_count_at_current_time() << std::endl;
                        // reset barrier
                        hblkslot.warp_reach_barrier.fill(false);
                        for (int hw_warp_idx = 0; hw_warp_idx < hblkslot.num_warp; hw_warp_idx++) {
                            // release all hardware warps running this block
                            if (hblkslot.hw_warp_running[hw_warp_idx]) {
                                wait_barrier[hw_warp_idx] = false;
                            }
                        }
                    }
                }
                break;

            case OP_TYPE::ENDPRG_:
                hwarp->is_warp_activated = false;
                if (m_warp_finish_callback) { // callback CTA Scheduler, return the finished warp
                    m_warp_finish_callback(sm_id, hwarp->blk_slot_idx, hwarp->warp_idx_in_blk);
                    hwarp->will_warp_activate = false;
                }

                // update block_slot data
                assert(hblkslot.num_warp > 0);
                assert(hblkslot.hw_warp_running[new_ins_warpid]);
                hblkslot.num_warp--;
                hblkslot.hw_warp_running[new_ins_warpid] = false;
                if (hblkslot.num_warp == 0) { // the last running warp of this block returns, reset its block_slot
                    assert(std::all_of(hblkslot.hw_warp_running.begin(), hblkslot.hw_warp_running.end(),
                                       [](bool i) { return i == false; }));
                    hblkslot.valid = false;
                    hblkslot.warp_reach_barrier.fill(false);
                }

                hwarp->initwarp();
                reset_endprg_flush_pipe[new_ins_warpid] = true;
                std::cout << "SM" << sm_id << " warp " << new_ins_warpid << " 0x" << std::hex << new_ins.currentpc
                          << " " << new_ins << " endprg"
                          << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
                break;

            default:
                std::cout << "warp scheduler warning, receive unrecognized instruction\n";
                break;
            }
        }

        // wait for all warps dispatch
        wait(ev_warp_dispatch_list);
        // std::cout << "SM" << sm_id << " WARP SCHEDULER receive issue_list " << sc_time_stamp() << "," <<
        // sc_delta_count_at_current_time() << std::endl;

        if (!opc_full | doemit) // 这是dispatch_ready，来自opc (ready-valid机制)
        {
            find_dispatchwarp = false; // 是否已经确定要dispatch的warp
            for (int i = last_dispatch_warpid; i < last_dispatch_warpid + hw_num_warp; i++) {
                if (!find_dispatchwarp && m_hw_warps[i % hw_num_warp]->can_dispatch && !wait_barrier[i % hw_num_warp]
                    && m_hw_warps[i % hw_num_warp]->is_warp_activated) {
                    m_hw_warps[i % hw_num_warp]->dispatch_warp_valid = true;
                    dispatch_valid = true;
                    _newissueins = m_hw_warps[i % hw_num_warp]->ififo.front();
                    _newissueins.mask = m_hw_warps[i % hw_num_warp]->current_mask;
                    // std::cout << "SM" << sm_id << " warp" << i % hw_num_warp << " 0x" << std::hex
                    //           << _newissueins.currentpc << std::dec << _newissueins
                    //           << " issue_ins mask=" << _newissueins.mask << " at " << sc_time_stamp() << ","
                    //           << sc_delta_count_at_current_time() << std::endl;
                    issue_ins = _newissueins;
                    issueins_warpid = i % hw_num_warp;
                    find_dispatchwarp = true;
                    last_dispatch_warpid = i % hw_num_warp + 1;
                } else {
                    m_hw_warps[i % hw_num_warp]->dispatch_warp_valid = false;
                    // std::cout << "ISSUE: let warp" << i % hw_num_warp << " dispatch_warp_valid=false at " <<
                    // sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
                }
            }
            if (!find_dispatchwarp)
                dispatch_valid = false;
        }
    }
}
