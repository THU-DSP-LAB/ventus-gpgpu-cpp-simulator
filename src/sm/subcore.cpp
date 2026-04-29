#include "subcore.hpp"
#include "../gvm_dpic.hpp"
#include "../gvm_global_var.hpp"
#include "../context_model.hpp"
#include "../cyclesim_gvm.hpp"
#include "icache.hpp"
#include "sysc/kernel/sc_simcontext.h"
#include "sysc/kernel/sc_time.h"
#include <algorithm>
#include <cstdint>
#include <memory>
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
    const warp_endprg_interface& warp_endprg, const l1icache_request_interface& l1icache_request,
    const l1icache_flushpipe_interface& l1icache_flushpipe, const SV39_basic& mmu,
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
    , f_l1icache_request(l1icache_request)
    , f_l1icache_flushpipe(l1icache_flushpipe)
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

    // fetch & decode
    SC_THREAD(icache_access);
    SC_THREAD(icache_wait);
    SC_THREAD(DECODE);
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

void Subcore::export_vcd_trace(sc_core::sc_trace_file* tf, const std::string& prefix) const {
    for (int i = 0; i < SUBCORE_WARP_NUM; i++) {
        m_hw_warps[i]->export_vcd_trace(tf, fmt::format("{}.warp{}", prefix, i));
    }
    sc_trace(tf, clk, fmt::format("{}.clk", prefix));
    sc_trace(tf, rst_n, fmt::format("{}.rst_n", prefix));
    sc_trace(tf, opc_full, prefix + ".opc_full");
    sc_trace(tf, last_dispatch_warpid, prefix + ".last_dispatch_warpid");
    sc_trace(tf, issue_ins, prefix + ".issue_ins");
    sc_trace(tf, issueins_warpid, prefix + ".issueins_warpid");
    sc_trace(tf, dispatch_valid, prefix + ".dispatch_valid");
    sc_trace(tf, dispatch_ready, prefix + ".dispatch_ready");
    sc_trace(tf, opcfifo_elem_num, prefix + ".opcfifo_elem_num");
    sc_trace(tf, emit_ins, prefix + ".emit_ins");
    sc_trace(tf, emitins_warpid, prefix + ".emitins_warpid");
    sc_trace(tf, doemit, prefix + ".doemit");
    sc_trace(tf, findemit, prefix + ".findemit");
    sc_trace(tf, emit_idx, prefix + ".emit_idx");
    sc_trace(tf, emito_salu, prefix + ".emito_salu");
    sc_trace(tf, emito_valu, prefix + ".emito_valu");
    sc_trace(tf, emito_vfpu, prefix + ".emito_vfpu");
    sc_trace(tf, emito_simtstk, prefix + ".emito_simtstk");
    sc_trace(tf, emito_warpscheduler, prefix + ".emito_warpscheduler");
    // salu
    sc_trace(tf, tosalu_data1, prefix + ".tosalu_data.data1");
    sc_trace(tf, tosalu_data2, prefix + ".tosalu_data.data2");
    sc_trace(tf, tosalu_data3, prefix + ".tosalu_data.data3");
    sc_trace(tf, salu_ready, prefix + ".salu_ready");
    sc_trace(tf, salufifo_empty, prefix + ".salufifo_empty");
    sc_trace(tf, salutmp2, prefix + ".salutmp2");
    sc_trace(tf, salutop_dat, prefix + ".salutop_dat");
    sc_trace(tf, salufifo_elem_num, prefix + ".salufifo_elem_num");
    // valu
    sc_trace(tf, valu_ready, prefix + ".valu_ready");
    sc_trace(tf, valuto_simtstk, prefix + ".valuto_simtstk");
    sc_trace(tf, branch_elsemask, prefix + ".branch_elsemask");
    sc_trace(tf, branch_elsepc, prefix + ".branch_elsepc");
    sc_trace(tf, vbranch_ins, prefix + ".vbranch_ins");
    sc_trace(tf, vbranchins_warpid, prefix + ".vbranchins_warpid");
    sc_trace(tf, valufifo_empty, prefix + ".valufifo_empty");
    sc_trace(tf, valutop_dat, prefix + ".valutop_dat");
    sc_trace(tf, valufifo_elem_num, prefix + ".valufifo_elem_num");
    // simt-stack
    sc_trace(tf, emito_simtstk, "emito_simtstk");
    // vfpu
    sc_trace(tf, vfpu_ready, "vfpu_ready");
    sc_trace(tf, vfpufifo_empty, "vfpufifo_empty");
    sc_trace(tf, vfputop_dat, "vfputop_dat");
    sc_trace(tf, vfpufifo_elem_num, "vfpufifo_elem_num");
    // lsu
    sc_trace(tf, lsufifo_empty, "lsufifo_empty");
    sc_trace(tf, lsufifo_elem_num, "lsufifo_elem_num");
    // writeback
    sc_trace(tf, write_s, "write_s");
    sc_trace(tf, write_v, "write_v");
    sc_trace(tf, write_f, "write_f");
    sc_trace(tf, execpop_salu, "execpop_salu");
    sc_trace(tf, execpop_valu, "execpop_valu");
    sc_trace(tf, execpop_vfpu, "execpop_vfpu");
    sc_trace(tf, execpop_lsu, "execpop_lsu");
    sc_trace(tf, wb_ena, "wb_ena");
    sc_trace(tf, wb_ins, "wb_ins");
    sc_trace(tf, wb_warpid, "wb_warpid");
}

void WARP_BONE::export_vcd_trace(sc_core::sc_trace_file* tf, const std::string& prefix) const {
    sc_trace(tf, is_warp_activated, prefix + ".is_warp_activated");
    sc_trace(tf, pc_valid, prefix + ".pc_valid");
    sc_trace(tf, jump, prefix + ".jump");
    sc_trace(tf, branch_sig, prefix + ".branch_sig");
    sc_trace(tf, vbran_sig, prefix + ".vbran_sig");
    sc_trace(tf, jump_addr, prefix + ".jump_addr");
    sc_trace(tf, pc, prefix + ".pc");
    sc_trace(tf, decode_ins, prefix + ".decode_ins");
    sc_trace(tf, ibuf_empty, prefix + ".ibuf_empty");
    sc_trace(tf, ibuf_full, prefix + ".ibuf_full");
    sc_trace(tf, ibuftop_ins, prefix + ".ibuftop_ins");
    sc_trace(tf, ififo_elem_num, prefix + ".ififo_elem_num");
    sc_trace(tf, dispatch_warp_valid, prefix + ".dispatch_warp_valid");
    sc_trace(tf, current_mask, prefix + ".current_mask");
    sc_trace(tf, simtstk_jumpaddr, prefix + ".simtstk_jumpaddr");
    sc_trace(tf, simtstk_jump, prefix + ".simtstk_jump");
    sc_trace(tf, simtstk_jump, prefix + ".simtstk_jump");
    sc_trace(tf, simtstk_jumpaddr, prefix + ".simtstk_jumpaddr");
    sc_trace(tf, current_mask, prefix + ".current_mask");
    sc_trace(tf, vbran_sig, prefix + ".vbran_sig");
    for (int i = 0; i < s_regfile.size(); i++) {
        sc_trace(tf, s_regfile[i], fmt::format("{}.sgpr[{}]", prefix, i));
    }
}

int Subcore::l0icache_access(paddr_t pagetable_root, vaddr_t addr) const {
    auto addr_base = addr & ~(l0icache_line_size - 1);
    for (const auto& line : m_l0icache) {
        if (line.valid && line.addr_base == addr_base) {
            // hit
            return 0;
        }
    }
    return -1; // miss
}
void Subcore::l1icache_response(const ICacheRsp& rsp) {
    l1icache_rsp_queue.push(rsp);
    l1icache_rsp_queue.back().warpid = rsp.warpid; // convert to local warpid
    ev_l1icache_rsp.notify();
    SPDLOG_LOGGER_TRACE(
        m_logger, "SM {} warp {} ICACHE response: 0x{:x} hit={}", m_sm_id,
        warpid_convert(m_subcore_id, rsp.warpid), rsp.addr, rsp.hit
    );

    // find l0 icache victim line to replace
    auto item = std::find_if(m_l0icache.begin(), m_l0icache.end(), [](const l0icache_line_t& line) {
        return !line.valid;
    });
    if (item == m_l0icache.end()) {
        item = m_l0icache.begin() + (rand() % m_l0icache.size()); // random replacement
    }
    item->valid = true;
    item->pagetable_root = m_hw_warps[rsp.warpid]->pagetable;
    item->addr_base = rsp.addr & ~(l0icache_line_size - 1);
}

bool Subcore::ibuf_in_ready(int warp_id) const {
    const auto& hwarp = m_hw_warps.at(warp_id);
    return ibuf_in_ready(hwarp);
}
bool Subcore::ibuf_in_ready(const std::unique_ptr<WARP_BONE>& hwarp) const {
    return !hwarp->ibuf_full || (hwarp->dispatch_warp_valid && opc_in_ready());
}

bool Subcore::pc_need_rewind(int warp_id) const {
    // icache miss或者ibuf无法容纳新指令时，回溯此warp的PC并冲刷PC,fetch,fetch2流水级
    const auto& fetch2 = fetch2_reg.read();
    if (fetch2.from != fetch_t::FETCH_FROM::NONE && fetch2.warp_id == warp_id) {
        return !fetch2.success || !ibuf_in_ready(warp_id);
    } else {
        return false;
    }
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
                hwarp->pc_valid = false;
            } else if (hwarp->jump == 1) {
                hwarp->pc = hwarp->jump_addr;
                hwarp->pc_valid = true;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} JUMP to 0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, warp_id), hwarp->jump_addr
                );
#endif
            } else if (hwarp->simtstk_jump == 1) {
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} PC JUMP(simtstk) to 0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, warp_id), hwarp->jump_addr
                );
#endif
                hwarp->pc = hwarp->simtstk_jumpaddr;
                hwarp->pc_valid = true;
            } else if (pc_need_rewind(warp_id)) {
                // SPDLOG_LOGGER_TRACE(
                //     m_logger, "SM {} warp {} PC rewind to 0x{:x}", m_sm_id,
                //     warpid_convert(m_subcore_id, warp_id), fetch2_reg.read().pc
                // );
                hwarp->pc = fetch2_reg.read().pc;
                hwarp->pc_valid = true;
            } else if (warp_scheduler_fetch_select() != warp_id) {
                // FETCH not working for this warp, keep PC unchanged.
                // SPDLOG_LOGGER_TRACE(
                //     m_logger,
                //     "SM {} warp {} PC waiting because warp scheduler doesn't"
                //     "select this warp to fetch, it selects warp {}",
                //     m_sm_id, warpid_convert(m_subcore_id, warp_id), warp_scheduler_fetch_select()
                // );
            } else {
                // SPDLOG_LOGGER_TRACE(
                //     m_logger, "SM {} warp {} PC += 4 to 0x{:x}", m_sm_id,
                //     warpid_convert(m_subcore_id, warp_id), hwarp->pc.read() + 4
                // );
                hwarp->pc = hwarp->pc.read() + 4;
                hwarp->pc_valid = true;
            }
        }
        if (hwarp->endprg_flush_pipe) {
            hwarp->pc_valid = false;
        }
    }
}

// helper function to determine whether fetch is valid for a warp
// suitable for statges before IBUF: PC, FETCH, FETCH2
bool Subcore::fetch_need_flush(int warp_id) const {
    auto& hwarp = m_hw_warps.at(warp_id);
    return !hwarp->is_warp_activated.read() || hwarp->jump || hwarp->simtstk_jump
        || hwarp->endprg_flush_pipe || pc_need_rewind(warp_id);
}

bool Subcore::regext_need_clear(int warp_id) const {
    auto& hwarp = m_hw_warps.at(warp_id);
    return !hwarp->is_warp_activated.read() || hwarp->jump || hwarp->simtstk_jump
        || hwarp->endprg_flush_pipe;
}

// warp scheduler combinational logic,
// determining which warp to fetch from icache this cycle
int Subcore::warp_scheduler_fetch_select() const {
    // helper to check L1 icache response: which warp causes icache miss
    auto get_icache_miss_warp = [](const fetch_t& fetch2) -> int {
        if (fetch2.from == fetch_t::FETCH_FROM::L1ICACHE && !fetch2.success)
            return fetch2.warp_id;
        else
            return -1;
    };

    auto& fetch = fetch_reg.read();
    // which warp to fetch? default: Greedy policy
    int warp_id = fetch.warp_id >= 0 ? fetch.warp_id : 0;
    auto& hwarp = m_hw_warps.at(warp_id);
    auto icache_miss_warp = get_icache_miss_warp(fetch2_reg.read());
    if (!hwarp->is_warp_activated.read() || !hwarp->pc_valid.read() || icache_miss_warp == warp_id
        || warp_barrier_blocks_dispatch(warp_id)) {
        // @pc_invalid,@icache_miss: change to another warp
        // if there are not-barriered warps that can fetch, select them first
        // else we select barriered warps (prefill their ibuf)
        int barriered_warp_id = -1; // default: do not fetch any warp this cycle
        for (int i = 0; i < SUBCORE_WARP_NUM; i++) {
            int wid = (i + warp_id + 1) % SUBCORE_WARP_NUM;
            if (m_hw_warps.at(wid)->pc_valid.read() && !fetch_need_flush(wid)) {
                if (!warp_barrier_blocks_dispatch(wid)) {
                    return wid;
                } else {
                    barriered_warp_id = wid;
                }
            }
        }
        return barriered_warp_id;
    }
    return warp_id; // Greedy policy: keep current warp
}

void Subcore::icache_access() { // pipeline stage fetch
    while (true) {
        wait(clk.posedge_event());
        auto& fetch = fetch_reg.read();

        auto warp_id = warp_scheduler_fetch_select();
        if (warp_id < 0 || fetch_need_flush(warp_id)) { // invalid fetch (bubble)
            fetch_reg.write(fetch_t {
                .pc = 0, .warp_id = -1, .from = fetch_t::FETCH_FROM::NONE, .success = false });
            continue; // goto next cycle
        }

        // do fetch
        auto& hwarp = m_hw_warps.at(warp_id);
        bool l0i_hit = l0icache_access(hwarp->pagetable, hwarp->pc.read()) == 0;
        if (l0i_hit) { // fetch from L0 icache in subcore
            fetch_reg.write(fetch_t { .pc = hwarp->pc.read(),
                                      .warp_id = warp_id,
                                      .from = fetch_t::FETCH_FROM::L0ICACHE,
                                      .success = true });
        } else { // fetch from L1 icache in SM
            f_l1icache_request(hwarp->pagetable, hwarp->pc.read(), warp_id);
            fetch_reg.write(fetch_t { .pc = hwarp->pc.read(),
                                      .warp_id = warp_id,
                                      .from = fetch_t::FETCH_FROM::L1ICACHE,
                                      .success = false });
        }
        // SPDLOG_LOGGER_TRACE(
        //     m_logger, "SM {} warp {} FETCH from {}: 0x{:x}", m_sm_id,
        //     warpid_convert(m_subcore_id, warp_id), l0i_hit ? "L0ICACHE" : "L1ICACHE",
        //     hwarp->pc.read()
        // );
    }
}

void Subcore::icache_wait() { // pipeline stage fetch2
    auto get_icache_miss_warp = [](const fetch_t& fetch2) -> int {
        if (fetch2.from == fetch_t::FETCH_FROM::L1ICACHE && !fetch2.success)
            return fetch2.warp_id;
        else
            return -1;
    };
    while (true) {
        wait(clk.posedge_event());
        auto& fetch = fetch_reg.read();
        // check upstream FETCH stage valid or not
        if (fetch.from == fetch_t::FETCH_FROM::NONE) { // bubble
            fetch2_reg.write(fetch_t {
                .pc = 0, .warp_id = -1, .from = fetch_t::FETCH_FROM::NONE, .success = false });
            continue;
        }
        if (fetch_need_flush(fetch.warp_id)) { // pipeline flushed, generate bubble
            fetch2_reg.write(fetch_t {
                .pc = 0, .warp_id = -1, .from = fetch_t::FETCH_FROM::NONE, .success = false });
            f_l1icache_flushpipe(fetch.warp_id);
            continue;
        }
        auto& hwarp = m_hw_warps.at(fetch.warp_id);

        // valid fetch result
        if (fetch.from == fetch_t::FETCH_FROM::L1ICACHE) {
            // wait for l1icache response
            if (!ev_l1icache_rsp.triggered()) {
                auto time = sc_time_stamp();
                auto delta = sc_delta_count_at_current_time();
                wait(ev_l1icache_rsp);
                assert(sc_time_stamp() == time && delta == sc_delta_count_at_current_time());
            }
            auto rsp = l1icache_rsp_queue.front();
            assert(fetch.warp_id == rsp.warpid && fetch.pc == rsp.addr && "L1 icache rsp mismatch");
            fetch2_reg.write(fetch_t {
                .pc = fetch.pc,
                .warp_id = fetch.warp_id,
                .from = fetch_t::FETCH_FROM::L1ICACHE,
                .success = rsp.hit,
            });
            l1icache_rsp_queue.pop();
            assert(l1icache_rsp_queue.empty() && "L1 icache rsp should be consumed instantly");

            if (!rsp.hit) {
                f_l1icache_flushpipe(fetch.warp_id);
                continue; // miss, do not exec functional model
            }
        } else { // valid fetch result from L0 icache
            // directly pass L0 icache fetch
            fetch2_reg.write(fetch);
        }

        // functional model: get actual instructions from memory
        uint32_t instr_32bits;
        bool addrOutofRangeException = !m_mmu.memcpy(hwarp->pagetable, &instr_32bits, fetch.pc, 4);
        if (addrOutofRangeException) {
            SPDLOG_LOGGER_ERROR(
                m_logger, "SM {} warp {} instruction fetch error: pc=0x{:x} out of range", m_sm_id,
                warpid_convert(m_subcore_id, fetch.warp_id), fetch.pc
            );
        }
        fetch2_instr.write(instr_32bits);
    }
}

void Subcore::cycle_IBUF_ACTION(const int warp_id) {
    auto& hwarp = m_hw_warps[warp_id];
    if (rst_n.read() == 0)
        hwarp->ififo.clear();
    else {
        if (hwarp->dispatch_warp_valid && opc_in_ready()) {
            // std::cout << "before dispatch, ififo has " << ififo.used() << " elems at " <<
            // sc_time_stamp() <<","<< sc_delta_count_at_current_time() << std::endl;
            const auto& dispatched_ins = *hwarp->ififo.front();
            if (dispatched_ins.op == OP_TYPE::BARRIER_) {
                // Mark the barrier only after the OPC ready/valid handshake is accepted.
                warp_barrier_dispatch_to_opc(warp_id);
            }
            hwarp->ififo.pop();
            // std::cout << "IBUF: after dispatch, ififo has " << ififo.used() << " elems at " <<
            // sc_time_stamp()
            // <<","<< sc_delta_count_at_current_time() << std::endl;
        } else {
            // std::cout << "IBUF: dispatch == false at " << sc_time_stamp() <<","<<
            // sc_delta_count_at_current_time() << std::endl;
        }

        auto& fetch_output = fetch2_reg.read();
        if (hwarp->jump || hwarp->simtstk_jump) {
            // std::cout << "ibuf detected jump at " << sc_time_stamp() <<","<<
            // sc_delta_count_at_current_time() << std::endl;
            hwarp->ififo.clear();
        } else if (fetch_output.warp_id != warp_id) {
            // not this warp's fetch output, do noting
        } else { // have a valid fetch output for this warp
                 // wait for decode logic (mainly combinational except regext part)
            if (fetch_output.from != fetch_t::FETCH_FROM::NONE && fetch_output.success) {
                // SPDLOG_LOGGER_TRACE(
                //     m_logger, "SM {} warp {} IBUF wait for decode pc=0x{:x}", m_sm_id,
                //     warpid_convert(m_subcore_id, warp_id), fetch_output.pc
                // );
            }
            if (!ev_decode_finish.triggered()) {
                wait(ev_decode_finish);
            }
            auto& instr = decode_output.instr;
            if (decode_output.instr != nullptr) { // valid decode result, try to put it into IBUF
                assert(decode_output.warp_id == warp_id);
                if (!ibuf_in_ready(hwarp)) {
                    // std::cout << "SM" << sm_id << " warp" << warp_id << " IFIFO is full(not
                    // error) at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
                    // std::endl;
                    SPDLOG_LOGGER_WARN(
                        m_logger,
                        "SM {} warp {} 0x{:x} {} decode output valid but IBUF is full (error?)",
                        m_sm_id, warpid_convert(m_subcore_id, warp_id), instr->currentpc, *instr
                    );
                } else {
                    hwarp->ififo.push(instr);
                    // SPDLOG_LOGGER_TRACE(
                    //     m_logger, "SM {} warp {} 0x{:x} {} IBUFFER received", m_sm_id,
                    //     warpid_convert(m_subcore_id, warp_id), instr->currentpc, *instr
                    // );
                }
                // std::cout << "before put, ififo has " << ififo.used() << " elems at " <<
                // sc_time_stamp() <<","<< sc_delta_count_at_current_time() << std::endl; std::cout
                // << "after put, ififo has " << ififo.used() << " elems at " << sc_time_stamp()
                // <<","<< sc_delta_count_at_current_time() << std::endl;
            }
        }
    }
    hwarp->ibuf_empty = hwarp->ififo.isempty();
    hwarp->ibuf_full = hwarp->ififo.isfull();
    if (hwarp->ififo.isempty()) {
        hwarp->ififo_elem_num = 0;
        hwarp->ibuftop_ins = std::make_shared<I_TYPE>(INVALID_, -1, 0, 0);
    } else {
        hwarp->ibuftop_ins.write(hwarp->ififo.front());
        hwarp->ififo_elem_num = hwarp->ififo.used();
        // std::cout << "ififo has " << ififo.used() << " elems in it at " << sc_time_stamp()
        // <<","<< sc_delta_count_at_current_time() << std::endl;
    }
}

void Subcore::cycle_UPDATE_SCORE(const int warp_id) {
    auto& hwarp = m_hw_warps[warp_id];
    if (wb_ena && wb_warpid == warp_id) {
        //
        // 写回阶段，删除score
        //
        auto& tmpins = wb_ins.read();
        REG_TYPE regtype_;
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
        auto it = hwarp->score.find(SCORE_TYPE(regtype_, tmpins.d));
        // std::cout << "scoreboard写回: 正在寻找 SCORE " << SCORE_TYPE(regtype_, tmpins.d) << " at
        // " << sc_time_stamp()
        // <<","<< sc_delta_count_at_current_time() << std::endl;
        if (it == hwarp->score.end()) {
            SPDLOG_LOGGER_ERROR(
                m_logger, "SM {} warp {} 0x{:x} {} SCOREB: can't found this writeback instr",
                m_sm_id, warpid_convert(m_subcore_id, warp_id), wb_ins.read().currentpc,
                wb_ins.read()
            );
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
    auto& tmpins = *hwarp->ibuftop_ins.read(); // this ibuftop_ins is the old data
    if (hwarp->branch_sig || hwarp->vbran_sig) {
        if (hwarp->wait_bran == 0)
            std::cout
                << "warp" << warp_id
                << "_scoreboard error: detect (v)branch_sig=1(from salu) while wait_bran=0 at "
                << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
        else if (hwarp->dispatch_warp_valid && opc_in_ready())
            std::cout << "warp" << warp_id
                      << "_scoreboard error: detect (v)branch_sig=1(from salu) while dispatch=1 at "
                      << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
        hwarp->wait_bran = 0;
    } else if (hwarp->dispatch_warp_valid && (tmpins.ddd.branch != 0)
               && opc_in_ready()) // 表示将要dispatch
    {
        // std::cout << "ibuf let wait_bran=1 at " << sc_time_stamp() <<","<<
        // sc_delta_count_at_current_time() << std::endl;
        hwarp->wait_bran = 1;
    } else if (hwarp->dispatch_warp_valid && tmpins.op == OP_TYPE::ENDPRG_
               && opc_in_ready()) { // TODO: 权宜之计，让endprg后暂停dispatch
        // std::cout << "SM" << sm_id << " warp " << warp_id << " UPDATE_SCORE detect ENDPRG,
        // suspend to dispatch at "
        // << sc_time_stamp() << "," << sc_delta_count_at_current_time() << std::endl;
        hwarp->wait_bran = 1;
    }

    if (hwarp->dispatch_warp_valid && opc_in_ready()) { // 加入 score
        bool insertscore = true;
        REG_TYPE regtype_;
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
        if (insertscore)
            hwarp->score.insert(SCORE_TYPE(regtype_, tmpins.d));
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
        return false;
    }
    if (hwarp->ififo.isempty()) {
        return false;
    }
    if (opc_in_ready(warp_id) == false) {
        return false; // 限制OPC中1个warp只有1条指令，防止单warp在OPC出口出现乱序，导致同地址Load/Store乱序
    }

    const auto& instr = *hwarp->ififo.front();

    if (instr.op == INVALID_)
        return false;
    if (instr.op == ENDPRG_ && !hwarp->score.empty())
        return false;
    if (instr.op == CUSTOM_PRINT_ && !hwarp->score.empty())
        return false;

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
    auto& hwarp = m_hw_warps[warp_id];
    while (true) {
        wait(ev_warp_assigned);
        if (hwarp->is_warp_activated) {
            cycle_IBUF_ACTION(warp_id);
            cycle_UPDATE_SCORE(warp_id);
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

bool Subcore::opc_in_ready() const {
    return !opc_full.read() || doemit.read(); // not full
}
bool Subcore::opc_in_ready(int warp_id) const {
    if (!opc_in_ready())
        return false;
    for (int i = 0; i < opcfifo.get_size(); i++) {
        if (opcfifo[i].warp_id == warp_id && opcfifo.tag_valid(i)) {
            return false;
        }
    }
    return true;
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
    auto& state = warp_barrier_state.at(subcore_warp_id);
    if (val) {
        assert(state == WarpBarrierState::DispatchedToOpc);
        state = WarpBarrierState::WaitingAtBarrier;
        return;
    }
    state = WarpBarrierState::None;
}

bool Subcore::warp_barrier_blocks_dispatch(int subcore_warp_id) const {
    return warp_barrier_state.at(subcore_warp_id) != WarpBarrierState::None;
}

void Subcore::warp_barrier_dispatch_to_opc(int subcore_warp_id) {
    auto& state = warp_barrier_state.at(subcore_warp_id);
    assert(state == WarpBarrierState::None);
    state = WarpBarrierState::DispatchedToOpc;
}

void Subcore::receive_warp(
    uint32_t blk_idx_in_kernel, uint32_t warp_idx_in_blk, std::shared_ptr<kernel_info_t> kernel,
    uint32_t lds_baseaddr, uint32_t blk_slot_idx, uint32_t subcore_warp_idx
) {
    (void)blk_idx_in_kernel;
    auto& hwarp = m_hw_warps.at(subcore_warp_idx);
    assert(hwarp && !hwarp->is_warp_activated && !hwarp->will_warp_activate);
    assert(warp_barrier_state.at(subcore_warp_idx) == WarpBarrierState::None);

    // SPDLOG_LOGGER_TRACE(
    //     m_logger, "SM {} warp {} receive warp: kernel {}, blk_idx_in_kernel {}, warp_idx_in_blk
    //     {}", m_sm_id, warpid_convert(m_subcore_id, subcore_warp_idx), kernel->get_kname(),
    //     blk_idx_in_kernel, warp_idx_in_blk
    // );

    hwarp->will_warp_activate = true;

    // 将软件warp(线程束)派发到硬件warp
    dim3 block_idx_3d = kernel->get_next_cta_id();
    hwarp->CSR_reg[0x300] = 0x00001800; // WHY? CSR[mstatus] default value
    hwarp->CSR_reg[0x800] = warp_idx_in_blk * kernel->get_num_thread_per_warp();
    hwarp->CSR_reg[0x801] = kernel->get_num_warp_per_cta();
    hwarp->CSR_reg[0x802] = kernel->get_num_thread_per_warp();
    hwarp->CSR_reg[0x803] = kernel->get_metadata_baseaddr();
    hwarp->CSR_reg[0x804] = blk_slot_idx;
    hwarp->CSR_reg[0x805] = warp_idx_in_blk;
    hwarp->CSR_reg[0x806] = ldsBaseAddr_core + lds_baseaddr;
    const uint64_t slot_linear = static_cast<uint64_t>(m_sm_id) * MAX_CTA_PER_CORE + blk_slot_idx;
    const uint64_t pds_bytes_per_warp =
        static_cast<uint64_t>(kernel->get_num_thread_per_warp()) * kernel->get_pdsSize_per_thread();
    const uint64_t wg_pds_base = static_cast<uint64_t>(kernel->get_pdsBaseAddr())
        + slot_linear * static_cast<uint64_t>(kernel->get_num_warp_per_cta()) * pds_bytes_per_warp;
    hwarp->CSR_reg[0x807] = wg_pds_base;
    hwarp->CSR_reg[0x808] = block_idx_3d.x;
    hwarp->CSR_reg[0x809] = block_idx_3d.y;
    hwarp->CSR_reg[0x80a] = block_idx_3d.z;
    hwarp->CSR_reg[0x80b] = 0; // printf buffer base addr, TODO

    dim3 threadIdxG_base;
    auto num_thread_per_blk = kernel->get_num_thread_local_3d();
    threadIdxG_base.x = (block_idx_3d.x * num_thread_per_blk.x);
    threadIdxG_base.y = (block_idx_3d.y * num_thread_per_blk.y);
    threadIdxG_base.z = (block_idx_3d.z * num_thread_per_blk.z);
    auto& threadIdxG_x = hwarp->CSR_vreg[0x80d];
    auto& threadIdxG_y = hwarp->CSR_vreg[0x80e];
    auto& threadIdxG_z = hwarp->CSR_vreg[0x80f];
    auto& threadIdxG_1d = hwarp->CSR_vreg[0x810];
    auto& threadIdxL_x = hwarp->CSR_vreg[0x811];
    auto& threadIdxL_y = hwarp->CSR_vreg[0x812];
    auto& threadIdxL_z = hwarp->CSR_vreg[0x813];
    auto threadIdxG_offset = kernel->get_threadIdx_offset_3d();
    auto threadIdxL_1d_base = kernel->get_num_thread_per_warp() * warp_idx_in_blk;
    for (int i = 0; i < hw_num_thread; i++) {
        const auto& blksz = num_thread_per_blk;
        threadIdxL_x[i] = (threadIdxL_1d_base + i) % (blksz.x);
        threadIdxL_y[i] = (threadIdxL_1d_base + i) % (blksz.x * blksz.y) / blksz.x;
        threadIdxL_z[i] = (threadIdxL_1d_base + i) / (blksz.x * blksz.y);
        threadIdxG_x[i] = threadIdxG_base.x + threadIdxL_x[i] + threadIdxG_offset.x;
        threadIdxG_y[i] = threadIdxG_base.y + threadIdxL_y[i] + threadIdxG_offset.y;
        threadIdxG_z[i] = threadIdxG_base.z + threadIdxL_z[i] + threadIdxG_offset.z;
        threadIdxG_1d[i] = (threadIdxG_base.x + threadIdxL_x[i])
            + (threadIdxG_base.y + threadIdxL_y[i]) * blksz.x
            + (threadIdxG_base.z + threadIdxL_z[i]) * blksz.x * blksz.y;
    }

    hwarp->is_warp_activated.write(true);
    hwarp->pc_valid.write(true);
    hwarp->pc.write(kernel->get_startaddr());
    hwarp->pagetable = kernel->get_pagetable();
    hwarp->num_thread = kernel->get_num_thread_per_warp();
    auto local_num_thread_3d = kernel->get_num_thread_local_3d();
    auto local_num_thread_1d
        = local_num_thread_3d.x * local_num_thread_3d.y * local_num_thread_3d.z;
    hwarp->num_thread = std::min(
        hwarp->num_thread,
        (int)(local_num_thread_1d - warp_idx_in_blk * kernel->get_num_thread_per_warp())
    );
    hwarp->blk_slot_idx = blk_slot_idx;
    hwarp->warp_idx_in_blk = warp_idx_in_blk;

    sc_bv<hw_num_thread> _validmask = 0;
    for (int i = 0; i < hwarp->num_thread; i++) {
        _validmask[i] = 1;
    }
    hwarp->current_mask.write(_validmask);

    if (cyclesim_gvm_enabled()) {
        const auto hw_warp_id = warpid_convert(m_subcore_id, subcore_warp_idx);
        hwarp->dispatch_id = 0;
        const uint64_t software_wg_id =
            ventus_cyclesim_gvm_get_kernel_wg_id_base(kernel->get_kid())
            + static_cast<uint64_t>(blk_idx_in_kernel);
        g_sgprUsage = static_cast<uint32_t>(kernel->get_metadata().sgprUsage);
        g_vgprUsage = static_cast<uint32_t>(kernel->get_metadata().vgprUsage);
        c_GvmDutCta2Warp(
            static_cast<int>(software_wg_id), static_cast<int>(warp_idx_in_blk),
            static_cast<int>(m_sm_id), static_cast<int>(hw_warp_id), 0, 0,
            static_cast<int>(blk_slot_idx), static_cast<int>(hwarp->CSR_reg[0x806]), hwarp->num_thread
        );
        const uint32_t xreg_words
            = std::min<uint32_t>(static_cast<uint32_t>(kernel->get_metadata().sgprUsage), hwarp->s_regfile.size());
        for (uint32_t i = 0; i < xreg_words; ++i) {
            c_GvmDutWarpXRegInit(
                static_cast<int>(m_sm_id), static_cast<int>(hw_warp_id),
                static_cast<int>(hwarp->s_regfile[i]), static_cast<int>(i)
            );
        }
    }
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
