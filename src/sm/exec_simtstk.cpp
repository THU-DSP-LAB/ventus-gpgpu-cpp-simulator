#include "subcore.hpp"
#include <spdlog/spdlog.h>

// 函数用于比较两个 sc_bv 中“1”的个数
int compareOnesInSCBV(
    const sc_bv<hw_num_thread>& mask1, const sc_bv<hw_num_thread>& mask2,
    const sc_bv<hw_num_thread>& ins_current_mask, int CSR_NUMT
) { // CSR_NUMT为当前有效的线程数
    int count1 = 0, count2 = 0;
    for (int i = 0; i < CSR_NUMT; ++i) {
        if (ins_current_mask[i] != 1)
            continue;
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

bool checkMaskAllZero(const sc_bv<hw_num_thread>& mask, const sc_bv<hw_num_thread>& branch_mask) {
    for (int i = 0; i < hw_num_thread; i++) {
        // 如果 mask[i] 为 1，检查 branch_mask[i] 是否为 0
        if (mask[i] == 1 && branch_mask[i] != 0) {
            return false; // 如果任何一个对应的位不满足条件，返回 false
        }
    }
    return true; // 如果所有对应位都满足条件，返回 true
}

void Subcore::SIMT_STACK(int warp_id) {
    simtstack_t newstkelem;
    I_TYPE readins;
    simtstack_t tmpstkelem;
    auto& hwarp = m_hw_warps[warp_id];
    while (true) {
        wait(clk.posedge_event());
        hwarp->simtstk_jump = false;
        hwarp->vbran_sig = false;
        if (valuto_simtstk && vbranchins_warpid == warp_id) // VALU计算的beq类指令
        {
            hwarp->vbran_sig = true;
            readins = vbranch_ins.read();
            if (emito_simtstk && emitins_warpid == warp_id)
                std::cout << "SM" << m_sm_id << " warp " << warp_id
                          << " SIMT-STACK error: receive join & beq at the same time at "
                          << sc_time_stamp() << "," << sc_delta_count_at_current_time()
                          << std::endl;

#ifdef SPIKE_OUTPUT
            SPDLOG_LOGGER_TRACE(
                m_logger,
                "SM {} warp {} 0x{:x} {} from VALU, current_mask={:X}, branch_mask={:X}, "
                "stack-size={}",
                m_sm_id, warpid_convert(m_subcore_id, warp_id), readins.currentpc, readins,
                readins.mask.to_uint(), branch_elsemask.read().to_uint(), hwarp->IPDOM_stack.size()
            );
#endif
            if (checkMaskAllZero(
                    readins.mask, branch_elsemask.read()
                )) { // VALU计算出的elsemask全为0，不对stack操作，不跳转
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} join mask={:X} stack-size={}", m_sm_id,
                    warpid_convert(m_subcore_id, warp_id), readins.currentpc, readins,
                    readins.mask.to_uint(), hwarp->IPDOM_stack.size()
                );
#endif
            } else if (checkMaskAllZero(
                           readins.mask, branch_ifmask.read()
                       )) { // VALU计算出的ifmask全为0, elsemask全为1，跳转
                // 不对stack操作
                // 跳转
                hwarp->simtstk_jumpaddr = branch_elsepc;
                hwarp->current_mask = branch_elsemask; // 其实不变
                hwarp->simtstk_jump = true;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger,
                    "SM {} warp {} 0x{:x} {} goto elsepath(jump) mask={:X} jumpTO 0x{:x}, "
                    "stack-size={}",
                    m_sm_id, warpid_convert(m_subcore_id, warp_id), readins.currentpc, readins,
                    branch_elsemask.read().to_uint(), branch_elsepc, hwarp->IPDOM_stack.size()
                );
#endif
            } else {
                if (compareOnesInSCBV(
                        branch_ifmask, branch_elsemask, readins.mask, hwarp->CSR_reg[0x802]
                    )
                    == -1) { // if_mask线程数更少，不跳转，pc+4
                    hwarp->current_mask = branch_ifmask;
                    // 压栈两次
                    newstkelem.rpc = hwarp->CSR_reg[0x80c];
                    newstkelem.nextpc = hwarp->CSR_reg[0x80c];
                    newstkelem.nextmask = vbranch_ins.read().mask;
                    hwarp->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem <<
                    // " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
                    // std::endl;

                    newstkelem.nextpc = branch_elsepc;
                    newstkelem.nextmask = branch_elsemask;
                    hwarp->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem <<
                    // " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
                    // std::endl;
#ifdef SPIKE_OUTPUT
                    SPDLOG_LOGGER_TRACE(
                        m_logger,
                        "SM {} warp {} 0x{:x} {} goto ifpath(pc+4) mask={:X} stack-size={}",
                        m_sm_id, warpid_convert(m_subcore_id, warp_id), readins.currentpc, readins,
                        branch_ifmask.read().to_uint(), hwarp->IPDOM_stack.size()
                    );
#endif
                } else { // else_mask线程数更少或相等，先跳转到else path
                    hwarp->simtstk_jumpaddr = branch_elsepc;
                    hwarp->current_mask = branch_elsemask;
                    hwarp->simtstk_jump = true;

                    // 压栈时，不跳转的分支被视为else path
                    newstkelem.rpc = hwarp->CSR_reg[0x80c];
                    newstkelem.nextpc = hwarp->CSR_reg[0x80c];
                    newstkelem.nextmask = vbranch_ins.read().mask;
                    hwarp->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem <<
                    // " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
                    // std::endl;

                    newstkelem.nextpc = vbranch_ins.read().currentpc + 4;
                    newstkelem.nextmask = branch_ifmask;
                    hwarp->IPDOM_stack.push(newstkelem);
                    // std::cout << "SIMT-stack warp " << warp_id << " pushed elem" << newstkelem <<
                    // " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
                    // std::endl;
#ifdef SPIKE_OUTPUT
                    SPDLOG_LOGGER_TRACE(
                        m_logger,
                        "SM {} warp {} 0x{:x} {} goto elsepath(jump) mask={:X} jumpTO 0x{:x}, "
                        "stack-size={}",
                        m_sm_id, warpid_convert(m_subcore_id, warp_id), readins.currentpc, readins,
                        branch_elsemask.read().to_uint(), branch_elsepc.read(),
                        hwarp->IPDOM_stack.size()
                    );
#endif
                }
            }
        }

        if (emito_simtstk && emitins_warpid == warp_id) // OPC发射的join指令
        {
#ifdef SPIKE_OUTPUT
            SPDLOG_LOGGER_TRACE(
                m_logger, "SM {} warp {} 0x{:x} {} SIMTSTK receive join, stack-size={}", m_sm_id,
                warpid_convert(m_subcore_id, warp_id), emit_ins.read().currentpc, emit_ins.read(),
                hwarp->IPDOM_stack.size()
            );
#endif
            hwarp->vbran_sig = true;
            /*** 以下为分支控制 ***/
            if (not hwarp->IPDOM_stack.empty()) {
                tmpstkelem = hwarp->IPDOM_stack.top();
                readins = emit_ins;
                if (readins.currentpc == tmpstkelem.rpc) {
                    hwarp->IPDOM_stack.pop();
#ifdef SPIKE_OUTPUT
                    SPDLOG_LOGGER_TRACE(
                        m_logger,
                        "SM {} warp {} 0x{:x} {} SIMTSTK jump=true, jumpTO 0x{:x}, "
                        "mask change from {:X} to {:X}, stack-size={}",
                        m_sm_id, warpid_convert(m_subcore_id, warp_id), emit_ins.read().currentpc,
                        emit_ins.read(), tmpstkelem.nextpc, hwarp->current_mask.read().to_uint(),
                        tmpstkelem.nextmask.to_uint(), hwarp->IPDOM_stack.size()
                    );
#endif
                    hwarp->simtstk_jumpaddr = tmpstkelem.nextpc;
                    hwarp->current_mask = tmpstkelem.nextmask;

                    hwarp->simtstk_jump = true;
                } else {
// 栈顶元素不是当前rpc，什么都不做。在循环时适用
#ifdef SPIKE_OUTPUT
                    SPDLOG_LOGGER_TRACE(
                        m_logger,
                        "SM {} warp {} 0x{:x} {} SIMTSTK receive join ins and nothing to do, "
                        "stack-size={}",
                        m_sm_id, warpid_convert(m_subcore_id, warp_id), emit_ins.read().currentpc,
                        emit_ins.read(), hwarp->IPDOM_stack.size()
                    );
#endif
                }
            } else {
                // 栈为空，什么都不做（这是正确的，不用报错）
            }
        }
    }
}
