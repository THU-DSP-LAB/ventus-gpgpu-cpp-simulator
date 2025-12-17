#include "subcore.hpp"
#include <spdlog/spdlog.h>

void Subcore::SALU_IN() {
    salu_in_t new_data;
    int a_delay, b_delay;
    while (true) {
        wait();
        if (emito_salu) {
            // std::cout << "SALU_IN: receive ins=" << emit_ins << "warp" << emitins_warpid << " at
            // " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
            if (salu_ready_old == false)
                std::cout << "salu error: not ready at " << sc_time_stamp() << ","
                          << sc_delta_count_at_current_time() << std::endl;
            salu_unready.notify();
            switch (emit_ins.read().op) {
            default:
                new_data.ins = emit_ins;
                new_data.warp_id = emitins_warpid;
                new_data.rss1_data = tosalu_data1;
                new_data.rss2_data = tosalu_data2;
                new_data.rss3_data = tosalu_data3;
                salu_dq.push(new_data);
                
                // 调试：追踪 JALR 推入 SALU 队列（放宽条件，打印所有相关指令）
                if (m_sm_id == 1 && emit_ins.read().currentpc >= 0x80000080 && emit_ins.read().currentpc <= 0x800000c0) {
                    uint32_t global_warp = warpid_convert(m_subcore_id, emitins_warpid);
                    std::cout << "[SALU_IN] SM" << m_sm_id << " subcore" << m_subcore_id
                              << " warp" << emitins_warpid << " (global_warp=" << global_warp << ")"
                              << " push to SALU: pc=0x" << std::hex << emit_ins.read().currentpc << std::dec
                              << " op=" << static_cast<int>(emit_ins.read().op)
                              << " @ " << sc_time_stamp() << std::endl;
                }
                
                // std::cout << "[EMIT->SALU] " 
                //   << " tosalu_data1=" << tosalu_data1 
                //   << " tosalu_data2=" << tosalu_data2 
                //   << " tosalu_data3=" << tosalu_data3 
                //   << " @ " << sc_time_stamp() << "\n";

                // std::cout << "salu_dq has just pushed 1 elem at " << sc_time_stamp() <<","<<
                // sc_delta_count_at_current_time() << std::endl;
                a_delay = 1;
                b_delay = 1;
                // std::cout << "SALU_IN: see salueqa_triggered=" << salueqa_triggered << " at " <<
                // sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
                if (a_delay == 0)
                    salu_eva.notify();
                else if (salueqa_triggered) {
                    salu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                    // std::cout << "SALU_IN detect salueqa is triggered at " << sc_time_stamp() <<
                    // "," << sc_delta_count_at_current_time() << std::endl;
                } else {
                    salu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                    ev_salufifo_pushed.notify();
                    for (auto& warp_ : m_hw_warps) {
                        warp_->jump = false;
                        warp_->branch_sig = false;
                    }
                }
                if (b_delay == 0)
                    salu_evb.notify();
                else { // 这都是emit的情况，所以这个cycle eqb不可能被触发
                    salu_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                    ev_saluready_updated.notify();
                }
                // std::cout << "SALU_IN switch to ADD_ (from opc input) at " << sc_time_stamp()
                // <<","<< sc_delta_count_at_current_time() << std::endl;
                break;
                // default:
                //     std::cout << "salu error: receive wrong ins " << emit_ins << " at " <<
                //     sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
                //     break;
            }
        } else {
            if (!salueqa_triggered) {
                ev_salufifo_pushed.notify();
                for (auto& warp_ : m_hw_warps) {
                    warp_->jump = false;
                    warp_->branch_sig = false;
                }
            }
            if (!salueqb_triggered)
                ev_saluready_updated.notify();
        }
    }
}

void Subcore::SALU_CALC() {
    salufifo_elem_num = 0;
    salufifo_empty = 1;
    salueqa_triggered = false;
    bool succeed;
    vaddr_t jump_addr_tmp;
    while (true) {
        wait(salu_eva | salu_eqa.default_event());
        if (salu_eqa.default_event().triggered()) {
            // std::cout << "SALU_CALC detect salueqa triggered at " << sc_time_stamp() << "," <<
            // sc_delta_count_at_current_time() << std::endl;
            salueqa_triggered = true;
            wait(SC_ZERO_TIME);
            salueqa_triggered = false;
        }
        // 调试：记录清除 branch_sig 和 jump（针对 SM1 warp1）
        bool should_debug_clear = false;
        for (auto& warp_ : m_hw_warps) {
            int wid = &warp_ - &m_hw_warps[0];
            if (m_sm_id == 1 && wid == 1 && (warp_->branch_sig || warp_->jump)) {
                should_debug_clear = true;
            }
            warp_->jump = false;
            warp_->branch_sig = false;
        }
        if (should_debug_clear) {
            std::cout << "[SALU_CALC] SM" << m_sm_id
                      << " warp1 CLEAR branch_sig/jump at cycle start"
                      << " @ " << sc_time_stamp() << std::endl;
        }
        salutmp1 = salu_dq.front();
        // 调试：弹出 SALU 队列的指令（关注 0x80000080-0x800000c0）
        if (m_sm_id == 1
            && salutmp1.ins.currentpc >= 0x80000080
            && salutmp1.ins.currentpc <= 0x800000c0) {
            uint32_t global_warp = warpid_convert(m_subcore_id, salutmp1.warp_id);
            std::cout << "[SALU_CALC] POP SM" << m_sm_id << " subcore" << m_subcore_id
                      << " warp" << salutmp1.warp_id << " (global_warp=" << global_warp << ")"
                      << " pc=0x" << std::hex << salutmp1.ins.currentpc << std::dec
                      << " op=" << static_cast<int>(salutmp1.ins.op)
                      << " @ " << sc_time_stamp() << std::endl;
        }
        // std::cout << "salu_dq.front's ins is " << salutmp1.ins << ", data is " <<
        // salutmp1.rss1_data << "," << salutmp1.rss2_data << std::endl;
        salu_dq.pop();
        // std::cout << "salu_dq has poped, now its elem_num is " << salu_dq.size() << " at " <<
        // sc_time_stamp() <<","<< sc_delta_count_at_current_time() << std::endl;
        auto& hwarp = m_hw_warps[salutmp1.warp_id];
        if (salutmp1.ins.ddd.wxd) {
            salutmp2.ins = salutmp1.ins;
            salutmp2.warp_id = salutmp1.warp_id;
            switch (salutmp1.ins.ddd.alu_fn) {
            case DecodeParams::alu_fn_t::FN_ADD:
                salutmp2.data = salutmp1.rss1_data + salutmp1.rss2_data;

                if (salutmp1.ins.ddd.branch == DecodeParams::branch_t::B_J) // jal
                {
#ifdef SPIKE_OUTPUT
                    SPDLOG_LOGGER_TRACE(
                        m_logger, "SM {} warp {} 0x{:x} {} JUMP=true, jumpTO 0x{:x}",
                        m_sm_id, salutmp1.warp_id, salutmp1.ins.currentpc, salutmp1.ins,
                        salutmp1.rss3_data
                    );
#endif
                    hwarp->branch_sig = true;
                    hwarp->jump = 1;
                    hwarp->jump_addr = salutmp1.rss3_data;
                } else if (salutmp1.ins.ddd.branch == DecodeParams::branch_t::B_R) // jalr
                {
                    jump_addr_tmp = (salutmp1.rss3_data + salutmp1.ins.imm) & (~1);
                    std::cout<<"[salu] JALR: warp="<<salutmp1.warp_id
                             <<" pc=0x"<<std::hex<<salutmp1.ins.currentpc<<std::dec
                             <<" rss3_data=0x"<<std::hex<<salutmp1.rss3_data<<std::dec
                             <<" imm="<<salutmp1.ins.imm
                             <<" jump_addr_tmp=0x"<<std::hex<<jump_addr_tmp<<std::dec
                             <<" @ "<<sc_time_stamp()<<std::endl;
                    
                    // 检查跳转地址是否为0（可能是LW指令数据未正确写回）
                    if (jump_addr_tmp == 0x0) {
                        std::cerr << "[salu] ⚠️ WARNING: JALR jump address is 0x0! "
                                  << "warp=" << salutmp1.warp_id
                                  << ", pc=0x" << std::hex << salutmp1.ins.currentpc << std::dec
                                  << ", rss3_data=0x" << std::hex << salutmp1.rss3_data << std::dec
                                  << ", imm=" << salutmp1.ins.imm
                                  << ". This may indicate that LW instruction data was not written back correctly!"
                                  << std::endl;
                    }
                    
#ifdef SPIKE_OUTPUT
                    SPDLOG_LOGGER_TRACE(
                        m_logger, "SM {} warp {} 0x{:x} {} JUMP=true, jumpTO 0x{:x}",
                        m_sm_id, salutmp1.warp_id, salutmp1.ins.currentpc, salutmp1.ins,
                        jump_addr_tmp
                    );
#endif
                    hwarp->branch_sig = true;
                    hwarp->jump = 1;
                    hwarp->jump_addr = jump_addr_tmp;
                    
                // 调试：记录 JALR 设置 branch_sig 和 jump（放宽条件，打印所有 JALR）
                if (m_sm_id == 1 && salutmp1.ins.currentpc >= 0x80000080 && salutmp1.ins.currentpc <= 0x800000c0) {
                    uint32_t global_warp = warpid_convert(m_subcore_id, salutmp1.warp_id);
                    std::cout << "[SALU_CALC] SM" << m_sm_id << " subcore" << m_subcore_id
                              << " warp" << salutmp1.warp_id << " (global_warp=" << global_warp << ")"
                              << " JALR SET branch_sig=1 jump=1: pc=0x" << std::hex << salutmp1.ins.currentpc << std::dec
                              << " jump_addr=0x" << std::hex << jump_addr_tmp << std::dec
                              << " @ " << sc_time_stamp() << std::endl;
                }
                } else {
#ifdef SPIKE_OUTPUT
                    // if (salutmp1.warp_id == 2 && m_sm_id == 0) {
                    //     std::cout << "SM" << m_sm_id << " warp " << salutmp1.warp_id << " 0x"
                    //               << std::hex << salutmp1.ins.currentpc << " " << salutmp1.ins
                    //               << ", rs1=" << std::hex << salutmp1.rss1_data
                    //               << ", rs2=" << salutmp1.rss2_data << std::dec << " at "
                    //               << sc_time_stamp() << "," << sc_delta_count_at_current_time()
                    //               << std::endl;
                    //     std::cout << "↑currently, rs1_addr=" << salutmp1.ins.s1
                    //               << ",rs2_addr=" << salutmp1.ins.s2
                    //               << ",rd_addr=" << salutmp1.ins.d
                    //               << ", s_regfile[rs1_addr]=" << hwarp->s_regfile[salutmp1.ins.s1]
                    //               << ", s_regfile[rs2_addr]=" << hwarp->s_regfile[salutmp1.ins.s2]
                    //               << std::endl;
                    // }
#endif
                }

                break;

            // case AND_:
            // case ANDI_:
            case DecodeParams::alu_fn_t::FN_AND:
                salutmp2.data = salutmp1.rss1_data & salutmp1.rss2_data;
                break;

            // case LUI_:
            case DecodeParams::alu_fn_t::FN_A1ZERO:
                salutmp2.data = salutmp1.rss2_data;
                break;

            // case OR_:
            // case ORI_:
            case DecodeParams::alu_fn_t::FN_OR:
                salutmp2.data = salutmp1.rss1_data | salutmp1.rss2_data;
                break;

            // case SLL_:
            // case SLLI_:
            case DecodeParams::alu_fn_t::FN_SL:
                salutmp2.data = salutmp1.rss1_data << salutmp1.rss2_data;
                break;

            // case SLT_:
            // case SLTI_:
            case DecodeParams::alu_fn_t::FN_SLT:
                if (salutmp1.rss1_data < salutmp1.rss2_data)
                    salutmp2.data = 1;
                else
                    salutmp2.data = 0;
                break;

            // case SLTIU_:
            // case SLTU_:
            case DecodeParams::alu_fn_t::FN_SLTU:
                if (static_cast<unsigned int>(salutmp1.rss1_data)
                    < static_cast<unsigned int>(salutmp1.rss2_data))
                    salutmp2.data = 1;
                else
                    salutmp2.data = 0;
                break;

            // case SRA_:
            // case SRAI_:
            case DecodeParams::alu_fn_t::FN_SRA:
                salutmp2.data = salutmp1.rss1_data >> salutmp1.rss2_data;
                break;

            // case SRL_:
            // case SRLI_:
            case DecodeParams::alu_fn_t::FN_SR:
                salutmp2.data = static_cast<unsigned int>(salutmp1.rss1_data) >> salutmp1.rss2_data;
                break;

            // case SUB_:
            case DecodeParams::alu_fn_t::FN_SUB:
                salutmp2.data = salutmp1.rss1_data - salutmp1.rss2_data;
                break;

            // case XOR_:
            // case XORI_:
            case DecodeParams::alu_fn_t::FN_XOR:
                salutmp2.data = salutmp1.rss1_data ^ salutmp1.rss2_data;
                break;

            default:
                std::cout << "SALU_CALC warning: switch to unrecognized ins" << salutmp1.ins
                          << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time()
                          << std::endl;
                assert(0);
                break;
            }
            salufifo.push(salutmp2);
        } else { // for branch instructions
#ifdef SPIKE_OUTPUT
            std::string log_str = fmt::format(
                "SM {} warp {} 0x{:x} {} mask={:X}, JUMP=", m_sm_id, salutmp1.warp_id,
                salutmp1.ins.currentpc, salutmp1.ins, salutmp1.ins.mask.to_uint64()
            );
#endif
            switch (salutmp1.ins.ddd.alu_fn) {
            // case BEQ_:
            case DecodeParams::alu_fn_t::FN_SEQ:
                hwarp->branch_sig = true;
                if (salutmp1.rss1_data == salutmp1.rss2_data) {
                    hwarp->jump = 1;
                    hwarp->jump_addr = salutmp1.rss3_data;
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("true, jumpTO 0x{:x}", salutmp1.rss3_data.to_uint());
#endif
                } else {
#ifdef SPIKE_OUTPUT
                    log_str += "false";
#endif
                }
                break;

            // case BGE_:
            case DecodeParams::alu_fn_t::FN_SGE:
                hwarp->branch_sig = true;
                if (salutmp1.rss1_data >= salutmp1.rss2_data) {
                    hwarp->jump = 1;
                    hwarp->jump_addr = salutmp1.rss3_data;
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("true, jumpTO 0x{:x}", salutmp1.rss3_data.to_uint());
#endif
                } else {
#ifdef SPIKE_OUTPUT
                    log_str += "false";
#endif
                }
                break;
            // case BGEU_:
            case DecodeParams::alu_fn_t::FN_SGEU:
                hwarp->branch_sig = true;
                if (static_cast<unsigned int>(salutmp1.rss1_data)
                    >= static_cast<unsigned int>(salutmp1.rss2_data)) {
                    hwarp->jump = 1;
                    hwarp->jump_addr = salutmp1.rss3_data;
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("true, jumpTO 0x{:x}", salutmp1.rss3_data.to_uint());
#endif
                } else {
#ifdef SPIKE_OUTPUT
                    log_str += "false";
#endif
                }
                break;
            // case BLT_:
            case DecodeParams::alu_fn_t::FN_SLT:
                hwarp->branch_sig = true;
                if (salutmp1.rss1_data < salutmp1.rss2_data) {
                    hwarp->jump = 1;
                    hwarp->jump_addr = salutmp1.rss3_data;
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("true, jumpTO 0x{:x}", salutmp1.rss3_data.to_uint());
#endif
                } else {
#ifdef SPIKE_OUTPUT
                    log_str += "false";
#endif
                }
                break;
            // case BLTU_:
            case DecodeParams::alu_fn_t::FN_SLTU:
                hwarp->branch_sig = true;
                if (static_cast<unsigned int>(salutmp1.rss1_data)
                    < static_cast<unsigned int>(salutmp1.rss2_data)) {
                    hwarp->jump = 1;
                    hwarp->jump_addr = salutmp1.rss3_data;
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("true, jumpTO 0x{:x}", salutmp1.rss3_data.to_uint());
#endif
                } else {
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("false");
#endif
                }
                break;

            // case BNE_:
            case DecodeParams::alu_fn_t::FN_SNE:
                hwarp->branch_sig = true;
                if (salutmp1.rss1_data != salutmp1.rss2_data) {
                    hwarp->jump = 1;
                    hwarp->jump_addr = salutmp1.rss3_data;
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("true, jumpTO 0x{:x}", salutmp1.rss3_data.to_uint());
#endif
                } else {
#ifdef SPIKE_OUTPUT
                    log_str += fmt::format("false");
#endif
                }
                break;

            default:
                SPDLOG_LOGGER_ERROR(
                    m_logger, "SALU_CALC : switch to unrecognized ins {} @ 0x{:x}", salutmp1.ins,
                    salutmp1.ins.currentpc
                );
                assert(0);
                break;
            }
#ifdef SPIKE_OUTPUT
            SPDLOG_LOGGER_TRACE(m_logger, "{}", log_str);
#endif
        }
        ev_salufifo_pushed.notify();
    }
}

void Subcore::SALU_CTRL() {
    salu_ready = true;
    salu_ready_old = true;
    salueqb_triggered = false;
    while (true) {
        // unready和eqb不可能在同一个cycle被触发
        // 因unready表示接收了新指令，而eqb触发意味着SALU此时才从busy变ready
        // 同理，evb和eqb也不可能在同一个cycle
        wait(salu_eqb.default_event() | salu_unready | salu_evb);
        if (salu_eqb.default_event().triggered()) {
            // eq的触发发生在delta 0
            salu_ready = true;
            salu_ready_old = salu_ready;
            salueqb_triggered = true;
            wait(SC_ZERO_TIME);
            salueqb_triggered = false;
            ev_saluready_updated.notify();
        } else if (salu_evb.triggered()) {
            salu_ready = true;
            salu_ready_old = salu_ready;
            ev_saluready_updated.notify();
        } else if (salu_unready.triggered(
                   )) { // else if很重要，对于b_delay=0的情况，salu_ready不会变0
            salu_ready = false;
            salu_ready_old = salu_ready;
            ev_saluready_updated.notify();
        }
    }
}
