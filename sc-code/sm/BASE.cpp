#include "BASE.h"

void BASE::debug_sti()
{
    while (true)
    {
        wait(clk.posedge_event());
        wait(SC_ZERO_TIME);
        dispatch_ready = !opc_full | doemit;
    }
}
void BASE::debug_display()
{
    while (true)
    {
        wait(ev_salufifo_pushed);
        cout << "ev_salufifo_pushed triggered at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
}
void BASE::debug_display1()
{
    while (true)
    {
        wait(ev_valufifo_pushed);
        cout << "ev_valufifo_pushed triggered at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
}
void BASE::debug_display2()
{
    while (true)
    {
        wait(ev_vfpufifo_pushed);
        cout << "ev_vfpufifo_pushed triggered at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
}
void BASE::debug_display3()
{
    while (true)
    {
        wait(ev_lsufifo_pushed);
        cout << "ev_lsufifo_pushed triggered at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
}

void BASE::PROGRAM_COUNTER(int warp_id)
{
    WARPS[warp_id]->pc = 0x80000000 - 4;
    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " PC: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(clk.posedge_event());
        // cout << "SM" << sm_id << " warp" << warp_id << " PC: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // cout << "PC warp" << warp_id << " start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // wait(WARPS[warp_id]->ev_ibuf_inout); // ibuf判断swallow后，fetch新指令
        // cout << "PC start, ibuf_swallow=" << ibuf_swallow << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

        if (WARPS[warp_id]->is_warp_activated)
        {
            if (rst_n == 0)
            {
                WARPS[warp_id]->pc = 0x80000000 - 4;
                WARPS[warp_id]->fetch_valid = false;
            }
            else if (WARPS[warp_id]->jump == 1)
            {
                WARPS[warp_id]->pc = WARPS[warp_id]->jump_addr;
                WARPS[warp_id]->fetch_valid = true;
                // cout << "pc jumps to addr " << jump_addr.read() << " at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
            }
            else if (WARPS[warp_id]->simtstk_jump == 1)
            {
                WARPS[warp_id]->pc = WARPS[warp_id]->simtstk_jumpaddr;
                WARPS[warp_id]->fetch_valid = true;
            }
            else if (WARPS[warp_id]->ibuf_empty |
                     (!WARPS[warp_id]->ibuf_full |
                      (WARPS[warp_id]->dispatch_warp_valid && (!opc_full | doemit))))
            {
                // cout << "pc will +1 at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                WARPS[warp_id]->pc = WARPS[warp_id]->pc.read() + 4;
                WARPS[warp_id]->fetch_valid = true;
            }
        }
        WARPS[warp_id]->ev_fetchpc.notify();
    }
}

void BASE::INSTRUCTION_REG(int warp_id)
{
    // initialize
    // ireg[0] = I_TYPE(ADD_, 0, 1, 2);
    // ireg[1] = I_TYPE(ADD_, 1, 3, 3);
    // ireg[2] = I_TYPE(ADD_, 0, 4, 5);
    // ireg[3] = I_TYPE(BEQ_, 0, 7, 2);
    // ireg[4] = I_TYPE(VADD_VX_, 0, 1, 4);
    // ireg[5] = I_TYPE(VFADD_VV_, 3, 4, 2);
    // ireg[6] = I_TYPE(BEQ_, 0, 7, 5);

    bool addrOutofRangeException;

    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(clk.posedge_event());
        // cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        if (WARPS[warp_id]->is_warp_activated && rst_n != 0)
        {
            if (WARPS[warp_id]->jump == 1 |
                WARPS[warp_id]->simtstk_jump == 1)
            {
                WARPS[warp_id]->fetch_valid12 = false;
                WARPS[warp_id]->ev_decode.notify();
            }
            else if (WARPS[warp_id]->ibuf_empty |
                     (!WARPS[warp_id]->ibuf_full |
                      (WARPS[warp_id]->dispatch_warp_valid && (!opc_full | doemit))))
            {
                WARPS[warp_id]->fetch_valid12 = WARPS[warp_id]->fetch_valid;
                if (inssrc == "ireg")
                    WARPS[warp_id]->fetch_ins =
                        (WARPS[warp_id]->pc.read() >= 0)
                            ? ireg[WARPS[warp_id]->pc.read() / 4]
                            : I_TYPE(INVALID_, 0, 0, 0);
                else if (inssrc == "imem")
                {
                    // if (sm_id == 0 && warp_id == 0)
                    //     cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG: fetch_ins pc=" << std::hex << WARPS[warp_id]->pc.read() << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    // WARPS[warp_id]->fetch_ins =
                    //     (WARPS[warp_id]->pc.read() >= 0x80000000)
                    //         ? I_TYPE(ins_mem[(WARPS[warp_id]->pc.read() - 0x80000000) / 4])
                    //         : I_TYPE(INVALID_, 0, 0, 0);
                    WARPS[warp_id]->fetch_ins = readInsBuffer(WARPS[warp_id]->pc.read(), addrOutofRangeException);
                    if (addrOutofRangeException)
                        cout << "SM" << sm_id << " warp" << warp_id << "INS_REG error: pc out of range at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    // if (sm_id == 0 && warp_id == 0)
                    //     cout << "SM" << sm_id << " warp" << warp_id << " ICACHE: read fetch_ins.bit=ins_mem[" << std::hex << WARPS[warp_id]->pc.read() / 4 << "]=" << WARPS[warp_id]->fetch_ins.origin32bit << std::dec
                    //          << ", will pass to decode_ins at the same cycle at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    // cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG: finish fetch_ins pc=" << WARPS[warp_id]->pc.read() << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }

                else
                    cout << "ICACHE error: unrecognized param inssrc=" << inssrc << "\n";
                WARPS[warp_id]->ev_decode.notify();
            }
        }
    }
}

void BASE::cycle_IBUF_ACTION(int warp_id, I_TYPE &dispatch_ins_, I_TYPE &_readdata3)
{
    WARPS[warp_id]->ibuf_swallow = false;
    if (rst_n.read() == 0)
        WARPS[warp_id]->ififo.clear();
    else
    {
        if (WARPS[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
        {
            // cout << "before dispatch, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
            dispatch_ins_ = WARPS[warp_id]->ififo.get();
            // if (sm_id == 0 && warp_id == 0)
            // {
            //     if (!WARPS[warp_id]->ififo.isempty())
            //         cout << "SM" << sm_id << " warp" << warp_id << " IBUF dispatch ins.bit=" << std::hex << dispatch_ins_.origin32bit << ", and ibuf.top become " << WARPS[warp_id]->ififo.front().origin32bit << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            //     else
            //         cout << "SM" << sm_id << " warp" << warp_id << " IBUF dispatch ins.bit=" << std::hex << dispatch_ins_.origin32bit << ", and ibuf become empty at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            // }
            // cout << "IBUF: after dispatch, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        }
        else
        {
            // cout << "IBUF: dispatch == false at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        }
        if (WARPS[warp_id]->fetch_valid2 && WARPS[warp_id]->jump == false && WARPS[warp_id]->simtstk_jump == false)
        {
            if (WARPS[warp_id]->ififo.isfull())
            {
                // cout << "SM" << sm_id << " warp" << warp_id << " IFIFO is full(not error) at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else
            {
                WARPS[warp_id]->ififo.push(WARPS[warp_id]->decode_ins.read());
                WARPS[warp_id]->ibuf_swallow = true;
                // if (sm_id == 0 && warp_id == 1)
                //     cout << "SM" << sm_id << " warp" << warp_id << " IFIFO push decode_ins.bit=" << std::hex << WARPS[warp_id]->decode_ins.read().origin32bit << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            // cout << "before put, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
            // cout << "after put, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        }
        else if (WARPS[warp_id]->jump || WARPS[warp_id]->simtstk_jump)
        {
            // cout << "ibuf detected jump at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
            WARPS[warp_id]->ififo.clear();
        }
    }
    WARPS[warp_id]->ibuf_empty = WARPS[warp_id]->ififo.isempty();
    WARPS[warp_id]->ibuf_full = WARPS[warp_id]->ififo.isfull();
    if (WARPS[warp_id]->ififo.isempty())
    {
        WARPS[warp_id]->ififo_elem_num = 0;
        WARPS[warp_id]->ibuftop_ins = I_TYPE(INVALID_, -1, 0, 0);
    }
    else
    {
        WARPS[warp_id]->ibuftop_ins.write(WARPS[warp_id]->ififo.front());
        WARPS[warp_id]->ififo_elem_num = WARPS[warp_id]->ififo.used();
        // cout << "ififo has " << ififo.used() << " elems in it at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
    }
    // if (sm_id == 0 && warp_id == 0)
    //     cout << "SM" << sm_id << " warp" << warp_id << " IBUF ififo_elem_num=" << WARPS[warp_id]->ififo_elem_num << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
}

void BASE::IBUF_ACTION(int warp_id)
{
    I_TYPE dispatch_ins_;
    I_TYPE _readdata3;
    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " IBUF_ACTION: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(clk.posedge_event());
        // cout << "SM" << sm_id << " warp" << warp_id << " IBUF_ACTION: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // cout << "IBUF_ACTION warp" << warp_id << ": start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

        if (WARPS[warp_id]->is_warp_activated)
        {
            cycle_IBUF_ACTION(warp_id, dispatch_ins_, _readdata3);
            WARPS[warp_id]->ev_ibuf_updated.notify();
        }
    }
}

void BASE::cycle_UPDATE_SCORE(int warp_id, I_TYPE &tmpins, std::set<SCORE_TYPE>::iterator &it, REG_TYPE &regtype_, bool &insertscore)
{
    if (wb_ena && wb_warpid == warp_id)
    { // 写回阶段，删除score
        tmpins = wb_ins;
        // cout << "scoreboard: wb_ins is " << tmpins << " at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        if (tmpins.ddd.wvd)
        {
            if (tmpins.ddd.wxd)
                cout << "Scoreboard warp" << warp_id << " error: wb_ins wvd=wxd=1 at the same time at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            regtype_ = v;
        }
        else if (tmpins.ddd.wxd)
            regtype_ = s;
        else
            cout << "Scoreboard warp" << warp_id << " error: wb_ins wvd=wxd=0 at the same time at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        it = WARPS[warp_id]->score.find(SCORE_TYPE(regtype_, tmpins.d));
        // cout << "scoreboard写回: 正在寻找 SCORE " << SCORE_TYPE(regtype_, tmpins.d) << " at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        if (it == WARPS[warp_id]->score.end())
        {
            cout << "warp" << warp_id << "_wb_ena error: scoreboard can't find rd in score set, wb_ins=" << wb_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        }
        else
        {
            WARPS[warp_id]->score.erase(it);
        }
        // cout << "warp" << warp_id << "_scoreboard: succesfully erased SCORE " << SCORE_TYPE(regtype_, tmpins.d) << ", wb_ins=" << wb_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
    // dispatch阶段，写入score
    tmpins = WARPS[warp_id]->ibuftop_ins; // this ibuftop_ins is the old data
    if (WARPS[warp_id]->branch_sig || WARPS[warp_id]->vbran_sig)
    {
        if (WARPS[warp_id]->wait_bran == 0)
            cout << "warp" << warp_id << "_scoreboard error: detect (v)branch_sig=1(from salu) while wait_bran=0 at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        else if (WARPS[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
            cout << "warp" << warp_id << "_scoreboard error: detect (v)branch_sig=1(from salu) while dispatch=1 at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        WARPS[warp_id]->wait_bran = 0;
    }
    else if ((tmpins.ddd.branch != 0) &&
             WARPS[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
    {
        // cout << "ibuf let wait_bran=1 at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        WARPS[warp_id]->wait_bran = 1;
    }
    if (WARPS[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
    { // 加入 score
        insertscore = true;
        if (tmpins.ddd.wvd)
        {
            if (tmpins.ddd.wxd)
                cout << "Scoreboard warp" << warp_id << " error: dispatch_ins wvd=wxd=1 at the same time at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            // if (sm_id == 0 && warp_id == 0 && tmpins.d == 0)
            //     cout << "SM" << sm_id << " warp" << warp_id << " UPDATE_SCORE insert ins.bit=" << std::hex << tmpins.origin32bit << std::dec << " vector regfile 0 to scoreboard at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            regtype_ = v;
        }
        else if (tmpins.ddd.wxd)
            regtype_ = s;
        else
            insertscore = false;
        if (insertscore)
            WARPS[warp_id]->score.insert(SCORE_TYPE(regtype_, tmpins.d));
        // if (sm_id == 0)
        //     cout << "SM0 warp" << warp_id << "_scoreboard: insert " << SCORE_TYPE(regtype_, tmpins.d)
        //          << " because of dispatch " << tmpins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
}

void BASE::UPDATE_SCORE(int warp_id)
{
    I_TYPE tmpins;
    std::set<SCORE_TYPE>::iterator it;
    REG_TYPE regtype_;
    bool insertscore = false;
    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " UPDATE_SCORE: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(clk.posedge_event());
        // cout << "SM" << sm_id << " warp" << warp_id << " UPDATE_SCORE: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        if (WARPS[warp_id]->is_warp_activated)
        {

            cycle_UPDATE_SCORE(warp_id, tmpins, it, regtype_, insertscore);

            WARPS[warp_id]->ev_judge_dispatch.notify();
        }
    }
}

void BASE::cycle_JUDGE_DISPATCH(int warp_id, I_TYPE &_readibuf)
{
    if (WARPS[warp_id]->wait_bran | WARPS[warp_id]->jump)
    {
        WARPS[warp_id]->can_dispatch = false;
    }
    else if (!WARPS[warp_id]->ififo.isempty())
    {
        _readibuf = WARPS[warp_id]->ififo.front();
        WARPS[warp_id]->can_dispatch = true;

        if (_readibuf.op == INVALID_)
            WARPS[warp_id]->can_dispatch = false;

        if (_readibuf.ddd.wxd && WARPS[warp_id]->score.find(SCORE_TYPE(s, _readibuf.d)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.wvd && WARPS[warp_id]->score.find(SCORE_TYPE(v, _readibuf.d)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu1 == DecodeParams::A1_RS1 && WARPS[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s1)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu1 == DecodeParams::A1_VRS1 && WARPS[warp_id]->score.find(SCORE_TYPE(v, _readibuf.s1)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu2 == DecodeParams::A2_RS2 && WARPS[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s2)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu2 == DecodeParams::A2_VRS2 && WARPS[warp_id]->score.find(SCORE_TYPE(v, _readibuf.s2)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::A3_FRS3 && WARPS[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s3)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::A3_VRS3 && WARPS[warp_id]->score.find(SCORE_TYPE(v, _readibuf.s3)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::A3_PC && _readibuf.ddd.branch == DecodeParams::B_R && WARPS[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s1)) != WARPS[warp_id]->score.end())
            WARPS[warp_id]->can_dispatch = false;

        // if (sm_id == 0 && warp_id == 0)
        //     if (WARPS[warp_id]->can_dispatch == false)
        //         cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH=false with ins.bit=" << std::hex << _readibuf.origin32bit << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

        // if (sm_id == 0 && warp_id == 0 && _readibuf.origin32bit == uint32_t(0x96013057) && WARPS[warp_id]->can_dispatch == false)
        // if (sm_id == 0 && warp_id == 0 && WARPS[warp_id]->can_dispatch == false)
        //     cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH meet ins.bit=" << std::hex << _readibuf.origin32bit << std::dec << ", can't dispatch, ins.d=" << _readibuf.d << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
}

void BASE::JUDGE_DISPATCH(int warp_id)
{
    I_TYPE _readibuf;
    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(WARPS[warp_id]->ev_judge_dispatch & WARPS[warp_id]->ev_ibuf_updated);
        // cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // cout << "scoreboard: ibuftop_ins=" << ibuftop_ins << " at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";

        cycle_JUDGE_DISPATCH(warp_id, _readibuf);

        WARPS[warp_id]->ev_issue.notify();
    }
}

void BASE::BEFORE_DISPATCH(int warp_id)
{
    I_TYPE dispatch_ins_;
    I_TYPE _readdata3;
    I_TYPE tmpins;
    std::set<SCORE_TYPE>::iterator it;
    REG_TYPE regtype_;
    bool insertscore = false;
    I_TYPE _readibuf;

    while (true)
    {
        wait(ev_warp_assigned);
        if (WARPS[warp_id]->is_warp_activated)
        {
            // if (sm_id == 0 && warp_id == 0)
            // cout << "SM" << sm_id << " warp" << warp_id << " before action, fetch_valid2=" << WARPS[warp_id]->fetch_valid2 << ", decode_ins=" << std::hex << WARPS[warp_id]->decode_ins.read().origin32bit
            //      << std::dec << ", jump=" << WARPS[warp_id]->jump << ", ififo.isfull=" << WARPS[warp_id]->ififo.isfull() << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

            cycle_IBUF_ACTION(warp_id, dispatch_ins_, _readdata3);
            cycle_UPDATE_SCORE(warp_id, tmpins, it, regtype_, insertscore);
            cycle_JUDGE_DISPATCH(warp_id, _readibuf);
            WARPS[warp_id]->ev_issue.notify();
        }
    }
}
void BASE::WARP_SCHEDULER()
{
    bool find_dispatchwarp = 0;
    last_dispatch_warpid = 0;
    I_TYPE _newissueins;
    while (true)
    {
        wait(clk.posedge_event());

        // handle warp end

        ev_warp_assigned.notify();

        wait(ev_issue_list);

        // cycle_ISSUE_ACTION();

        if (!opc_full | doemit) // 这是dispatch_ready (ready-valid机制)
        {
            find_dispatchwarp = 0;
            for (int i = last_dispatch_warpid; i < last_dispatch_warpid + num_warp_activated; i++)
            {
                if (!find_dispatchwarp && WARPS[i % num_warp_activated]->can_dispatch)
                {
                    // cout << "ISSUE: opc_full=" << opc_full << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    WARPS[i % num_warp_activated]->dispatch_warp_valid = true;
                    dispatch_valid = true;
                    _newissueins = WARPS[i % num_warp_activated]->ififo.front();
                    _newissueins.mask = WARPS[i % num_warp_activated]->current_mask;
                    // cout << "let issue_ins mask=" << _newissueins.mask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    issue_ins = _newissueins;
                    issueins_warpid = i % num_warp_activated;
                    find_dispatchwarp = true;
                    last_dispatch_warpid = i % num_warp_activated + 1;
                }
                else
                {
                    WARPS[i % num_warp_activated]->dispatch_warp_valid = false;
                    // cout << "ISSUE: let warp" << i % num_warp_activated << " dispatch_warp_valid=false at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }
            }
            if (!find_dispatchwarp)
                dispatch_valid = false;
        }
    }
}
void BASE::cycle_ISSUE_ACTION()
{
    bool find_dispatchwarp = 0;
    last_dispatch_warpid = 0;
    I_TYPE _newissueins;

    if (!opc_full | doemit) // 这是dispatch_ready (ready-valid机制)
    {
        find_dispatchwarp = 0;
        for (int i = last_dispatch_warpid; i < last_dispatch_warpid + num_warp_activated; i++)
        {
            if (!find_dispatchwarp && WARPS[i % num_warp_activated]->can_dispatch)
            {
                // cout << "ISSUE: opc_full=" << opc_full << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                WARPS[i % num_warp_activated]->dispatch_warp_valid = true;
                dispatch_valid = true;
                _newissueins = WARPS[i % num_warp_activated]->ififo.front();
                _newissueins.mask = WARPS[i % num_warp_activated]->current_mask;
                // cout << "let issue_ins mask=" << _newissueins.mask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                issue_ins = _newissueins;
                issueins_warpid = i % num_warp_activated;
                find_dispatchwarp = true;
                last_dispatch_warpid = i % num_warp_activated + 1;
            }
            else
            {
                WARPS[i % num_warp_activated]->dispatch_warp_valid = false;
                // cout << "ISSUE: let warp" << i % num_warp_activated << " dispatch_warp_valid=false at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
        }
        if (!find_dispatchwarp)
            dispatch_valid = false;
    }
}

void BASE::ISSUE_ACTION()
{
    bool find_dispatchwarp = 0;
    last_dispatch_warpid = 0;
    I_TYPE _newissueins;
    while (true)
    {
        wait(ev_issue_list);
        // cout << "SM" << sm_id << " ISSUE_ACTION: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // cout << "SM" << sm_id << " ISSUE_ACTION: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // cout << "ISSUE start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        cycle_ISSUE_ACTION();
    }
}

std::pair<int, int> BASE::reg_arbiter(
    const std::array<std::array<bank_t, 3>, OPCFIFO_SIZE> &addr_arr, // opc_srcaddr
    const std::array<std::array<bool, 3>, OPCFIFO_SIZE> &valid_arr,  // opc_valid
    std::array<std::array<bool, 3>, OPCFIFO_SIZE> &ready_arr,        // opc_ready
    int bank_id,
    std::array<int, BANK_NUM> &REGcurrentIdx,
    std::array<int, BANK_NUM> &read_bank_addr)
{
    const int rows = OPCFIFO_SIZE; // = addr_arr.size()
    const int cols = 3;            // = addr_arr[0].size(), 每个opc_fifo_t四个待取元素
    const int size = rows * cols;
    std::pair<int, int> result(-1, -1); // 默认值表示没有找到有效数据
    int index, i, j;
    for (int idx = REGcurrentIdx[bank_id] % size;
         idx < size + REGcurrentIdx[bank_id] % size; idx++)
    {
        index = idx % size;
        i = index / cols;
        j = index % cols;
        if (valid_arr[i][j] == true)
        {
            if (addr_arr[i][j].bank_id == bank_id)
            {
                read_bank_addr[bank_id] = addr_arr[i][j].addr; // 下周期读数据
                // cout << "let read_bank_addr(bank" << bank_id << ")="
                //      << addr_arr[i][j].addr << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                result.first = i;
                result.second = j;
                ready_arr[i][j] = true;
                REGcurrentIdx[bank_id] = index + 1;
                break;
            }
        }
    }
    return result;
}

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
                // cout << opcfifo[row].ins << "warp" << opcfifo[row].warp_id;
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

void BASE::WRITE_BACK()
{
    // FloatAndInt newFI;

    while (true)
    {
        wait(ev_salufifo_pushed & ev_valufifo_pushed & ev_vfpufifo_pushed &
             ev_lsufifo_pushed & ev_csrfifo_pushed & ev_mulfifo_pushed & ev_sfufifo_pushed);
        // if (sm_id == 0)
        //     cout << "SM" << sm_id << " WRITEBACK: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        if (execpop_salu)
        {
            salufifo.pop();
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB pop salufifo at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
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
        lsufifo_empty = lsufifo.isempty();
        if (!lsufifo_empty)
            lsutop_dat = lsufifo.front();
        lsufifo_elem_num = lsufifo.used();
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

        execpop_valu = false;
        execpop_salu = false;
        execpop_vfpu = false;
        execpop_lsu = false;
        execpop_csr = false;
        execpop_mul = false;
        execpop_sfu = false;

        if (salufifo_empty == false)
        {
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB judge popsalu, write_s=true, salutop.ins=" << salutop_dat.ins << ",pc=" << std::hex << salutop_dat.ins.currentpc << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            write_s = true;
            write_v = false;
            wb_ena = true;
            execpop_salu = true;
            wb_ins = salutop_dat.ins;
            rds1_addr = salutop_dat.ins.d;
            rds1_data = salutop_dat.data;
            wb_warpid = salutop_dat.warp_id;
        }
        else if (valufifo_empty == false)
        {
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB judge popvalu, write_v=true at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            write_s = false;
            write_v = true;
            wb_ena = true;
            execpop_valu = true;
            wb_ins = valutop_dat.ins;
            rdv1_addr = valutop_dat.ins.d;
            for (int i = 0; i < num_thread; i++)
            {
                rdv1_data[i] = valutop_dat.rdv1_data[i];
            }
            wb_warpid = valutop_dat.warp_id;
        }
        else if (vfpufifo_empty == false)
        {
            wb_ena = true;
            execpop_vfpu = true;
            wb_ins = vfputop_dat.ins;
            if (vfputop_dat.ins.ddd.wxd) // FEQ_S_等指令
            {
                // if (sm_id == 0)
                //     cout << "SM" << sm_id << " WB judge popvfpu, write_s=true at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                write_s = true;
                write_v = false;
                rds1_addr = vfputop_dat.ins.d;
                rds1_data = vfputop_dat.rds1_data;
            }
            else
            {
                // if (sm_id == 0)
                //     cout << "SM" << sm_id << " WB judge popvfpu, write_v=true at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                write_s = false;
                write_v = true;
                rdv1_addr = vfputop_dat.ins.d;
                // cout << "WB: let wb_ins=" << vfputop_dat.ins << "warp" << vfputop_dat.warp_id << ", rdf1_data={";
                for (int i = 0; i < num_thread; i++)
                {
                    // newFI.f = vfputop_dat.rdf1_data[i];
                    // cout << std::hex << newFI.i << std::dec << ",";
                    rdv1_data[i].write(vfputop_dat.rdf1_data[i]);
                }
            }
            wb_warpid = vfputop_dat.warp_id;
            // cout << "} at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        }
        else if (lsufifo_empty == false)
        {
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB judge poplsu, at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            execpop_lsu = true;
            switch (lsutop_dat.ins.op)
            {
            case LW_:
                write_s = true;
                write_v = false;
                wb_ena = true;
                wb_ins = lsutop_dat.ins;
                rds1_addr = lsutop_dat.ins.d;
                rds1_data = lsutop_dat.rds1_data;
                // cout << "WB: arbit LW_ writeback, ins=" << lsutop_dat.ins << "warp" << lsutop_dat.warp_id << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                break;
            case VLE32_V_:
                write_s = false;
                write_v = true;
                wb_ena = true;
                wb_ins = lsutop_dat.ins;
                rdv1_addr = lsutop_dat.ins.d;
                for (int i = 0; i < num_thread; i++)
                {
                    rdv1_data[i] = lsutop_dat.rdv1_data[i];
                }
                break;
            default:
                cout << "wb error: lsu unrecognized ins\n";
                break;
            }
            wb_warpid = lsutop_dat.warp_id;
        }
        else if (csrfifo_empty == false)
        {
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB judge popcsr, write_s=true, csrtop.ins=" << csrtop_dat.ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            write_s = true;
            write_v = false;
            wb_ena = true;
            execpop_csr = true;
            wb_ins = csrtop_dat.ins;
            rds1_addr = csrtop_dat.ins.d;
            rds1_data = csrtop_dat.data;
            wb_warpid = csrtop_dat.warp_id;
        }
        else if (mulfifo_empty == false)
        {
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB judge popmul, at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            wb_ena = true;
            execpop_mul = true;
            wb_ins = multop_dat.ins;
            wb_warpid = multop_dat.warp_id;
            if (multop_dat.ins.ddd.wxd)
            {
                write_s = true;
                write_v = false;
                rds1_addr = multop_dat.ins.d;
                rds1_data = multop_dat.rdv1_data[0];
            }
            else if (multop_dat.ins.ddd.wvd)
            {
                write_s = false;
                write_v = true;
                rdv1_addr = multop_dat.ins.d;
                for (int i = 0; i < num_thread; i++)
                {
                    rdv1_data[i] = multop_dat.rdv1_data[i];
                }
            }
        }
        else if (sfufifo_empty == false)
        {
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB judge popsfu, at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            wb_ena = true;
            execpop_sfu = true;
            wb_ins = sfutop_dat.ins;
            wb_warpid = sfutop_dat.warp_id;
            if (sfutop_dat.ins.ddd.wxd)
            {
                write_s = true;
                write_v = false;
                rds1_addr = sfutop_dat.ins.d;
                rds1_data = sfutop_dat.rdv1_data[0];
            }
            else if (sfutop_dat.ins.ddd.wvd)
            {
                write_s = false;
                write_v = true;
                rdv1_addr = sfutop_dat.ins.d;
                for (int i = 0; i < num_thread; i++)
                    rdv1_data[i] = sfutop_dat.rdv1_data[i];
            }
        }
        else
        {
            // if (sm_id == 0)
            //     cout << "SM" << sm_id << " WB judge not writeback, at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            write_s = false;
            write_v = false;
            wb_ena = false;
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
                if (warp_id == 0)
                    cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << wb_ins.read().currentpc << std::dec
                         << " " << wb_ins
                         << " x " << std::setfill('0') << std::setw(3) << rds1_addr.read() << " "
                         << std::hex << std::setw(8)
                         << rds1_data
                         << std::dec << std::setfill(' ') << std::setw(0)
                         << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                WARPS[warp_id]->s_regfile[rds1_addr.read()] = rds1_data;
            }
            if (write_v)
            {
                if (warp_id == 0)
                {
                    cout << "SM" << sm_id << " warp " << warp_id << " 0x" << std::hex << wb_ins.read().currentpc << std::dec
                         << " " << wb_ins
                         << " v " << std::setfill('0') << std::setw(3) << rdv1_addr.read() << " "
                         << std::hex << std::setw(8);
                    for (int j = 0; j < num_thread - 1; j++)
                        cout << rdv1_data[j] << ",";
                    cout << rdv1_data[num_thread - 1];
                    cout << std::dec << std::setfill(' ') << std::setw(0)
                         << "; mask=" << wb_ins.read().mask << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }

                for (int i = 0; i < num_thread; i++)
                    if (wb_ins.read().mask[i] == 1)
                        WARPS[warp_id]->v_regfile[rdv1_addr.read()][i] = rdv1_data[i];
            }
        }
    }
}

void BASE::activate_warp(int warp_id)
{
    if (WARPS[warp_id] == nullptr)
    {
        WARP_BONE *new_warp_bone_ = new WARP_BONE(warp_id);
        WARPS[warp_id] = new_warp_bone_;

        // warp_threads_group[0][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::PROGRAM_COUNTER, this, warp_id), ("warp" + std::to_string(warp_id) + "_PROGRAM_COUNTER").c_str()));
        // warp_threads_group[1][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::INSTRUCTION_REG, this, warp_id), ("warp" + std::to_string(warp_id) + "_INSTRUCTION_REG").c_str()));
        // warp_threads_group[2][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::DECODE, this, warp_id), ("warp" + std::to_string(warp_id) + "_DECODE").c_str()));
        // warp_threads_group[3][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::IBUF_ACTION, this, warp_id), ("warp" + std::to_string(warp_id) + "_IBUF_ACTION").c_str()));
        // warp_threads_group[4][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::JUDGE_DISPATCH, this, warp_id), ("warp" + std::to_string(warp_id) + "_JUDGE_DISPATCH").c_str()));
        // warp_threads_group[5][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::UPDATE_SCORE, this, warp_id), ("warp" + std::to_string(warp_id) + "_UPDATE_SCORE").c_str()));
        // warp_threads_group[6][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::INIT_REG, this, warp_id), ("warp" + std::to_string(warp_id) + "_INIT_REG").c_str()));
        // warp_threads_group[7][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::SIMT_STACK, this, warp_id), ("warp" + std::to_string(warp_id) + "_SIMT_STACK").c_str()));
        // warp_threads_group[8][warp_id] = new sc_process_handle(sc_spawn(sc_bind(&BASE::WRITE_REG, this, warp_id), ("warp" + std::to_string(warp_id) + "_WRITE_REG").c_str()));
    }
    else
    {
        cout << "Activate warp error: warp" << warp_id << " already exists!\n";
    }
}

void BASE::remove_warp(int warp_id)
{
    if (WARPS[warp_id] != nullptr)
    {
        delete WARPS[warp_id];
        WARPS[warp_id] = nullptr;
        for (auto &threads : warp_threads_group)
        {
            sc_process_handle *thread_to_delete = threads[warp_id];
            if (thread_to_delete != nullptr)
            {
                thread_to_delete->kill();
                delete thread_to_delete;
                threads[warp_id] = nullptr;
            }
        }
    }
    else
    {
        cout << "Remove warp error: warp" << warp_id << " doesn't exist!\n";
    }
}
