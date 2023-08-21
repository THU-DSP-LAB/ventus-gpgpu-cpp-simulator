#include "BASE.h"

void BASE::SIMT_STACK(int warp_id)
{
    simtstack_t newstkelem;
    I_TYPE readins;
    while (true)
    {
        wait(clk.posedge_event());
        WARPS[warp_id]->simtstk_jump = false;
        WARPS[warp_id]->simtstk_flush = false;
        WARPS[warp_id]->vbran_sig = false;
        if (valuto_simtstk && vbranchins_warpid == warp_id) // VALU计算的beq类指令
        {
            WARPS[warp_id]->vbran_sig = true;
            if (emito_simtstk)
                cout << "SIMT-STACK error: receive join & beq at the same time at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            /*** 以下为stack管理 ***/
            if (std::bitset<num_thread>(branch_elsemask.read().to_string()) == 0)
            { // VALU计算出的elsemask全为0
              // newstkelem.pair = 1;
              // newstkelem.is_part = 1;
            }
            else if (std::bitset<num_thread>(branch_elsemask.read().to_string()) == std::bitset<num_thread>().set())
            { // VALU计算出的ifmask全为0, elsemask全为1
              // newstkelem.pair = 0;
              // newstkelem.is_part = 1;
            }
            else
            {
                newstkelem.rpc = WARPS[warp_id]->CSR_reg[0x80c];
                newstkelem.nextpc = WARPS[warp_id]->CSR_reg[0x80c];
                newstkelem.nextmask = vbranch_ins.read().mask;
                WARPS[warp_id]->simt_stack.push(newstkelem);
                cout << "SIMT-stack warp" << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                newstkelem.nextpc = branch_elsepc;
                newstkelem.nextmask = branch_elsemask;
                WARPS[warp_id]->simt_stack.push(newstkelem);
                cout << "SIMT-stack warp" << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }

            /*** 以下为分支控制 ***/
            if (std::bitset<num_thread>(branch_elsemask.read().to_string()) == 0)
            { // elsemask全为0，不跳转
                // 什么也不做
                cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << vbranch_ins.read().currentpc << std::dec
                     << " " << vbranch_ins
                     << " join " << vbranch_ins.read().mask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else if (std::bitset<num_thread>(branch_elsemask.read().to_string()) == std::bitset<num_thread>().set())
            { // ifmask全为0，elsemask全为1
                WARPS[warp_id]->simtstk_jumpaddr = branch_elsepc;
                WARPS[warp_id]->current_mask = branch_elsemask; // 全为1
                WARPS[warp_id]->simtstk_jump = true;
                WARPS[warp_id]->simtstk_flush = true;
                cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << vbranch_ins.read().currentpc << std::dec
                     << " " << vbranch_ins
                     << " elsepath(jump) " << branch_elsemask << " jumpTO 0x"
                     << std::hex << branch_elsepc << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else
            { // 暂时默认先走if path
                cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << vbranch_ins.read().currentpc << std::dec
                     << " " << vbranch_ins
                     << " ifpath(pc+4) " << branch_ifmask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
        }
        if (emito_simtstk && emitins_warpid == warp_id) // OPC发射的join指令
        {
            cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << emit_ins.read().currentpc << std::dec
                 << " SIMT_STK receive join ins" << emit_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            WARPS[warp_id]->vbran_sig = true;
            /*** 以下为分支控制 ***/
            if (!WARPS[warp_id]->simt_stack.empty())
            {
                simtstack_t &tmpstkelem = WARPS[warp_id]->simt_stack.top();
                readins = emit_ins;
                if (readins.currentpc == tmpstkelem.rpc)
                {
                    cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << emit_ins.read().currentpc
                         << " " << emit_ins << "jump=true, jumpTO 0x" << tmpstkelem.nextpc
                         << ", mask change from " << WARPS[warp_id]->current_mask << " to " << tmpstkelem.nextmask
                         << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    WARPS[warp_id]->simtstk_jumpaddr = tmpstkelem.nextpc;
                    WARPS[warp_id]->current_mask = tmpstkelem.nextmask;

                    WARPS[warp_id]->simtstk_jump = true;
                    WARPS[warp_id]->simtstk_flush = true;
                    WARPS[warp_id]->simt_stack.pop();
                }
            }
        }
    }
}
