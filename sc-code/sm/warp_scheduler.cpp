#include "BASE.h"

void BASE::WARP_SCHEDULER()
{
    bool find_dispatchwarp = 0;
    last_dispatch_warpid = 0;
    I_TYPE _newissueins;
    uint32_t wait_barrier_ins_pc;
    I_TYPE new_ins; // from opc, barrier ins
    int new_ins_warpid;
    while (true)
    {
        wait(clk.posedge_event());

        // cout << "SM" << sm_id << " WARP SCHEDULER start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

        // handle warp end

        ev_warp_assigned.notify();

        // barrier from opc

        if (emito_warpscheduler)
        {
            // cout << "SM" << sm_id << " WARP SCHEDULER receive emit ins" << emit_ins << ", warpid=" << emitins_warpid << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            new_ins = emit_ins;
            new_ins_warpid = emitins_warpid;
            switch (new_ins.op)
            {
            case OP_TYPE::BARRIER_:
                if (wait_barrier.areFirstNValue(num_warp_activated, false)) // 未设置barrier
                {
                    wait_barrier[new_ins_warpid] = true;
                    wait_barrier_ins_pc = new_ins.currentpc;
                }
                else
                {
                    if (new_ins.currentpc == wait_barrier_ins_pc)
                    {
                        wait_barrier[new_ins_warpid] = true;
                        if (wait_barrier.areFirstNValue(num_warp_activated, true))
                        {
                            wait_barrier.fill(false);
                            std::cout << "warp scheduler: all warps reach barrier pc=0x" << std::hex << new_ins.currentpc << " " << new_ins << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                        }
                    }
                    else
                    {
                        std::cout << "WARP_SCHEDULER ERROR: Barrier pc not match at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    }
                }
                break;

            case OP_TYPE::ENDPRG_:
                if (WARPS[new_ins_warpid]->is_warp_activated)
                    WARPS[new_ins_warpid]->is_warp_activated = false;
                cout << "SM" << sm_id << " warp " << new_ins_warpid << " 0x" << std::hex << new_ins.currentpc
                     << " " << new_ins << " endprg"
                     << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                break;

            default:
                cout << "warp scheduler warning, receive unrecognized instruction\n";
                break;
            }
        }

        // wait for all warps dispatch
        wait(ev_issue_list);
        // cout << "SM" << sm_id << " WARP SCHEDULER receive issue_list " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

        if (!opc_full | doemit) // 这是dispatch_ready，来自opc (ready-valid机制)
        {
            find_dispatchwarp = false; // 是否已经确定要dispatch的warp
            for (int i = last_dispatch_warpid; i < last_dispatch_warpid + num_warp_activated; i++)
            {
                if (!find_dispatchwarp && WARPS[i % num_warp_activated]->can_dispatch && !wait_barrier[i % num_warp_activated] && WARPS[i % num_warp_activated]->is_warp_activated)
                {
                    WARPS[i % num_warp_activated]->dispatch_warp_valid = true;
                    dispatch_valid = true;
                    _newissueins = WARPS[i % num_warp_activated]->ififo.front();
                    _newissueins.mask = WARPS[i % num_warp_activated]->current_mask;
                    // cout << "let issue_ins mask=" << _newissueins.mask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    issue_ins = _newissueins;
                    issueins_warpid = i % num_warp_activated;
                    find_dispatchwarp = true;
                    last_dispatch_warpid = i % num_warp_activated + 1;
                }
                else
                {
                    WARPS[i % num_warp_activated]->dispatch_warp_valid = false;
                    // cout << "ISSUE: let warp" << i % num_warp_activated << " dispatch_warp_valid=false at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }
            }
            if (!find_dispatchwarp)
                dispatch_valid = false;
        }
    }
}
