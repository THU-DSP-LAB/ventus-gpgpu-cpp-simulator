#include "BASE.h"

void BASE::LSU_IN()
{
    I_TYPE new_ins;
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
            new_data.rss1_data = tolsu_data1[0];
            new_data.rss2_data = tolsu_data2[0];
            new_data.rss3_data = tolsu_data3[0];
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
                cout << new_data.rss1_data << std::setw(0) << "+" << std::setw(8) << new_data.rss2_data;
                break;
            case SW_:
                cout << new_data.rss1_data << std::setw(0) << "+" << std::setw(8) << new_data.rss2_data;
                break;
            case VLE32_V_:
                cout << new_data.rss1_data;
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
        switch (lsutmp1.ins.op)
        {
        case LW_:
            lsutmp2.ins = lsutmp1.ins;
            lsutmp2.warp_id = lsutmp1.warp_id;

            // external_addr = (lsutmp1.rss1_data + lsutmp1.ins.s2) / 4 - 128 * num_thread; // 减去CSR_GDS
            external_addr = lsutmp1.rss1_data + lsutmp1.rss2_data;

            // cout << "LSU_CALC: read lw rss1_data=" << lsutmp1.rss1_data
            //      << ", external addr=" << external_addr
            //      << ", ins=" << lsutmp1.ins
            //      << ", external_mem[]=" << external_mem[external_addr]
            //      << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

            // lsutmp2.rds1_data = external_mem[external_addr];
            lsutmp2.rds1_data = getBufferData(*buffer_data, external_addr, mtd.num_buffer, mtd.buffer_base, mtd.buffer_allocsize, addrOutofRangeException, lsutmp2.ins);
            if (addrOutofRangeException)
                cout << "SM" << sm_id << " LSU detect addrOutofRange, ins=" << lsutmp1.ins << ",addr=" << external_addr << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            lsufifo.push(lsutmp2);
            // cout << "LSU_CALC: lw, pushed " << lsutmp2.rds1_data << " to s_regfile rd=" << lsutmp1.ins.d << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            break;
        case SW_:
            // external_addr = (lsutmp1.rss1_data + lsutmp1.ins.d) / 4 - 128 * num_thread;
            external_addr = lsutmp1.rss1_data + lsutmp1.rss2_data;
            // external_mem[external_addr] = lsutmp1.rds1_data;
            writeBufferData(lsutmp1.rss3_data, *buffer_data, external_addr, mtd.num_buffer, mtd.buffer_base, mtd.buffer_allocsize, lsutmp1.ins);
            break;
        case VLE32_V_:
            lsutmp2.ins = lsutmp1.ins;
            lsutmp2.warp_id = lsutmp1.warp_id;
            // cout << "LSU_CALC: calc vle32v, rss1=" << lsutmp1.rss1_data << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            for (int i = 0; i < num_thread; i++)
            {
                // lsutmp2.rdv1_data[i] = external_msem[lsutmp1.rss1_data / 4 + i - 128 * num_thread];
                lsutmp2.rdv1_data[i] = getBufferData(*buffer_data, lsutmp1.rss1_data + i * 4, mtd.num_buffer, mtd.buffer_base, mtd.buffer_allocsize, addrOutofRangeException, lsutmp2.ins);
                if (addrOutofRangeException)
                    cout << "SM" << sm_id << " LSU detect addrOutofRange, ins=" << lsutmp1.ins << ",addr=" << external_addr << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            lsufifo.push(lsutmp2);
            // cout << "LSU_CALC: pushed vle32v output at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            break;
        default:
            cout << "LSU_CALC warning: switch to unrecognized ins" << lsutmp1.ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            break;
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
