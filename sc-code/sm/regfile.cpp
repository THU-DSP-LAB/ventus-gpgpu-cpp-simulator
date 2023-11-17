#include "BASE.h"

void BASE::READ_REG()
{
    std::pair<int, int> temp_pair(-1, -1);
    int row, col;
    REGselectIdx.fill({-1, -1});
    warpaddr_t tmp;
    while (true)
    {
        wait(clk.posedge_event());
        // 先根据上一cycle regfile arbiter的结果读数据
        for (int i = 0; i < BANK_NUM; i++)
        {
            row = REGselectIdx[i].first;
            col = REGselectIdx[i].second;
            if (REGselectIdx[i] != temp_pair)
            {
                tmp = bank_undecode(i, read_bank_addr[i]);
                // cout << opcfifo[row].ins << " warp " << opcfifo[row].warp_id;
                // cout << " decode(bank" << i << ",addr" << read_bank_addr[i]
                //      << ") undecode --> warp_id=" << tmp.warp_id << ", addr=" << tmp.addr << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                // cout << "从regfile读出: REGselectIdx[" << i << "] to opc(" << REGselectIdx[i].first << "," << REGselectIdx[i].second << ") at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
                if (opc_banktype[row][col] == 0)
                {
                    read_data[i].fill(WARPS[tmp.warp_id]->s_regfile[tmp.addr]);
                }
                else
                {
                    read_data[i] = WARPS[tmp.warp_id]->v_regfile[tmp.addr];
                }
            }
        }
        ev_regfile_readdata.notify();
        wait(ev_opc_collect);
        for (auto &elem : opc_ready)
            elem.fill(0);
        for (int i = 0; i < BANK_NUM; i++)
        {
            REGselectIdx[i] = reg_arbiter(opc_srcaddr, opc_valid, opc_ready, i, REGcurrentIdx, read_bank_addr);
        }
    }
}

void BASE::WRITE_REG(int warp_id)
{
    float f1;
    float *pa1;
    while (true)
    {
        wait(clk.posedge_event());
        if (wb_warpid == warp_id)
        {
            // 后续regfile要一次只能写一个，否则报错
            if (write_s)
            {

                cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << wb_ins.read().currentpc << std::dec
                     << " " << wb_ins
                     << " x " << std::setfill('0') << std::setw(3) << rdv1_addr.read() << " "
                     << std::hex << std::setw(8)
                     << rdv1_data[0]
                     << std::dec << std::setfill(' ') << std::setw(0)
                     << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                if (rdv1_addr != 0)
                    WARPS[warp_id]->s_regfile[rdv1_addr.read()] = rdv1_data[0];
            }
            if (write_v)
            {

                cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << wb_ins.read().currentpc << std::dec
                     << " " << wb_ins
                     << " v " << std::setfill('0') << std::setw(3) << rdv1_addr.read() << " "
                     << std::hex << std::setw(8);
                for (int j = num_thread - 1; j > 0; j--)
                    cout << rdv1_data[j] << ",";
                cout << rdv1_data[0];
                cout << std::dec << std::setfill(' ') << std::setw(0)
                     << "; mask=" << wb_ins.read().mask << ", s1=" << wb_ins.read().s1 << ",s2=" << wb_ins.read().s2 << ",s3=" << wb_ins.read().s3
                     << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                for (int i = 0; i < num_thread; i++)
                    if (wb_ins.read().mask[i] == 1)
                        WARPS[warp_id]->v_regfile[rdv1_addr.read()][i] = rdv1_data[i];
            }
        }
    }
}
