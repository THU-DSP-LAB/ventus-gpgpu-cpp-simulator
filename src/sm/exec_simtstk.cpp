#include "BASE.h"

// 函数用于比较两个 sc_bv 中“1”的个数
int compareOnesInSCBV(const sc_bv<hw_num_thread> &mask1, const sc_bv<hw_num_thread> &mask2, const sc_bv<hw_num_thread> &ins_current_mask, int CSR_NUMT)
{ // CSR_NUMT为当前有效的线程数
    int count1 = 0, count2 = 0;
    for (int i = 0; (i < CSR_NUMT) && (ins_current_mask[i] == 1); ++i)
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

bool checkMaskAllZero(const sc_bv<hw_num_thread> &mask, const sc_bv<hw_num_thread> &branch_mask)
{
    std::cout << "checkMaskAllZero: mask=" << mask << ", branch_mask=" << branch_mask << "\n";
    for (int i = 0; i < hw_num_thread; i++)
    {
        // 如果 mask[i] 为 1，检查 branch_mask[i] 是否为 0
        if (mask[i] == 1 && branch_mask[i] != 0)
        {
            return false; // 如果任何一个对应的位不满足条件，返回 false
        }
    }
    return true; // 如果所有对应位都满足条件，返回 true
}

void BASE::SIMT_STACK(int warp_id)
{
    simtstack_t newstkelem;
    I_TYPE readins;
    while (true)
    {
        wait(clk.posedge_event());
        m_hw_warps[warp_id]->simtstk_jump = false;
        m_hw_warps[warp_id]->vbran_sig = false;
        if (valuto_simtstk && vbranchins_warpid == warp_id) // VALU计算的beq类指令
        {
            m_hw_warps[warp_id]->vbran_sig = true;
            readins = vbranch_ins.read();
            if (emito_simtstk)
                std::cout << "SIMT-STACK error: receive join & beq at the same time at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

            if (checkMaskAllZero(readins.mask, branch_elsemask.read()))
            { // VALU计算出的elsemask全为0，不对stack操作，不跳转
#ifdef SPIKE_OUTPUT
                std::cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << readins.currentpc << std::dec
                          << " " << readins
                          << " join " << readins.mask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#endif
            }
            else if (checkMaskAllZero(readins.mask, branch_ifmask.read()))
            { // VALU计算出的ifmask全为0, elsemask全为1，跳转
                // 不对stack操作
                // 跳转
                m_hw_warps[warp_id]->simtstk_jumpaddr = branch_elsepc;
                m_hw_warps[warp_id]->current_mask = branch_elsemask; // 全为1
                m_hw_warps[warp_id]->simtstk_jump = true;
#ifdef SPIKE_OUTPUT
                std::cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << readins.currentpc << std::dec
                          << " " << readins
                          << " goto elsepath(jump) mask=" << branch_elsemask << " jumpTO 0x"
                          << std::hex << branch_elsepc << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#endif
            }
            else
            {
                if (compareOnesInSCBV(branch_ifmask, branch_elsemask, readins.mask, m_hw_warps[warp_id]->CSR_reg[0x802]) != 1)
                { // if_mask线程数更少，不跳转，pc+4
                    // 压栈两次
                    newstkelem.rpc = m_hw_warps[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextpc = m_hw_warps[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextmask = vbranch_ins.read().mask;
                    m_hw_warps[warp_id]->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    newstkelem.nextpc = branch_elsepc;
                    newstkelem.nextmask = branch_elsemask;
                    m_hw_warps[warp_id]->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#ifdef SPIKE_OUTPUT
                    std::cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << readins.currentpc << std::dec
                              << " " << readins
                              << " goto ifpath(pc+4) mask=" << branch_ifmask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#endif
                }
                else
                { // else_mask线程数更少，先跳转到else path
                    m_hw_warps[warp_id]->simtstk_jumpaddr = branch_elsepc;
                    m_hw_warps[warp_id]->current_mask = branch_elsemask;
                    m_hw_warps[warp_id]->simtstk_jump = true;

                    // 压栈时，不跳转的分支被视为else path
                    newstkelem.rpc = m_hw_warps[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextpc = m_hw_warps[warp_id]->CSR_reg[0x80c];
                    newstkelem.nextmask = vbranch_ins.read().mask;
                    m_hw_warps[warp_id]->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    newstkelem.nextpc = vbranch_ins.read().currentpc + 4;
                    newstkelem.nextmask = branch_ifmask;
                    m_hw_warps[warp_id]->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#ifdef SPIKE_OUTPUT
                    std::cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << readins.currentpc << std::dec
                              << " " << readins
                              << " going to elsepath(jump) mask=" << branch_elsemask << " jumpTO 0x"
                              << std::hex << branch_elsepc << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#endif
                }
            }
        }

        if (emito_simtstk && emitins_warpid == warp_id) // OPC发射的join指令
        {
#ifdef SPIKE_OUTPUT
            std::cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << emit_ins.read().currentpc << std::dec
                      << " SIMT_STK receive join ins" << emit_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#endif
            m_hw_warps[warp_id]->vbran_sig = true;
            /*** 以下为分支控制 ***/
            if (!m_hw_warps[warp_id]->IPDOM_stack.empty())
            {
                simtstack_t &tmpstkelem = m_hw_warps[warp_id]->IPDOM_stack.top();
                readins = emit_ins;
                if (readins.currentpc == tmpstkelem.rpc)
                {
#ifdef SPIKE_OUTPUT
                    std::cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << emit_ins.read().currentpc
                              << " " << emit_ins << "jump=true, jumpTO 0x" << tmpstkelem.nextpc
                              << ", mask change from " << m_hw_warps[warp_id]->current_mask << " to " << tmpstkelem.nextmask
                              << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
#endif
                    m_hw_warps[warp_id]->simtstk_jumpaddr = tmpstkelem.nextpc;
                    m_hw_warps[warp_id]->current_mask = tmpstkelem.nextmask;

                    m_hw_warps[warp_id]->simtstk_jump = true;
                    m_hw_warps[warp_id]->IPDOM_stack.pop();
                }
            }
        }
    }
}
