#include "subcore.hpp"
#include "../gvm_dpic.hpp"
#include "../cyclesim_gvm.hpp"

void Subcore::WRITE_BACK() {
    // FloatAndInt newFI;

    while (true) {
        wait(
            ev_salufifo_pushed & ev_valufifo_pushed & ev_vfpufifo_pushed & ev_lsufifo_pushed
            & ev_csrfifo_pushed & ev_mulfifo_pushed & ev_sfufifo_pushed & ev_tcfifo_pushed
        );
        if (execpop_salu) {
            salufifo.pop();
        }
        if (execpop_valu)
            valufifo.pop();
        if (execpop_vfpu)
            vfpufifo.pop();
        if (execpop_lsu)
            lsufifo.pop();
        if (execpop_csr)
            csrfifo.pop();
        if (execpop_mul)
            mulfifo.pop();
        if (execpop_sfu)
            sfufifo.pop();
        if (execpop_tc)
            tcfifo.pop();

        salufifo_empty = salufifo.isempty();
        if (!salufifo_empty)
            salutop_dat = salufifo.front();
        salufifo_elem_num = salufifo.used();
        valufifo_empty = valufifo.isempty();
        if (!valufifo_empty)
            valutop_dat = valufifo.front();
        valufifo_elem_num = valufifo.used();
        vfpufifo_empty = vfpufifo.isempty();
        if (!vfpufifo_empty)
            vfputop_dat = vfpufifo.front();
        vfpufifo_elem_num = vfpufifo.used();
        lsufifo_empty = lsufifo.empty();
        lsufifo_elem_num = lsufifo.size();
        assert(lsufifo.size() <= 10);
        csrfifo_empty = csrfifo.isempty();
        if (!csrfifo_empty)
            csrtop_dat = csrfifo.front();
        csrfifo_elem_num = csrfifo.used();
        mulfifo_empty = mulfifo.isempty();
        if (!mulfifo_empty)
            multop_dat = mulfifo.front();
        mulfifo_elem_num = mulfifo.used();
        sfufifo_empty = sfufifo.isempty();
        if (!sfufifo_empty)
            sfutop_dat = sfufifo.front();
        sfufifo_elem_num = sfufifo.used();
        tcfifo_empty = tcfifo.isempty();
        if (!tcfifo_empty)
            tctop_dat = tcfifo.front();
        tcfifo_elem_num = tcfifo.used();

        execpop_valu = false;
        execpop_salu = false;
        execpop_vfpu = false;
        execpop_lsu = false;
        execpop_csr = false;
        execpop_mul = false;
        execpop_sfu = false;
        execpop_tc = false;
        
        if (salufifo_empty == false) {
            // if (sm_id == 0)
            //     std::cout << "SM" << sm_id << " WB judge popsalu, write_s=true, salutop.ins=" <<
            //     salutop_dat.ins << ",pc=" << std::hex << salutop_dat.ins.currentpc << std::dec <<
            //     " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            write_s = true;
            write_v = false;
            wb_ena = true;
            execpop_salu = true;
            wb_ins = salutop_dat.ins;
            rdv1_addr = salutop_dat.ins.d;
            rdv1_data = {salutop_dat.data};
            wb_warpid = salutop_dat.warp_id;
        } else if (valufifo_empty == false) {
            // if (sm_id == 0)
            //     std::cout << "SM" << sm_id << " WB judge popvalu, write_v=true at " <<
            //     sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            write_s = false;
            write_v = true;
            wb_ena = true;
            execpop_valu = true;
            wb_ins = valutop_dat.ins;
            rdv1_addr = valutop_dat.ins.d;
            rdv1_data = valutop_dat.rdv1_data;
            wb_warpid = valutop_dat.warp_id;
        } else if (vfpufifo_empty == false) {
            wb_ena = true;
            execpop_vfpu = true;
            wb_ins = vfputop_dat.ins;
            if (vfputop_dat.ins.ddd.wxd) // FEQ_S_等指令
            {
                // if (sm_id == 0)
                //     std::cout << "SM" << sm_id << " WB judge popvfpu, write_s=true at " <<
                //     sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                write_s = true;
                write_v = false;
                rdv1_addr = vfputop_dat.ins.d;
                rdv1_data.write({vfputop_dat.rds1_data});
            } else {
                // if (sm_id == 0)
                //     std::cout << "SM" << sm_id << " WB judge popvfpu, write_v=true at " <<
                //     sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                write_s = false;
                write_v = true;
                rdv1_addr = vfputop_dat.ins.d;
                // std::cout << "WB: let wb_ins=" << vfputop_dat.ins << "warp" <<
                // vfputop_dat.warp_id << ", rdf1_data={";
                rdv1_data = vfputop_dat.rdf1_data;
            }
            wb_warpid = vfputop_dat.warp_id;
            // std::cout << "} at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() <<
            // "\n";
        } else if (lsufifo_empty == false) {
            auto& lsutop_dat = lsufifo.front();
            execpop_lsu = true;
            if (lsutop_dat.ins.ddd.wxd) {
                write_s = true;
                write_v = false;
            } else if (lsutop_dat.ins.ddd.wvd) {
                write_s = false;
                write_v = true;
            }

            wb_ena = true;
            wb_ins = lsutop_dat.ins;
            rdv1_addr = lsutop_dat.ins.d;
            rdv1_data = *lsutop_dat.rdv1_data;
            wb_warpid = lsutop_dat.warp_id;
        } else if (csrfifo_empty == false) {
            // if (sm_id == 0)
            //     std::cout << "SM" << sm_id << " WB judge popcsr, write_s=true, csrtop.ins=" <<
            //     csrtop_dat.ins << " at " << sc_time_stamp() << "," <<
            //     sc_delta_count_at_current_time() << "\n";
            write_s = csrtop_dat.ins.ddd.wxd;
            write_v = csrtop_dat.ins.ddd.wvd;
            wb_ena = true;
            execpop_csr = true;
            wb_ins = csrtop_dat.ins;
            rdv1_addr = csrtop_dat.ins.d;
            rdv1_data = csrtop_dat.data;
            wb_warpid = csrtop_dat.warp_id;
        } else if (mulfifo_empty == false) {
            // if (sm_id == 0)
            //     std::cout << "SM" << sm_id << " WB judge popmul, at " << sc_time_stamp() << ","
            //     << sc_delta_count_at_current_time() << "\n";
            wb_ena = true;
            execpop_mul = true;
            wb_ins = multop_dat.ins;
            wb_warpid = multop_dat.warp_id;
            if (multop_dat.ins.ddd.wxd) {
                write_s = true;
                write_v = false;
                rdv1_addr = multop_dat.ins.d;
                rdv1_data = {multop_dat.rdv1_data[0]};
            } else if (multop_dat.ins.ddd.wvd) {
                write_s = false;
                write_v = true;
                rdv1_addr = multop_dat.ins.d;
                rdv1_data = multop_dat.rdv1_data;
            }
        } else if (sfufifo_empty == false) {
            // if (sm_id == 0)
            //     std::cout << "SM" << sm_id << " WB judge popsfu, at " << sc_time_stamp() << ","
            //     << sc_delta_count_at_current_time() << "\n";
            wb_ena = true;
            execpop_sfu = true;
            wb_ins = sfutop_dat.ins;
            wb_warpid = sfutop_dat.warp_id;
            if (sfutop_dat.ins.ddd.wxd) {
                write_s = true;
                write_v = false;
                rdv1_addr = sfutop_dat.ins.d;
                rdv1_data = {sfutop_dat.rdv1_data[0]};
            } else if (sfutop_dat.ins.ddd.wvd) {
                write_s = false;
                write_v = true;
                rdv1_addr = sfutop_dat.ins.d;
                rdv1_data = sfutop_dat.rdv1_data;
            }
        } else if (tcfifo_empty == false) {
            write_s = false;
            write_v = true;
            wb_ena = true;
            execpop_tc = true;
            wb_ins = tctop_dat.ins;
            rdv1_addr = tctop_dat.ins.d;
            rdv1_data = tctop_dat.rdv1_data;
            wb_warpid = tctop_dat.warp_id;
        } else {
            // if (sm_id == 0)
            //     std::cout << "SM" << sm_id << " WB judge not writeback, at " << sc_time_stamp()
            //     << "," << sc_delta_count_at_current_time() << "\n";
            write_s = false;
            write_v = false;
            wb_ena = false;
        }

        if (wb_ena && cyclesim_gvm_enabled()) {
            const auto wb_ins_value = wb_ins.read();
            const auto wb_data = rdv1_data.read();
            const auto hw_warp_id = warpid_convert(m_subcore_id, wb_warpid);
            const int reg_idx = rdv1_addr.read();
            if (write_s) {
                c_GvmDutXRegWriteback(
                    static_cast<int>(m_sm_id), static_cast<int>(wb_data[0]), true, reg_idx,
                    static_cast<int>(hw_warp_id), static_cast<int>(wb_ins_value.currentpc),
                    static_cast<int>(wb_ins_value.origin32bit),
                    static_cast<int>(wb_ins_value.dispatch_id)
                );
            } else if (write_v) {
                for (int thread_idx = 0; thread_idx < hw_num_thread; ++thread_idx) {
                    c_GvmDutVRegWriteback(
                        static_cast<int>(m_sm_id), static_cast<int>(wb_data[thread_idx]),
                        true, reg_idx, static_cast<int>(hw_warp_id),
                        static_cast<int>(wb_ins_value.currentpc),
                        static_cast<int>(wb_ins_value.origin32bit),
                        static_cast<int>(wb_ins_value.dispatch_id),
                        wb_ins_value.mask[thread_idx] == 1,
                        thread_idx
                    );
                }
            }
        }
    }
}
