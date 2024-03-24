#include "BASE.h"

void BASE::TC_IN()
{
    tc_in_t new_data;
    int a_delay, b_delay;
    while (true)
    {
        wait();
        if (emito_tc)
        {
            if (tc_ready_old == false)
            {
                std::cout << "tc error: not ready at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            tc_unready.notify();
            new_data.ins = emit_ins;
            new_data.warp_id = emitins_warpid;
            // std::cout << "SM" << sm_id << " TC_IN receive ins=" << emit_ins.read() << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

            for (int i = 0; i < m_hw_warps[new_data.warp_id]->CSR_reg[0x802]; i++)
            {
                new_data.tcSdata1[i] = totc_data1[i];
                new_data.tcSdata2[i] = totc_data2[i];
                new_data.tcSdata3[i] = totc_data3[i];
            }

            tc_dq.push(new_data);
            a_delay = 20;
            b_delay = 6;

            if (a_delay == 0)
                tc_eva.notify();
            else if (tceqa_triggered)
                tc_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
            else
            {
                tc_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                ev_tcfifo_pushed.notify();
            }
            if (b_delay == 0)
                tc_evb.notify();
            else
            {
                tc_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                ev_tcready_updated.notify();
            }
        }
        else
        {
            if (!tceqa_triggered)
                ev_tcfifo_pushed.notify();
            if (!tceqb_triggered)
                ev_tcready_updated.notify();
        }
    }
}

void BASE::TC_CALC()
{
    tcfifo_elem_num = 0;
    tcfifo_empty = true;
    tceqa_triggered = false;
    tc_in_t tctmp1;
    tc_out_t tctmp2;
    bool succeed;
    float source_f1, source_f2, source_f3;
    while (true)
    {
        wait(tc_eva | tc_eqa.default_event());
        if (tc_eqa.default_event().triggered())
        {
            tceqa_triggered = true;
            wait(SC_ZERO_TIME);
            tceqa_triggered = false;
        }
        tctmp1 = tc_dq.front();
        tc_dq.pop();
        if (tctmp1.ins.ddd.wxd | tctmp1.ins.ddd.wvd)
        {
            tctmp2.ins = tctmp1.ins;
            tctmp2.warp_id = tctmp1.warp_id;
            switch (tctmp1.ins.ddd.alu_fn)
            {
            case DecodeParams::alu_fn_t::FN_TTF:
                // std::cout << "Matrix A (Hardware format):" << std::endl;
                // for (int i = 0; i < 2; ++i)
                // {
                //     for (int j = 0; j < 4; ++j)
                //         std::cout << std::hex << std::setw(8) << std::setfill('0') << tctmp1.tcSdata1[i * 4 + j] << " ";
                //     std::cout << std::endl;
                // }
                // std::cout << "Matrix B (Hardware format):" << std::endl;
                // for (int i = 0; i < 4; ++i)
                // {
                //     for (int j = 0; j < 2; ++j)
                //         std::cout << std::hex << std::setw(8) << std::setfill('0') << tctmp1.tcSdata2[i * 2 + j] << " ";
                //     std::cout << std::endl;
                // }
                // std::cout << "Matrix C (Hardware format):" << std::endl;
                // for (int i = 0; i < 2; ++i)
                // {
                //     for (int j = 0; j < 2; ++j)
                //         std::cout << std::hex << std::setw(8) << std::setfill('0') << tctmp1.tcSdata3[i * 2 + j] << " ";
                //     std::cout << std::endl;
                // }

                assert(hw_num_thread >= 8);
                // static_assert(hw_num_thread >= 8, "hw_num_thread must be at least 8.");
                for (int i = 0; i < 2; ++i)
                {
                    for (int j = 0; j < 2; ++j)
                    {
                        float result = 0.0f;
                        for (int k = 0; k < 4; ++k)
                        {
                            float a = std::bit_cast<float>(tctmp1.tcSdata1[i * 4 + k]);
                            float b = std::bit_cast<float>(tctmp1.tcSdata2[j * 4 + k]);
                            result += a * b;
                        }
                        result += std::bit_cast<float>(tctmp1.tcSdata3[i * 2 + j]);
                        tctmp2.rdv1_data[i * 2 + j] = std::bit_cast<int>(result);
                    }
                }

                // std::cout << "Matrix D (Result in Hardware format):" << std::endl;
                // for (int i = 0; i < 2; ++i)
                // {
                //     for (int j = 0; j < 2; ++j)
                //         std::cout << std::hex << std::setw(8) << std::setfill('0') << tctmp2.rdv1_data[i * 2 + j] << " ";
                //     std::cout << std::endl;
                // }
                break;
            default:
                std::cout << "TC_CALC warning: switch to unrecognized ins" << tctmp1.ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                break;
            }
            tcfifo.push(tctmp2);
        }
        else
        {
        }

        ev_tcfifo_pushed.notify();
    }
}

void BASE::TC_CTRL()
{
    tc_ready = true;
    tc_ready_old = true;
    tceqb_triggered = false;
    while (true)
    {
        wait(tc_eqb.default_event() | tc_unready | tc_evb);
        if (tc_eqb.default_event().triggered())
        {
            tc_ready = true;
            tc_ready_old = tc_ready;
            tceqb_triggered = true;
            wait(SC_ZERO_TIME);
            tceqb_triggered = false;
            ev_tcready_updated.notify();
        }
        else if (tc_evb.triggered())
        {
            tc_ready = true;
            tc_ready_old = tc_ready;
            ev_tcready_updated.notify();
        }
        else if (tc_unready.triggered())
        {
            tc_ready = false;
            tc_ready_old = tc_ready;
            ev_tcready_updated.notify();
        }
    }
}
