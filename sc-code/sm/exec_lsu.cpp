#include "BASE.h"

void BASE::LSU_IN()
{
    lsu_in_t new_data;
    int a_delay, b_delay;
    while (true)
    {
        wait();
        if (emito_lsu)
        {
            if (lsu_ready_old == false)
            {
                cout << "lsu error: not ready at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            lsu_unready.notify();

            new_data.ins = emit_ins;
            new_data.warp_id = emitins_warpid;
            for (int i = 0; i < hw_num_thread; i++)
            {
                new_data.rsv1_data[i] = tolsu_data1[i];
                new_data.rsv2_data[i] = tolsu_data2[i];
                new_data.rsv3_data[i] = tolsu_data3[i];
            }
            lsu_dq.push(new_data);
            a_delay = 15;
            b_delay = 5;
            if (a_delay == 0)
                lsu_eva.notify();
            else if (lsueqa_triggered)
                lsu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
            else
            {
                lsu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                ev_lsufifo_pushed.notify();
            }
            if (b_delay == 0)
                lsu_evb.notify();
            else
            {
                lsu_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                ev_lsuready_updated.notify();
            }

            cout << "SM" << sm_id << " warp " << emitins_warpid << " 0x" << std::hex << emit_ins.read().currentpc << " " << emit_ins << " LSU addr=" << std::hex << std::setw(8) << std::setfill('0');
            switch (emit_ins.read().op)
            {
            case LW_:
                cout << new_data.rsv1_data[0] << std::setw(0) << "+" << std::setw(8) << new_data.rsv2_data[0];
                break;
            case SW_:
                cout << new_data.rsv1_data[0] << std::setw(0) << "+" << std::setw(8) << new_data.rsv2_data[0];
                break;
            case VLE32_V_:
                cout << new_data.rsv1_data[0];
                break;
            }
            cout << std::setw(0) << std::setfill(' ') << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        }
        else
        {
            if (!lsueqa_triggered)
                ev_lsufifo_pushed.notify();
            if (!lsueqb_triggered)
                ev_lsuready_updated.notify();
        }
    }
}

void BASE::LSU_CALC()
{
    lsufifo_elem_num = 0;
    lsufifo_empty = 1;
    lsueqa_triggered = false;
    lsu_in_t lsutmp1;
    lsu_out_t lsutmp2;
    bool succeed;
    unsigned int external_addr;
    bool addrOutofRangeException;
    std::array<int, hw_num_thread> LSUaddr;
    while (true)
    {
        wait(lsu_eva | lsu_eqa.default_event());
        if (lsu_eqa.default_event().triggered())
        {
            lsueqa_triggered = true;
            wait(SC_ZERO_TIME);
            lsueqa_triggered = false;
        }
        // cout << "LSU_OUT: triggered by eva/eqa at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        lsutmp1 = lsu_dq.front();
        lsu_dq.pop();

        for (int i = 0; i < hw_num_thread; i++)
        {
            LSUaddr[i] = (lsutmp1.ins.ddd.isvec & lsutmp1.ins.ddd.disable_mask)
                             ? lsutmp1.ins.ddd.is_vls12()
                                   ? (lsutmp1.rsv1_data[i] + lsutmp1.rsv2_data[i])
                                   : ((lsutmp1.rsv1_data[i] + lsutmp1.rsv2_data[i]) * hw_num_thread + i << 2 + m_hw_warps[lsutmp1.warp_id]->CSR_reg[0x807])
                         : lsutmp1.ins.ddd.isvec
                             ? (lsutmp1.rsv1_data[i] + (lsutmp1.ins.ddd.mop == 0
                                                            ? i << 2
                                                            : i * lsutmp1.rsv2_data[i]))
                             : (lsutmp1.rsv1_data[0] + lsutmp1.rsv2_data[0]);
        }

        if (lsutmp1.ins.ddd.wvd || lsutmp1.ins.ddd.wxd)
        { // 要写回寄存器
            lsutmp2.ins = lsutmp1.ins;
            lsutmp2.warp_id = lsutmp1.warp_id;
            for (int i = 0; i < m_hw_warps[lsutmp1.warp_id]->CSR_reg[0x802]; i++)
            {
                lsutmp2.rdv1_data[i] = m_kernel->getBufferData(LSUaddr[i], addrOutofRangeException, lsutmp2.ins);
                if (addrOutofRangeException)
                    cout << "SM" << sm_id << " LSU detect addrOutofRange, ins=" << lsutmp1.ins << ",addr=" << LSUaddr[i] << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }

            lsufifo.push(lsutmp2);
        }
        else
        {
            for (int i = 0; i < m_hw_warps[lsutmp1.warp_id]->CSR_reg[0x802]; i++)
            {
                m_kernel->writeBufferData(lsutmp1.rsv3_data[i], LSUaddr[i], lsutmp1.ins);
            }
            cout << "SM" << sm_id << " warp " << lsutmp1.warp_id << " 0x" << std::hex << lsutmp1.ins.currentpc << " " << lsutmp1.ins << std::hex
                 << " data=" << std::setw(8) << std::setfill('0');
            for (int i = m_hw_warps[lsutmp1.warp_id]->CSR_reg[0x802] - 1; i >= 0; i--)
                cout << lsutmp1.rsv3_data[i] << " ";
            cout << "@ ";
            for (int i = m_hw_warps[lsutmp1.warp_id]->CSR_reg[0x802] - 1; i >= 0; i--)
                cout << LSUaddr[i] << " ";
            cout << std::setw(0) << std::setfill(' ') << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        }
        ev_lsufifo_pushed.notify();
    }
}

void BASE::LSU_CTRL()
{
    lsu_ready = true;
    lsu_ready_old = true;
    lsueqb_triggered = false;
    while (true)
    {
        wait(lsu_eqb.default_event() | lsu_unready | lsu_evb);
        if (lsu_eqb.default_event().triggered())
        {
            lsu_ready = true;
            lsu_ready_old = lsu_ready;
            lsueqb_triggered = true;
            wait(SC_ZERO_TIME);
            lsueqb_triggered = false;
            ev_lsuready_updated.notify();
        }
        else if (lsu_evb.triggered())
        {
            lsu_ready = true;
            lsu_ready_old = lsu_ready;
            ev_lsuready_updated.notify();
        }
        else if (lsu_unready.triggered())
        {
            lsu_ready = false;
            lsu_ready_old = lsu_ready;
            ev_lsuready_updated.notify();
        }
    }
}
