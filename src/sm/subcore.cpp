#include "subcore.hpp"
#include "../context_model.hpp"
#include <cstdint>
#include <spdlog/spdlog.h>
#include <systemc.h>
#include <utility>

uint32_t warpid_convert(uint32_t subcore_id, uint32_t subcore_warp_id) {
    assert(subcore_id < SUBCORE_NUM);
    assert(subcore_warp_id < SUBCORE_WARP_NUM);
    return subcore_warp_id * SUBCORE_NUM + subcore_id;
}
std::pair<uint32_t, uint32_t> warpid_convert(uint32_t warp_id) {
    assert(warp_id < hw_num_warp);
    uint32_t subcore_id = warp_id % SUBCORE_NUM;
    uint32_t subcore_warp_id = warp_id / SUBCORE_NUM;
    return { subcore_id, subcore_warp_id };
}

Subcore::Subcore(
    sc_core::sc_module_name name, uint32_t sm_id, uint32_t subcore_id,
    const std::shared_ptr<const std::vector<instable_t>>& instruction_table,
    const std::shared_ptr<const std::map<OP_TYPE, decodedat>>& decode_table,
    const lsu_req_interface& lsu_subcore_req, const warp_barrier_req_interface& warp_barrier_req,
    const warp_endprg_interface& warp_endprg, const SV39_basic& mmu,
    std::shared_ptr<spdlog::logger> logger
)
    : sc_module(name)
    , m_sm_id(sm_id)
    , m_subcore_id(subcore_id)
    , m_instruction_table(instruction_table)
    , m_decode_table(decode_table)
    , f_lsu_subcore_req(lsu_subcore_req)
    , f_warp_barrier_req(warp_barrier_req)
    , f_warp_endprg(warp_endprg)
    , m_mmu(mmu)
    , m_logger(logger ? logger : spdlog::default_logger()) {

    for (uint8_t i = 0; i < SUBCORE_WARP_NUM; i++) {
        const uint8_t warp_id = m_sm_id + i * SUBCORE_WARP_NUM;
        m_hw_warps[i] = std::make_unique<WARP_BONE>(warp_id);
        ev_warp_dispatch_list &= m_hw_warps[i]->ev_warp_dispatch;
    }

    SC_HAS_PROCESS(Subcore);

    for (int i = 0; i < SUBCORE_WARP_NUM; i++) {
        sc_core::sc_spawn(
            sc_bind(&Subcore::PROGRAM_COUNTER, this, i),
            fmt::format("warp_{}_PROGRAM_COUNTER", i).c_str()
        );
        sc_core::sc_spawn(
            sc_bind(&Subcore::INSTRUCTION_REG, this, i),
            fmt::format("warp_{}_INSTRUCTION_REG", i).c_str()
        );
        sc_core::sc_spawn(
            sc_bind(&Subcore::DECODE, this, i), fmt::format("warp_{}_DECODE", i).c_str()
        );
        sc_core::sc_spawn(
            sc_bind(&Subcore::BEFORE_DISPATCH, this, i),
            fmt::format("warp_{}_BEFORE_DISPATCH", i).c_str()
        );
        sc_core::sc_spawn(
            sc_bind(&Subcore::SIMT_STACK, this, i), fmt::format("warp_{}_SIMT_STACK", i).c_str()
        );
        sc_core::sc_spawn(
            sc_bind(&Subcore::WRITE_REG, this, i), fmt::format("warp_{}_WRITE_REG", i).c_str()
        );
    }

    // issue
    SC_THREAD(WARP_SCHEDULER);
    // opc
    SC_THREAD(OPC_FIFO);
    sensitive << clk.pos();
    SC_THREAD(OPC_FETCH);
    sensitive << clk.pos();
    SC_THREAD(OPC_EMIT);
    sensitive << clk.pos();
    // regfile
    SC_THREAD(READ_REG);
    // exec
    SC_THREAD(SALU_IN);
    sensitive << clk.pos();
    SC_THREAD(SALU_CALC);
    sensitive << clk.pos();
    SC_THREAD(SALU_CTRL);

    SC_THREAD(VALU_IN);
    sensitive << clk.pos();
    SC_THREAD(VALU_CALC);
    sensitive << clk.pos();
    SC_THREAD(VALU_CTRL);

    SC_THREAD(VFPU_IN);
    sensitive << clk.pos();
    SC_THREAD(VFPU_CALC);
    sensitive << clk.pos();
    SC_THREAD(VFPU_CTRL);

    SC_THREAD(CSR_IN);
    sensitive << clk.pos();
    SC_THREAD(CSR_CALC);
    sensitive << clk.pos();
    SC_THREAD(CSR_CTRL);

    SC_THREAD(MUL_IN);
    sensitive << clk.pos();
    SC_THREAD(MUL_CALC);
    sensitive << clk.pos();
    SC_THREAD(MUL_CTRL);

    SC_THREAD(SFU_IN);
    sensitive << clk.pos();
    SC_THREAD(SFU_CALC);
    sensitive << clk.pos();
    SC_THREAD(SFU_CTRL);

    SC_THREAD(TC_IN);
    sensitive << clk.pos();
    SC_THREAD(TC_CALC);
    sensitive << clk.pos();
    SC_THREAD(TC_CTRL);

    // writeback
    SC_THREAD(WRITE_BACK);
    sensitive << clk.pos();
}

void Subcore::PROGRAM_COUNTER(const int warp_id) {
    auto& hwarp = m_hw_warps[warp_id];
    while (true) {
        // std::cout << "SM" << sm_id << " warp" << warp_id << " PC: finish at " << sc_time_stamp()
        // << "," << sc_delta_count_at_current_time() << std::endl;
        wait(clk.posedge_event());
        // std::cout << "SM" << sm_id << " warp" << warp_id << " PC: start at " << sc_time_stamp()
        // << "," << sc_delta_count_at_current_time() << std::endl; std::cout << "PC warp" <<
        // warp_id << " start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
        // std::endl; wait(hwarp->ev_ibuf_inout); // ibuf判断swallow后，fetch新指令 std::cout << "PC
        // start, ibuf_swallow=" << ibuf_swallow << " at " << sc_time_stamp() << "," <<
        // sc_delta_count_at_current_time() << std::endl;
        if (hwarp->is_warp_activated.read()) {
            // std::cout << "warp " << warp_id << " sup at " << sc_time_stamp() << "," <<
            // sc_delta_count_at_current_time() << std::endl;
            if (rst_n == 0) {
                hwarp->pc = 0;
                hwarp->fetch_valid = false;
            } else if (hwarp->jump == 1) {
                hwarp->pc = hwarp->jump_addr;
                hwarp->fetch_valid = true;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} JUMP to 0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, warp_id), hwarp->jump_addr
                );
                // std::cout << "SM" << m_sm_id << " warp " << warp_id << " pc jumps to 0x" <<
                // std::hex
                //           << hwarp->jump_addr << std::dec << " at " << sc_time_stamp() << ","
                //           << sc_delta_count_at_current_time() << std::endl;
#endif
            } else if (hwarp->simtstk_jump == 1) {
                hwarp->pc = hwarp->simtstk_jumpaddr;
                hwarp->fetch_valid = true;
            } else if (hwarp->ibuf_empty
                       | (!hwarp->ibuf_full | (hwarp->dispatch_warp_valid && (!opc_full | doemit))
                       )) {
                // std::cout << "pc will +1 at " << sc_time_stamp() << "," <<
                // sc_delta_count_at_current_time() << std::endl;
                uint32_t old_pc = hwarp->pc.read();
                hwarp->pc = old_pc + 4;
                hwarp->fetch_valid = true;
                // 调试：打印 PC 前进情况（每1000次打印一次，避免日志过大）
                static int pc_advance_count[hw_num_warp] = {0};
                // 只在特定条件下打印，避免循环卡死
                bool should_print_pc_advance = false;
                if (m_sm_id == 1 && warp_id == 1 && old_pc >= 0x800000b4 && old_pc <= 0x800000c0) {
                    // SM1 warp1 在 0x800000b4 附近，总是打印
                    should_print_pc_advance = true;
                } else if (++pc_advance_count[warp_id] % 1000 == 0 || old_pc == 0x800000b4) {
                    // 其他情况按频率打印
                    should_print_pc_advance = true;
                }
                if (should_print_pc_advance) {
                    std::cout << "[FETCH_PC] SM" << m_sm_id << " warp" << warp_id 
                              << " PC: 0x" << std::hex << old_pc << " -> 0x" << (old_pc + 4) << std::dec
                              << " @ " << sc_time_stamp() << std::endl;
                }
            } else {
                // Debug: PC cannot advance - 大幅减少输出，只在每100000次打印一次
                static int pc_stall_count[hw_num_warp] = {0};
                pc_stall_count[warp_id]++;
                // 只在每100000次打印一次，避免日志爆炸
                if (pc_stall_count[warp_id] % 100000 == 0) {
                    std::cout << "[FETCH_PC] ⚠️ SM" << m_sm_id << " warp" << warp_id 
                              << " PC STALLED (count=" << pc_stall_count[warp_id] << ") @ " << sc_time_stamp() 
                              << " pc=0x" << std::hex << hwarp->pc.read() << std::dec
                              << " ibuf_empty=" << hwarp->ibuf_empty
                              << " ibuf_full=" << hwarp->ibuf_full
                              << " dispatch_warp_valid=" << hwarp->dispatch_warp_valid
                              << " opc_full=" << opc_full << " doemit=" << doemit << "\n";
                }
            }
        }
        hwarp->ev_fetchpc.notify(); // Not used
        if (hwarp->endprg_flush_pipe) {
            hwarp->fetch_valid = false;
        }
    }
}

void Subcore::INSTRUCTION_REG(const int warp_id) {
    bool addrOutofRangeException;
    auto& hwarp = m_hw_warps[warp_id];
    while (true) {
        // std::cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG: finish at " <<
        // sc_time_stamp() << ","
        // << sc_delta_count_at_current_time() << std::endl;
        wait(clk.posedge_event());
        // std::cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG: start at " <<
        // sc_time_stamp() << ","
        // << sc_delta_count_at_current_time() << std::endl;
        if (hwarp->is_warp_activated && rst_n != 0) {
            if (hwarp->jump == 1 | hwarp->simtstk_jump == 1) {
                hwarp->fetch_valid12 = false;
                hwarp->ev_decode.notify();
            } else if (hwarp->ibuf_empty
                       | (!hwarp->ibuf_full | (hwarp->dispatch_warp_valid && (!opc_full | doemit))
                       )) {
                hwarp->fetch_valid12 = hwarp->fetch_valid;

                // if (sm_id == 0 && warp_id == 0)
                //     std::cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG:
                //     fetch_ins pc=" << std::hex
                //     << hwarp->pc.read() << std::dec << " at " << sc_time_stamp() << "," <<
                //     sc_delta_count_at_current_time() << std::endl;
                // hwarp->fetch_ins = m_kernel->readInsBuffer(hwarp->pc.read(),
                // addrOutofRangeException);
                addrOutofRangeException = !m_mmu.memcpy(
                    hwarp->pagetable, &hwarp->fetch_ins.origin32bit, hwarp->pc.read(), 4
                );
                if (addrOutofRangeException)
                    std::cout << "SM" << m_sm_id << " warp" << warp_id << "INS_REG error: pc("
                              << std::hex << hwarp->pc.read() << std::dec << ") out of range at "
                              << sc_time_stamp() << "," << sc_delta_count_at_current_time()
                              << std::endl;
                hwarp->ev_decode.notify();
            }
        } else if (hwarp->endprg_flush_pipe) {
            hwarp->fetch_valid12 = false;
            hwarp->ev_decode.notify();
        }
    }
}

void Subcore::cycle_IBUF_ACTION(const int warp_id, I_TYPE& dispatch_ins_, I_TYPE& _readdata3) {
    auto& hwarp = m_hw_warps[warp_id];
    hwarp->ibuf_swallow = false;
    if (rst_n.read() == 0)
        hwarp->ififo.clear();
    else {
        if (hwarp->dispatch_warp_valid && (!opc_full | doemit)) {
            // std::cout << "before dispatch, ififo has " << ififo.used() << " elems at " <<
            // sc_time_stamp() <<","<< sc_delta_count_at_current_time() << std::endl;
            dispatch_ins_ = hwarp->ififo.get();
            // 调试：打印 dispatch 的指令（扩展范围到 0x80000088-0x800000c0）
            if (m_sm_id == 1 && warp_id == 1 && dispatch_ins_.currentpc >= 0x80000088 && dispatch_ins_.currentpc <= 0x800000c0) {
                uint32_t global_warp = warpid_convert(m_subcore_id, warp_id);
                std::cout << "[DISPATCH] SM" << m_sm_id << " subcore" << m_subcore_id
                          << " warp" << warp_id << " (global_warp=" << global_warp << ")"
                          << " ins=0x" << std::hex << dispatch_ins_.currentpc << std::dec
                          << " op=" << static_cast<int>(dispatch_ins_.op)
                          << " rd=" << static_cast<int>(dispatch_ins_.d)
                          << " opc_full=" << opc_full << " doemit=" << doemit
                          << " @ " << sc_time_stamp() << std::endl;
            }
            // std::cout << "IBUF: after dispatch, ififo has " << ififo.used() << " elems at " <<
            // sc_time_stamp()
            // <<","<< sc_delta_count_at_current_time() << std::endl;
        } else {
            // std::cout << "IBUF: dispatch == false at " << sc_time_stamp() <<","<<
            // sc_delta_count_at_current_time() << std::endl;
        }

        if (hwarp->fetch_valid2 && hwarp->jump == false && hwarp->simtstk_jump == false) {
            if (hwarp->ififo.isfull()) {
                // std::cout << "SM" << sm_id << " warp" << warp_id << " IFIFO is full(not error) at
                // " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
            } else {
                hwarp->ififo.push(hwarp->decode_ins.read());
                hwarp->ibuf_swallow = true;

                // std::cout << "SM" << sm_id << " warp " << warp_id << " IFIFO push decode_ins=" <<
                // hwarp->decode_ins << " at " << sc_time_stamp() << "," <<
                // sc_delta_count_at_current_time() << std::endl;
            }
            // std::cout << "before put, ififo has " << ififo.used() << " elems at " <<
            // sc_time_stamp() <<","<< sc_delta_count_at_current_time() << std::endl; std::cout <<
            // "after put, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<<
            // sc_delta_count_at_current_time() << std::endl;
        } else if (hwarp->jump || hwarp->simtstk_jump) {
            // std::cout << "ibuf detected jump at " << sc_time_stamp() <<","<<
            // sc_delta_count_at_current_time() << std::endl;
            hwarp->ififo.clear();
        }
    }
    hwarp->ibuf_empty = hwarp->ififo.isempty();
    hwarp->ibuf_full = hwarp->ififo.isfull();
    if (hwarp->ififo.isempty()) {
        hwarp->ififo_elem_num = 0;
        hwarp->ibuftop_ins = I_TYPE(INVALID_, -1, 0, 0);
    } else {
        hwarp->ibuftop_ins.write(hwarp->ififo.front());
        hwarp->ififo_elem_num = hwarp->ififo.used();
        // std::cout << "ififo has " << ififo.used() << " elems in it at " << sc_time_stamp()
        // <<","<< sc_delta_count_at_current_time() << std::endl;
    }
}

void Subcore::cycle_UPDATE_SCORE(
    const int warp_id, I_TYPE& tmpins, std::set<SCORE_TYPE>::iterator& it, REG_TYPE& regtype_,
    bool& insertscore
) {
    auto& hwarp = m_hw_warps[warp_id];
    // 调试：如果 wb_ena 为 true 但 warp_id 不匹配，打印信息（wb_ins的rd=0或1时总是打印）
    // static int wb_mismatch_count = 0;
    // bool should_print_mismatch = (wb_ena && wb_warpid != warp_id) && 
    //                               ((++wb_mismatch_count <= 100) || (wb_ins.read().d == 0) || (wb_ins.read().d == 1));
    // if (should_print_mismatch) {
    //     std::cout << "[cycle_UPDATE_SCORE] SM" << m_sm_id << " subcore" << m_subcore_id
    //               << " wb_ena=true but warp_id mismatch: wb_warpid=" << wb_warpid
    //               << " (global_warp=" << warpid_convert(m_subcore_id, wb_warpid) << ")"
    //               << " current_warp_id=" << warp_id
    //               << " (global_warp=" << warpid_convert(m_subcore_id, warp_id) << ")"
    //               << " wb_ins=0x" << std::hex << wb_ins.read().currentpc << std::dec
    //               << " rd=" << static_cast<int>(wb_ins.read().d)
    //               << " @ " << sc_time_stamp() << std::endl;
    // }
    if (wb_ena && wb_warpid == warp_id) {
        //
        // 写回阶段，删除score
        //
        tmpins = wb_ins;
        // std::cout << "scoreboard: wb_ins is " << tmpins << " at " << sc_time_stamp() <<","<<
        // sc_delta_count_at_current_time() << std::endl;
        if (tmpins.ddd.wvd) {
            if (tmpins.ddd.wxd)
                std::cout << "Scoreboard warp" << warp_id
                          << " error: wb_ins wvd=wxd=1 at the same time at " << sc_time_stamp()
                          << "," << sc_delta_count_at_current_time() << std::endl;
            regtype_ = v;
        } else if (tmpins.ddd.wxd)
            regtype_ = s;
        else
            std::cout << "Scoreboard warp" << warp_id
                      << " error: wb_ins wvd=wxd=0 at the same time at " << sc_time_stamp() << ","
                      << sc_delta_count_at_current_time() << std::endl;
        it = hwarp->score.find(SCORE_TYPE(regtype_, tmpins.d));
        // std::cout << "scoreboard写回: 正在寻找 SCORE " << SCORE_TYPE(regtype_, tmpins.d) << " at
        // " << sc_time_stamp()
        // <<","<< sc_delta_count_at_current_time() << std::endl;
        if (it == hwarp->score.end()) {
            std::cout << "warp" << warp_id
                      << "_wb_ena error: scoreboard can't find rd in score set, wb_ins=" << wb_ins
                      << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time()
                      << std::endl;
            assert(0);
        } else {
            hwarp->score.erase(it);
        }
        // std::cout << "warp" << warp_id << "_scoreboard: succesfully erased SCORE " <<
        // SCORE_TYPE(regtype_, tmpins.d)
        // << ", wb_ins=" << wb_ins << " at " << sc_time_stamp() << "," <<
        // sc_delta_count_at_current_time() << std::endl;
    }

    //
    // dispatch阶段，写入score
    //
    tmpins = hwarp->ibuftop_ins; // this ibuftop_ins is the old data
    
    // 调试：追踪 wait_bran 的变化（针对 SM1 subcore1 warp1）
    bool should_debug_wait_bran = (m_sm_id == 1 && m_subcore_id == 1 && warp_id == 1);
    bool wait_bran_before = hwarp->wait_bran;
    
    if (hwarp->branch_sig || hwarp->vbran_sig) {
        if (hwarp->wait_bran == 0)
            std::cout
                << "warp" << warp_id
                << "_scoreboard error: detect (v)branch_sig=1(from salu) while wait_bran=0 at "
                << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
        else if (hwarp->dispatch_warp_valid && (!opc_full | doemit))
            std::cout << "warp" << warp_id
                      << "_scoreboard error: detect (v)branch_sig=1(from salu) while dispatch=1 at "
                      << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
        
        if (should_debug_wait_bran && wait_bran_before == 1) {
            uint32_t global_warp = warpid_convert(m_subcore_id, warp_id);
            std::cout << "[cycle_UPDATE_SCORE] SM" << m_sm_id << " subcore" << m_subcore_id
                      << " warp" << warp_id << " (global_warp=" << global_warp << ")"
                      << " CLEAR wait_bran: branch_sig=" << hwarp->branch_sig
                      << " vbran_sig=" << hwarp->vbran_sig
                      << " jump=" << hwarp->jump
                      << " @ " << sc_time_stamp() << std::endl;
        }
        
        hwarp->wait_bran = 0;
    } else if ((tmpins.ddd.branch != 0) && hwarp->dispatch_warp_valid
               && (!opc_full | doemit)) // 表示将要dispatch
    {
        if (should_debug_wait_bran && wait_bran_before == 0) {
            uint32_t global_warp = warpid_convert(m_subcore_id, warp_id);
            std::cout << "[cycle_UPDATE_SCORE] SM" << m_sm_id << " subcore" << m_subcore_id
                      << " warp" << warp_id << " (global_warp=" << global_warp << ")"
                      << " SET wait_bran=1: ins=0x" << std::hex << tmpins.currentpc << std::dec
                      << " op=" << static_cast<int>(tmpins.op)
                      << " branch=" << static_cast<int>(tmpins.ddd.branch)
                      << " dispatch_warp_valid=" << hwarp->dispatch_warp_valid
                      << " @ " << sc_time_stamp() << std::endl;
        }
        hwarp->wait_bran = 1;
    } else if (tmpins.op == OP_TYPE::ENDPRG_ && hwarp->dispatch_warp_valid
               && (!opc_full | doemit)) { // TODO: 权宜之计，让endprg后暂停dispatch
        // std::cout << "SM" << sm_id << " warp " << warp_id << " UPDATE_SCORE detect ENDPRG,
        // suspend to dispatch at "
        // << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
        hwarp->wait_bran = 1;
    }

    if (hwarp->dispatch_warp_valid && (!opc_full | doemit)) { // 加入 score
        insertscore = true;
        if (tmpins.ddd.wvd) {
            if (tmpins.ddd.wxd)
                std::cout << "Scoreboard warp" << warp_id
                          << " error: dispatch_ins wvd=wxd=1 at the same time at "
                          << sc_time_stamp() << "," << sc_delta_count_at_current_time()
                          << std::endl;
            // if (sm_id == 0 && warp_id == 0 && tmpins.d == 0)
            //     std::cout << "SM" << sm_id << " warp" << warp_id << " UPDATE_SCORE insert
            //     ins.bit=" << std::hex << tmpins.origin32bit << std::dec << " vector regfile 0 to
            //     scoreboard at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
            //     std::endl;
            regtype_ = v;
        } else if (tmpins.ddd.wxd)
            regtype_ = s;
        else
            insertscore = false;
        if (insertscore) {
            hwarp->score.insert(SCORE_TYPE(regtype_, tmpins.d));
        }
        // if (sm_id == 0)
        //     std::cout << "SM0 warp" << warp_id << "_scoreboard: insert " << SCORE_TYPE(regtype_,
        //     tmpins.d)
        //          << " because of dispatch " << tmpins << " at " << sc_time_stamp() << "," <<
        //          sc_delta_count_at_current_time() << std::endl;
    }
}

bool Subcore::cycle_JUDGE_DISPATCH(int warp_id) {
    auto& hwarp = m_hw_warps[warp_id];
    if (hwarp->wait_bran | hwarp->jump) {
        // 调试：打印为什么 can_dispatch 为 false（限制打印频率，避免日志爆炸）
        static int judge_dispatch_wait_count[hw_num_warp] = {0};
        bool should_print_wait = false;
        if (m_sm_id == 1 && warp_id == 1) {
            // SM1 warp1 只在每100000次打印一次，大幅减少输出
            judge_dispatch_wait_count[warp_id]++;
            if (judge_dispatch_wait_count[warp_id] % 100000 == 0) {
                should_print_wait = true;
            }
        }
        if (should_print_wait) {
            uint32_t global_warp = warpid_convert(m_subcore_id, warp_id);
            std::cout << "[cycle_JUDGE_DISPATCH] SM" << m_sm_id << " subcore" << m_subcore_id
                      << " warp" << warp_id << " (global_warp=" << global_warp << ")"
                      << " can_dispatch=false: wait_bran=" << hwarp->wait_bran
                      << " jump=" << hwarp->jump
                      << " (count=" << judge_dispatch_wait_count[warp_id] << ")"
                      << " @ " << sc_time_stamp() << std::endl;
        }
        return false;
    }
    if (hwarp->ififo.isempty()) {
        return false;
    }

    const auto& instr = hwarp->ififo.front();

    if (instr.op == INVALID_)
        return false;
    if (instr.op == ENDPRG_ && !hwarp->score.empty()) {
        // 调试：打印 ENDPRG 指令因为 scoreboard 不为空而无法 dispatch（限制打印频率，避免日志爆炸）
        static int judge_dispatch_endprg_count[hw_num_warp] = {0};
        bool should_print_endprg = false;
        if (m_sm_id == 1 && warp_id == 1) {
            // SM1 warp1 只在首次或每1000次打印一次（减少频率）
            if (judge_dispatch_endprg_count[warp_id]++ % 1000 == 0) {
                should_print_endprg = true;
            }
        }
        if (should_print_endprg) {
            uint32_t global_warp = warpid_convert(m_subcore_id, warp_id);
            std::cout << "[cycle_JUDGE_DISPATCH] SM" << m_sm_id << " subcore" << m_subcore_id
                      << " warp" << warp_id << " (global_warp=" << global_warp << ")"
                      << " can_dispatch=false: ENDPRG with non-empty scoreboard"
                      << " ins=0x" << std::hex << instr.currentpc << std::dec
                      << " score.size()=" << hwarp->score.size()
                      << " scoreboard=[";
            bool first = true;
            for (const auto& s : hwarp->score) {
                if (!first) std::cout << ",";
                std::cout << (s.regtype == REG_TYPE::s ? "s" : "v") << static_cast<int>(s.addr);
                first = false;
            }
            std::cout << "] (count=" << judge_dispatch_endprg_count[warp_id] << ")"
                      << " @ " << sc_time_stamp() << std::endl;
        }
        return false;
    }

    if (instr.ddd.wxd && hwarp->score.find(SCORE_TYPE(s, instr.d)) != hwarp->score.end())
        return false;
    if (instr.ddd.wvd && hwarp->score.find(SCORE_TYPE(v, instr.d)) != hwarp->score.end())
        return false;

    if (instr.ddd.sel_alu1 == DecodeParams::A1_RS1
        && hwarp->score.find(SCORE_TYPE(s, instr.s1)) != hwarp->score.end())
        return false;
    if (instr.ddd.sel_alu1 == DecodeParams::A1_VRS1
        && hwarp->score.find(SCORE_TYPE(v, instr.s1)) != hwarp->score.end())
        return false;
    if (instr.ddd.sel_alu2 == DecodeParams::sel_alu2_t::A2_RS2
        && hwarp->score.find(SCORE_TYPE(s, instr.s2)) != hwarp->score.end())
        return false;
    if (instr.ddd.sel_alu2 == DecodeParams::sel_alu2_t::A2_VRS2
        && hwarp->score.find(SCORE_TYPE(v, instr.s2)) != hwarp->score.end())
        return false;
    if (instr.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_FRS3
        && hwarp->score.find(SCORE_TYPE(s, instr.s3)) != hwarp->score.end())
        return false;
    if (instr.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_VRS3
        && hwarp->score.find(SCORE_TYPE(v, instr.s3)) != hwarp->score.end())
        return false;
    if (instr.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_PC
        && instr.ddd.branch == DecodeParams::branch_t::B_R
        && hwarp->score.find(SCORE_TYPE(s, instr.s1)) != hwarp->score.end())
        return false;
    if (instr.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_SD) {
        if (instr.ddd.isvec) {
            if (instr.ddd.readmask) {
                if (hwarp->score.find(SCORE_TYPE(v, instr.s2)) != hwarp->score.end()) {
                    return false;
                }
            } else {
                if (hwarp->score.find(SCORE_TYPE(v, instr.s3)) != hwarp->score.end()) {
                    return false;
                }
            }
        } else {
            if (hwarp->score.find(SCORE_TYPE(s, instr.s2)) != hwarp->score.end()) {
                return false;
            }
        }
    }
    return true;
}

void Subcore::BEFORE_DISPATCH(int warp_id) {
    I_TYPE dispatch_ins_;
    I_TYPE _readdata3;
    I_TYPE tmpins;
    std::set<SCORE_TYPE>::iterator it;
    REG_TYPE regtype_;
    bool insertscore = false;

    auto& hwarp = m_hw_warps[warp_id];
    while (true) {
        wait(ev_warp_assigned);
        if (hwarp->is_warp_activated) {
            cycle_IBUF_ACTION(warp_id, dispatch_ins_, _readdata3);
            cycle_UPDATE_SCORE(warp_id, tmpins, it, regtype_, insertscore);
            bool old_can_dispatch = hwarp->can_dispatch;
            hwarp->can_dispatch = cycle_JUDGE_DISPATCH(warp_id);
            hwarp->ev_warp_dispatch.notify();
        } else {
            // 某个warp结束后，依然出发issue_list，否则warp_scheduler无法运行
            hwarp->ev_warp_dispatch.notify();
        }
        if (hwarp->endprg_flush_pipe) {
            hwarp->ififo.clear();
            hwarp->wait_bran = false;
        }
    }
}

void Subcore::lsu_writeback(
    const I_TYPE& instr, int subcore_warp_id, std::unique_ptr<std::array<reg_t, hw_num_thread>> data
) {
    assert(data != nullptr);
    assert(subcore_warp_id < SUBCORE_WARP_NUM);
    lsu_out_t lsu_out;
    lsu_out.ins = instr;
    lsu_out.warp_id = subcore_warp_id;
    lsu_out.rdv1_data = std::move(data);
    lsufifo.push(std::move(lsu_out));
}

void Subcore::warp_barrier_set(int subcore_warp_id, bool val) {
    assert(subcore_warp_id < m_hw_warps.size());
    assert(wait_barrier.at(subcore_warp_id) != val);
    wait_barrier.at(subcore_warp_id) = val;
}

void Subcore::receive_warp(
    uint32_t blk_idx_in_kernel, uint32_t warp_idx_in_blk, std::shared_ptr<kernel_info_t> kernel,
    uint32_t lds_baseaddr, uint32_t blk_slot_idx, uint32_t subcore_warp_idx
) {
    auto& hwarp = m_hw_warps.at(subcore_warp_idx);
    assert(hwarp && !hwarp->is_warp_activated && !hwarp->will_warp_activate);

    hwarp->will_warp_activate = true;

    // 将软件warp(线程束)派发到硬件warp
    dim3 block_idx_3d = kernel->get_next_cta_id();
    hwarp->m_ctaid_in_core = blk_slot_idx;
    hwarp->CSR_reg[0x800] = warp_idx_in_blk * kernel->get_num_thread_per_warp();
    hwarp->CSR_reg[0x801] = kernel->get_num_warp_per_cta();
    hwarp->CSR_reg[0x802] = kernel->get_num_thread_per_warp();
    hwarp->CSR_reg[0x803] = kernel->get_metadata_baseaddr();
    hwarp->CSR_reg[0x804] = blk_slot_idx;
    hwarp->CSR_reg[0x805] = warp_idx_in_blk;
    hwarp->CSR_reg[0x806] = ldsBaseAddr_core + lds_baseaddr;
    hwarp->CSR_reg[0x807] = kernel->get_pdsBaseAddr()
        + (blk_idx_in_kernel * kernel->get_num_warp_per_cta() + warp_idx_in_blk)
            * kernel->get_num_thread_per_warp() * kernel->get_pdsSize_per_thread();
    hwarp->CSR_reg[0x808] = block_idx_3d.x;
    hwarp->CSR_reg[0x809] = block_idx_3d.y;
    hwarp->CSR_reg[0x80a] = block_idx_3d.z;
    hwarp->CSR_reg[0x300] = 0x00001800; // WHY? CSR[mstatus] default value

    hwarp->is_warp_activated.write(true);
    hwarp->fetch_valid.write(true);
    hwarp->pc.write(kernel->get_startaddr());
    hwarp->pagetable = kernel->get_pagetable();
    SPDLOG_LOGGER_DEBUG(m_logger, "[Subcore::receive_warp] SM{} subcore{} warp{} kernel->get_pagetable()=0x{:x} -> hwarp->pagetable=0x{:x}", 
        m_sm_id, m_subcore_id, subcore_warp_idx, kernel->get_pagetable(), hwarp->pagetable);
    hwarp->num_thread = kernel->get_num_thread_per_warp();
    hwarp->blk_slot_idx = blk_slot_idx;
    hwarp->warp_idx_in_blk = warp_idx_in_blk;

    sc_bv<hw_num_thread> _validmask = 0;
    for (int i = 0; i < kernel->get_num_thread_per_warp(); i++) {
        _validmask[i] = 1;
    }
    hwarp->current_mask.write(_validmask);

    wait_barrier.at(subcore_warp_idx) = false;
}

void Subcore::exec_calc_helper(
    const I_TYPE& ins, const int num_thread_active, const std::array<iuf32_t, hw_num_thread>& src1,
    const std::array<iuf32_t, hw_num_thread>& src2, const std::array<iuf32_t, hw_num_thread>& src3,
    std::array<iuf32_t, hw_num_thread>& dst, std::function<iuf32_t(iuf32_t, iuf32_t, iuf32_t)> calc
) {
    if (ins.ddd.isvec) {
        for (int i = 0; i < num_thread_active; i++) {
            if (ins.mask[i]) {
                const iuf32_t& src1_
                    = src1[ins.ddd.sel_alu1 == DecodeParams::sel_alu1_t::A1_VRS1 ? i : 0];
                const iuf32_t& src2_
                    = src2[ins.ddd.sel_alu2 == DecodeParams::sel_alu2_t::A2_VRS2 ? i : 0];
                const iuf32_t& src3_
                    = src3[ins.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_VRS3 ? i : 0];
                if (ins.ddd.reverse) {
                    dst[i] = calc(src2_, src1_, src3_);
                } else {
                    dst[i] = calc(src1_, src2_, src3_);
                }
            }
        }
    } else {
        assert(ins.ddd.reverse == false);
        dst[0] = calc(src1[0], src2[0], src3[0]);
    }
};
