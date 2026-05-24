#include "subcore.hpp"
#include <spdlog/spdlog.h>

void Subcore::CSR_IN() {
    csr_in_t new_data;
    int a_delay, b_delay;
    while (true) {
        wait();
        if (emito_csr) {
            if (csr_ready_old == false) {
                std::cout << "csr error: not ready at " << sc_time_stamp() << ","
                          << sc_delta_count_at_current_time() << "\n";
            }
            csr_unready.notify();
            new_data.ins = emit_ins;
            new_data.warp_id = emitins_warpid;

            new_data.csrSdata1 = tocsr_data1;
            new_data.csrSdata2 = tocsr_data2;

            csr_dq.push(new_data);
            // if (sm_id == 0)
            // std::cout << "SM" << sm_id << " CSR dataqueue push ins=" << new_data.ins << " at " <<
            // sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            a_delay = 0;
            b_delay = 0;

            if (a_delay == 0)
                csr_eva.notify();
            else if (csreqa_triggered)
                csr_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
            else {
                csr_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                ev_csrfifo_pushed.notify();
            }
            if (b_delay == 0)
                csr_evb.notify();
            else {
                csr_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                ev_csrready_updated.notify();
            }
        } else {
            if (!csreqa_triggered)
                ev_csrfifo_pushed.notify();
            if (!csreqb_triggered)
                ev_csrready_updated.notify();
        }
    }
}

void Subcore::CSR_CALC() {
    csrfifo_elem_num = 0;
    csrfifo_empty = true;
    csreqa_triggered = false;
    csr_in_t csrtmp1;
    csr_out_t csrtmp2;
    bool succeed;
    int t;
    while (true) {
        wait(csr_eva | csr_eqa.default_event());
        if (csr_eqa.default_event().triggered()) {
            csreqa_triggered = true;
            wait(SC_ZERO_TIME);
            csreqa_triggered = false;
        }
        csrtmp1 = csr_dq.front();
        csr_dq.pop();
        auto& hwarp = m_hw_warps[csrtmp1.warp_id];
        if (csrtmp1.ins.ddd.wxd | csrtmp1.ins.ddd.wvd) {
            csrtmp2.ins = csrtmp1.ins;
            csrtmp2.warp_id = csrtmp1.warp_id;
            int csr_addr = extractBits32(csrtmp2.ins.origin32bit, 31, 20);
            switch (csrtmp1.ins.op) {
            case CSRRW_:
                t = hwarp->CSR_reg[csr_addr];
                csrtmp2.data[0] = t;
                hwarp->CSR_reg[csr_addr] = csrtmp1.csrSdata1;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} CSR[0x{:x}]=0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, csrtmp1.warp_id), csrtmp1.ins.currentpc,
                    csrtmp1.ins, csr_addr, static_cast<uint32_t>(hwarp->CSR_reg[csr_addr])
                );
#endif
                break;
            case CSRRS_:
                t = hwarp->CSR_reg[csr_addr];
                csrtmp2.data[0] = t;
                hwarp->CSR_reg[csr_addr] = t | csrtmp1.csrSdata1;
// std::cout << "CSRRS, t=" << std::hex << t << ", csrSdata1=" << csrtmp1.csrSdata1 << std::dec <<
// ", ins.s1=" << csrtmp1.ins.s1 << "\n";
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} CSR[0x{:x}]=0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, csrtmp1.warp_id), csrtmp1.ins.currentpc,
                    csrtmp1.ins, csr_addr, static_cast<uint32_t>(hwarp->CSR_reg[csr_addr])
                );
#endif
                break;
            case CSRRSV_: {
                auto& vcsr = hwarp->CSR_vreg[csr_addr];
                csrtmp2.data = hwarp->CSR_vreg[csr_addr];
                for (int i = 0; i < hw_num_thread; i++) {
                    vcsr[i] = vcsr[i] | csrtmp1.csrSdata1;
                }
                break;
            }
            case CSRRC_:
                t = hwarp->CSR_reg[csr_addr];
                csrtmp2.data[0] = t;
                hwarp->CSR_reg[csr_addr] = t & ~csrtmp1.csrSdata1;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} CSR[0x{:x}]=0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, csrtmp1.warp_id), csrtmp1.ins.currentpc,
                    csrtmp1.ins, csr_addr, static_cast<uint32_t>(hwarp->CSR_reg[csr_addr])
                );
#endif
                break;
            case CSRRWI_:
                csrtmp2.data[0] = hwarp->CSR_reg[csr_addr];
                hwarp->CSR_reg[csr_addr] = csrtmp1.csrSdata1;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} CSR[0x{:x}]=0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, csrtmp1.warp_id), csrtmp1.ins.currentpc,
                    csrtmp1.ins, csr_addr, static_cast<uint32_t>(hwarp->CSR_reg[csr_addr])
                );
#endif
                break;
            case CSRRSI_:
                t = hwarp->CSR_reg[csr_addr];
                csrtmp2.data[0] = t;
                hwarp->CSR_reg[csr_addr] = t | csrtmp1.csrSdata1;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} CSR[0x{:x}]=0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, csrtmp1.warp_id), csrtmp1.ins.currentpc,
                    csrtmp1.ins, csr_addr, static_cast<uint32_t>(hwarp->CSR_reg[csr_addr])
                );
#endif
                break;
            case CSRRCI_:
                t = hwarp->CSR_reg[csr_addr];
                csrtmp2.data[0] = t;
                hwarp->CSR_reg[csr_addr] = t & ~csrtmp1.csrSdata1;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} CSR[0x{:x}]=0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, csrtmp1.warp_id), csrtmp1.ins.currentpc,
                    csrtmp1.ins, csr_addr, static_cast<uint32_t>(hwarp->CSR_reg[csr_addr])
                );
#endif
                break;
            case VSETVLI_:
                csrtmp2.data[0] = hwarp->num_thread;
                break;
            case SETRPC_:
                hwarp->CSR_reg[0x80c] = csrtmp1.csrSdata1 + csrtmp1.csrSdata2;
                csrtmp2.data[0] = 0;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger, "SM {} warp {} 0x{:x} {} CSR[0x{:x}]=0x{:x}", m_sm_id,
                    warpid_convert(m_subcore_id, csrtmp1.warp_id), csrtmp1.ins.currentpc,
                    csrtmp1.ins, csr_addr, static_cast<uint32_t>(hwarp->CSR_reg[csr_addr])
                );
#endif
                break;
            default:
                std::cout << "CSR_CALC warning: switch to unrecognized ins" << csrtmp1.ins << " at "
                          << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                assert(0);
                break;
            }
            csrfifo.push(csrtmp2);
            // if (sm_id == 0)
            //     std::cout << "SM" << sm_id << " CSRfifo push data ins=" << csrtmp2.ins << ",
            //     csrfifo's elem_num now is " << csrfifo.used() << " at " << sc_time_stamp() << ","
            //     << sc_delta_count_at_current_time() << "\n";
        } else {
        }

        ev_csrfifo_pushed.notify();
    }
}

void Subcore::CSR_CTRL() {
    csr_ready = true;
    csr_ready_old = true;
    csreqb_triggered = false;
    while (true) {
        wait(csr_eqb.default_event() | csr_unready | csr_evb);
        if (csr_eqb.default_event().triggered()) {
            csr_ready = true;
            csr_ready_old = csr_ready;
            csreqb_triggered = true;
            wait(SC_ZERO_TIME);
            csreqb_triggered = false;
            ev_csrready_updated.notify();
        } else if (csr_evb.triggered()) {
            csr_ready = true;
            csr_ready_old = csr_ready;
            ev_csrready_updated.notify();
        } else if (csr_unready.triggered()) {
            csr_ready = false;
            csr_ready_old = csr_ready;
            ev_csrready_updated.notify();
        }
    }
}
