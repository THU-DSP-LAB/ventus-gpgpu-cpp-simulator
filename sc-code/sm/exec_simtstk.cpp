#include "BASE.h"

// 函数用于比较两个 sc_bv 中“1”的个数
int compareOnesInSCBV(const sc_bv<num_thread> &mask1, const sc_bv<num_thread> &mask2)
{
    int count1 = 0, count2 = 0;
    for (int i = 0; i < num_thread; ++i)
    {
        if (mask1[i] == 1)
            count1++;
        if (mask2[i] == 1)
            count2++;
    }
    if (count1 > count2)
        return 1;
    if (count2 > count1)
        return -1;
    return 0;
}

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

            if (std::bitset<num_thread>(branch_elsemask.read().to_string()) == 0)
            { // VALU计算出的elsemask全为0，不跳转
                // 不对stack操作
                // 不跳转
                cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << vbranch_ins.read().currentpc << std::dec
                     << " " << vbranch_ins
                     << " join " << vbranch_ins.read().mask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else if (std::bitset<num_thread>(branch_elsemask.read().to_string()) == std::bitset<num_thread>().set())
            { // VALU计算出的ifmask全为0, elsemask全为1，跳转
                // 不对stack操作
                // 跳转
                WARPS[warp_id]->simtstk_jumpaddr = branch_elsepc;
                WARPS[warp_id]->current_mask = branch_elsemask; // 全为1
                WARPS[warp_id]->simtstk_jump = true;
                WARPS[warp_id]->simtstk_flush = true;
                cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << vbranch_ins.read().currentpc << std::dec
                     << " " << vbranch_ins
                     << " goto elsepath(jump) mask=" << branch_elsemask << " jumpTO 0x"
                     << std::hex << branch_elsepc << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else
            {

                if (compareOnesInSCBV(branch_ifmask, branch_elsemask) != 1)
                { // if_mask线程数更少，不跳转，pc+4

                    // 压栈两次
                    newstkelem.rpc = WARPS[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextpc = WARPS[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextmask = vbranch_ins.read().mask;
                    WARPS[warp_id]->IPDOM_stack.push(newstkelem);
                    cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    newstkelem.nextpc = branch_elsepc;
                    newstkelem.nextmask = branch_elsemask;
                    WARPS[warp_id]->IPDOM_stack.push(newstkelem);
                    cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << vbranch_ins.read().currentpc << std::dec
                         << " " << vbranch_ins
                         << " goto ifpath(pc+4) mask=" << branch_ifmask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }
                else
                { // else_mask线程数更少，先跳转到else path
                    WARPS[warp_id]->simtstk_jumpaddr = branch_elsepc;
                    WARPS[warp_id]->current_mask = branch_elsemask;
                    WARPS[warp_id]->simtstk_jump = true;
                    WARPS[warp_id]->simtstk_flush = true;

                    // 压栈时，不跳转的分支被视为else path
                    newstkelem.rpc = WARPS[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextpc = WARPS[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextmask = vbranch_ins.read().mask;
                    WARPS[warp_id]->IPDOM_stack.push(newstkelem);
                    cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    newstkelem.nextpc = vbranch_ins.read().currentpc + 4;
                    newstkelem.nextmask = branch_ifmask;
                    WARPS[warp_id]->IPDOM_stack.push(newstkelem);
                    cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << vbranch_ins.read().currentpc << std::dec
                         << " " << vbranch_ins
                         << " goto elsepath(jump) mask=" << branch_elsemask << " jumpTO 0x"
                         << std::hex << branch_elsepc << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }
            }
        }

        if (emito_simtstk && emitins_warpid == warp_id) // OPC发射的join指令
        {
            cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << emit_ins.read().currentpc << std::dec
                 << " SIMT_STK receive join ins" << emit_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            WARPS[warp_id]->vbran_sig = true;
            /*** 以下为分支控制 ***/
            if (!WARPS[warp_id]->IPDOM_stack.empty())
            {
                simtstack_t &tmpstkelem = WARPS[warp_id]->IPDOM_stack.top();
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
                    WARPS[warp_id]->IPDOM_stack.pop();
                }
            }
        }
    }
}
