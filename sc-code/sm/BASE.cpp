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
    // m_hw_warps[warp_id]->pc = 0x80000000 - 4;
    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " PC: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(clk.posedge_event());
        // cout << "SM" << sm_id << " warp" << warp_id << " PC: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // cout << "PC warp" << warp_id << " start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // wait(m_hw_warps[warp_id]->ev_ibuf_inout); // ibuf判断swallow后，fetch新指令
        // cout << "PC start, ibuf_swallow=" << ibuf_swallow << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        if (m_hw_warps[warp_id]->is_warp_activated.read())
        {
            // cout << "warp " << warp_id << " sup at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            if (rst_n == 0)
            {
                m_hw_warps[warp_id]->pc = 0;
                m_hw_warps[warp_id]->fetch_valid = false;
            }
            else if (m_hw_warps[warp_id]->jump == 1)
            {
                m_hw_warps[warp_id]->pc = m_hw_warps[warp_id]->jump_addr;
                m_hw_warps[warp_id]->fetch_valid = true;
                cout << "SM" << sm_id << " warp " << warp_id << " pc jumps to 0x" << std::hex << m_hw_warps[warp_id]->jump_addr << std::dec
                     << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else if (m_hw_warps[warp_id]->simtstk_jump == 1)
            {
                m_hw_warps[warp_id]->pc = m_hw_warps[warp_id]->simtstk_jumpaddr;
                m_hw_warps[warp_id]->fetch_valid = true;
            }
            else if (m_hw_warps[warp_id]->ibuf_empty |
                     (!m_hw_warps[warp_id]->ibuf_full |
                      (m_hw_warps[warp_id]->dispatch_warp_valid && (!opc_full | doemit))))
            {
                // cout << "pc will +1 at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                m_hw_warps[warp_id]->pc = m_hw_warps[warp_id]->pc.read() + 4;
                m_hw_warps[warp_id]->fetch_valid = true;
            }
        }
        m_hw_warps[warp_id]->ev_fetchpc.notify();
        if (m_hw_warps[warp_id]->flush_pipeline)
        {
            m_hw_warps[warp_id]->fetch_valid = false;
            m_hw_warps[warp_id]->fetch_valid2 = false;
        }
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
        if (m_hw_warps[warp_id]->is_warp_activated && rst_n != 0)
        {
            if (m_hw_warps[warp_id]->jump == 1 |
                m_hw_warps[warp_id]->simtstk_jump == 1)
            {
                m_hw_warps[warp_id]->fetch_valid12 = false;
                m_hw_warps[warp_id]->ev_decode.notify();
            }
            else if (m_hw_warps[warp_id]->ibuf_empty |
                     (!m_hw_warps[warp_id]->ibuf_full |
                      (m_hw_warps[warp_id]->dispatch_warp_valid && (!opc_full | doemit))))
            {
                m_hw_warps[warp_id]->fetch_valid12 = m_hw_warps[warp_id]->fetch_valid;
                if (inssrc == "ireg")
                    m_hw_warps[warp_id]->fetch_ins =
                        (m_hw_warps[warp_id]->pc.read() >= 0)
                            ? ireg[m_hw_warps[warp_id]->pc.read() / 4]
                            : I_TYPE(INVALID_, 0, 0, 0);
                else if (inssrc == "imem")
                {
                    // if (sm_id == 0 && warp_id == 0)
                    //     cout << "SM" << sm_id << " warp" << warp_id << " INSTRUCTION_REG: fetch_ins pc=" << std::hex << m_hw_warps[warp_id]->pc.read() << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    m_hw_warps[warp_id]->fetch_ins = m_kernel->readInsBuffer(m_hw_warps[warp_id]->pc.read(), addrOutofRangeException);
                    if (addrOutofRangeException)
                        cout << "SM" << sm_id << " warp" << warp_id << "INS_REG error: pc out of range at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

                    // if (sm_id == 0 && warp_id == 0)
                    //     cout << "SM" << sm_id << " warp" << warp_id << " ICACHE: read fetch_ins.bit=ins_mem[" << std::hex << m_hw_warps[warp_id]->pc.read() << "]=" << m_hw_warps[warp_id]->fetch_ins.origin32bit << std::dec
                    //          << ", will pass to decode_ins at the same cycle at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }

                else
                    cout << "ICACHE error: unrecognized param inssrc=" << inssrc << "\n";
                m_hw_warps[warp_id]->ev_decode.notify();
            }
        }
    }
}

void BASE::cycle_IBUF_ACTION(int warp_id, I_TYPE &dispatch_ins_, I_TYPE &_readdata3)
{
    m_hw_warps[warp_id]->ibuf_swallow = false;
    if (rst_n.read() == 0)
        m_hw_warps[warp_id]->ififo.clear();
    else
    {
        if (m_hw_warps[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
        {
            // cout << "before dispatch, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
            dispatch_ins_ = m_hw_warps[warp_id]->ififo.get();
            // if (sm_id == 0 && warp_id == 0)
            // {
            //     if (!m_hw_warps[warp_id]->ififo.isempty())
            //         cout << "SM" << sm_id << " warp" << warp_id << " IBUF dispatch ins.bit=" << std::hex << dispatch_ins_.origin32bit << ", and ibuf.top become " << m_hw_warps[warp_id]->ififo.front().origin32bit << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            //     else
            //         cout << "SM" << sm_id << " warp" << warp_id << " IBUF dispatch ins.bit=" << std::hex << dispatch_ins_.origin32bit << ", and ibuf become empty at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            // }
            // cout << "IBUF: after dispatch, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        }
        else
        {
            // cout << "IBUF: dispatch == false at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        }

        if (m_hw_warps[warp_id]->fetch_valid2 && m_hw_warps[warp_id]->jump == false && m_hw_warps[warp_id]->simtstk_jump == false)
        {
            if (m_hw_warps[warp_id]->ififo.isfull())
            {
                // cout << "SM" << sm_id << " warp" << warp_id << " IFIFO is full(not error) at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else
            {
                m_hw_warps[warp_id]->ififo.push(m_hw_warps[warp_id]->decode_ins.read());
                m_hw_warps[warp_id]->ibuf_swallow = true;
                // if (m_hw_warps[warp_id]->decode_ins.read().op == OP_TYPE::ENDPRG_)
                //     cout << "SM" << sm_id << " warp " << warp_id << " IFIFO push decode_ins=" << m_hw_warps[warp_id]->decode_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            // cout << "before put, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
            // cout << "after put, ififo has " << ififo.used() << " elems at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        }
        else if (m_hw_warps[warp_id]->jump || m_hw_warps[warp_id]->simtstk_jump)
        {
            // cout << "ibuf detected jump at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
            m_hw_warps[warp_id]->ififo.clear();
        }
    }
    m_hw_warps[warp_id]->ibuf_empty = m_hw_warps[warp_id]->ififo.isempty();
    m_hw_warps[warp_id]->ibuf_full = m_hw_warps[warp_id]->ififo.isfull();
    if (m_hw_warps[warp_id]->ififo.isempty())
    {
        m_hw_warps[warp_id]->ififo_elem_num = 0;
        m_hw_warps[warp_id]->ibuftop_ins = I_TYPE(INVALID_, -1, 0, 0);
    }
    else
    {
        m_hw_warps[warp_id]->ibuftop_ins.write(m_hw_warps[warp_id]->ififo.front());
        m_hw_warps[warp_id]->ififo_elem_num = m_hw_warps[warp_id]->ififo.used();
        // cout << "ififo has " << ififo.used() << " elems in it at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
    }
    // if (sm_id == 0 && warp_id == 0)
    //     cout << "SM" << sm_id << " warp" << warp_id << " IBUF ififo_elem_num=" << m_hw_warps[warp_id]->ififo_elem_num << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
}

void BASE::cycle_UPDATE_SCORE(int warp_id, I_TYPE &tmpins, std::set<SCORE_TYPE>::iterator &it, REG_TYPE &regtype_, bool &insertscore)
{
    if (wb_ena && wb_warpid == warp_id)
    {
        //
        // 写回阶段，删除score
        //
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
        it = m_hw_warps[warp_id]->score.find(SCORE_TYPE(regtype_, tmpins.d));
        // cout << "scoreboard写回: 正在寻找 SCORE " << SCORE_TYPE(regtype_, tmpins.d) << " at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        if (it == m_hw_warps[warp_id]->score.end())
        {
            cout << "warp" << warp_id << "_wb_ena error: scoreboard can't find rd in score set, wb_ins=" << wb_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        }
        else
        {
            m_hw_warps[warp_id]->score.erase(it);
        }
        // cout << "warp" << warp_id << "_scoreboard: succesfully erased SCORE " << SCORE_TYPE(regtype_, tmpins.d) << ", wb_ins=" << wb_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }

    //
    // dispatch阶段，写入score
    //
    tmpins = m_hw_warps[warp_id]->ibuftop_ins; // this ibuftop_ins is the old data
    if (m_hw_warps[warp_id]->branch_sig || m_hw_warps[warp_id]->vbran_sig)
    {
        if (m_hw_warps[warp_id]->wait_bran == 0)
            cout << "warp" << warp_id << "_scoreboard error: detect (v)branch_sig=1(from salu) while wait_bran=0 at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        else if (m_hw_warps[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
            cout << "warp" << warp_id << "_scoreboard error: detect (v)branch_sig=1(from salu) while dispatch=1 at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        m_hw_warps[warp_id]->wait_bran = 0;
    }
    else if ((tmpins.ddd.branch != 0) &&
             m_hw_warps[warp_id]->dispatch_warp_valid && (!opc_full | doemit)) // 表示将要dispatch
    {
        // cout << "ibuf let wait_bran=1 at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";
        m_hw_warps[warp_id]->wait_bran = 1;
    }
    else if (tmpins.op == OP_TYPE::ENDPRG_ && m_hw_warps[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
    { // 权宜之计，让endprg后暂停dispatch
        // cout << "SM" << sm_id << " warp " << warp_id << " UPDATE_SCORE detect ENDPRG, suspend to dispatch at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        m_hw_warps[warp_id]->wait_bran = 1;
    }

    if (m_hw_warps[warp_id]->dispatch_warp_valid && (!opc_full | doemit))
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
            m_hw_warps[warp_id]->score.insert(SCORE_TYPE(regtype_, tmpins.d));
        // if (sm_id == 0)
        //     cout << "SM0 warp" << warp_id << "_scoreboard: insert " << SCORE_TYPE(regtype_, tmpins.d)
        //          << " because of dispatch " << tmpins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
}

void BASE::cycle_JUDGE_DISPATCH(int warp_id, I_TYPE &_readibuf)
{
    if (m_hw_warps[warp_id]->wait_bran | m_hw_warps[warp_id]->jump)
    {
        m_hw_warps[warp_id]->can_dispatch = false;
    }
    else if (!m_hw_warps[warp_id]->ififo.isempty())
    {
        _readibuf = m_hw_warps[warp_id]->ififo.front();
        m_hw_warps[warp_id]->can_dispatch = true;

        if (_readibuf.op == INVALID_)
            m_hw_warps[warp_id]->can_dispatch = false;

        if (_readibuf.ddd.wxd && m_hw_warps[warp_id]->score.find(SCORE_TYPE(s, _readibuf.d)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.wvd && m_hw_warps[warp_id]->score.find(SCORE_TYPE(v, _readibuf.d)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu1 == DecodeParams::A1_RS1 && m_hw_warps[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s1)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu1 == DecodeParams::A1_VRS1 && m_hw_warps[warp_id]->score.find(SCORE_TYPE(v, _readibuf.s1)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu2 == DecodeParams::sel_alu2_t::A2_RS2 && m_hw_warps[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s2)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu2 == DecodeParams::sel_alu2_t::A2_VRS2 && m_hw_warps[warp_id]->score.find(SCORE_TYPE(v, _readibuf.s2)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_FRS3 && m_hw_warps[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s3)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_VRS3 && m_hw_warps[warp_id]->score.find(SCORE_TYPE(v, _readibuf.s3)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_PC && _readibuf.ddd.branch == DecodeParams::branch_t::B_R && m_hw_warps[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s1)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_SD && (_readibuf.ddd.isvec & (!_readibuf.ddd.readmask)) && m_hw_warps[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s3)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;
        else if (_readibuf.ddd.sel_alu3 == DecodeParams::sel_alu3_t::A3_SD && !(_readibuf.ddd.isvec & (!_readibuf.ddd.readmask)) && m_hw_warps[warp_id]->score.find(SCORE_TYPE(s, _readibuf.s2)) != m_hw_warps[warp_id]->score.end())
            m_hw_warps[warp_id]->can_dispatch = false;

        // if (sm_id == 0 && warp_id == 0)
        //     if (m_hw_warps[warp_id]->can_dispatch == false)
        //         cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH=false with ins.bit=" << std::hex << _readibuf.origin32bit << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

        // if (sm_id == 0 && warp_id == 0 && _readibuf.origin32bit == uint32_t(0x96013057) && m_hw_warps[warp_id]->can_dispatch == false)
        // if (sm_id == 0 && warp_id == 0 && m_hw_warps[warp_id]->can_dispatch == false)
        //     cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH meet ins.bit=" << std::hex << _readibuf.origin32bit << std::dec << ", can't dispatch, ins.d=" << _readibuf.d << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
    }
    else if (m_hw_warps[warp_id]->ififo.isempty())
        m_hw_warps[warp_id]->can_dispatch = false;
}

void BASE::JUDGE_DISPATCH(int warp_id)
{
    I_TYPE _readibuf;
    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(m_hw_warps[warp_id]->ev_judge_dispatch & m_hw_warps[warp_id]->ev_ibuf_updated);
        // cout << "SM" << sm_id << " warp" << warp_id << " JUDGE_DISPATCH: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        // cout << "scoreboard: ibuftop_ins=" << ibuftop_ins << " at " << sc_time_stamp() <<","<< sc_delta_count_at_current_time() << "\n";

        cycle_JUDGE_DISPATCH(warp_id, _readibuf);

        m_hw_warps[warp_id]->ev_issue.notify();
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
        if (m_hw_warps[warp_id]->is_warp_activated)
        {
            // if (sm_id == 0 && warp_id == 0)
            // cout << "SM" << sm_id << " warp" << warp_id << " before action, fetch_valid2=" << m_hw_warps[warp_id]->fetch_valid2 << ", decode_ins=" << std::hex << m_hw_warps[warp_id]->decode_ins.read().origin32bit
            //      << std::dec << ", jump=" << m_hw_warps[warp_id]->jump << ", ififo.isfull=" << m_hw_warps[warp_id]->ififo.isfull() << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

            cycle_IBUF_ACTION(warp_id, dispatch_ins_, _readdata3);
            cycle_UPDATE_SCORE(warp_id, tmpins, it, regtype_, insertscore);
            cycle_JUDGE_DISPATCH(warp_id, _readibuf);
            m_hw_warps[warp_id]->ev_issue.notify();
        }
        else
        {
            // 某个warp结束后，依然出发issue_list，否则warp_scheduler无法运行
            m_hw_warps[warp_id]->ev_issue.notify();
        }

        if (m_hw_warps[warp_id]->flush_pipeline)
        {
            m_hw_warps[warp_id]->ififo_elem_num = m_hw_warps[warp_id]->ififo.used();
        }
    }
}

void BASE::activate_warp(int warp_id)
{
    if (m_hw_warps[warp_id] == nullptr)
    {
        WARP_BONE *new_warp_bone_ = new WARP_BONE(warp_id);
        m_hw_warps[warp_id] = new_warp_bone_;

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
    if (m_hw_warps[warp_id] != nullptr)
    {
        delete m_hw_warps[warp_id];
        m_hw_warps[warp_id] = nullptr;
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

void BASE::set_kernel(std::shared_ptr<kernel_info_t> kernel)
{
    assert(kernel);
    m_kernel = kernel;
    cout << "SM " << sm_id << " bind to kernel " << m_kernel->get_kid() << " \"" << m_kernel->get_kname() << "\" at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
}

bool BASE::can_issue_1block(std::shared_ptr<kernel_info_t> kernel)
{
    if (max_cta_num(kernel) < 1)
        return false;
    else
        return true;
}

unsigned BASE::max_cta_num(std::shared_ptr<kernel_info_t> kernel)
{
    unsigned kernel_num_thread_per_warp = kernel->get_num_thread_per_warp();
    unsigned kernel_num_warp_per_cta = kernel->get_num_warp_per_cta();
    if (kernel_num_thread_per_warp > hw_num_thread)
        return 0;

    // limited by warps
    unsigned result_warp;
    result_warp = hw_num_warp / kernel_num_warp_per_cta;

    // limited by local memory size
    unsigned kernel_ldsSize_per_cta = kernel->get_ldsSize_per_cta();
    unsigned result_localmem;
    result_localmem = hw_lds_size / kernel_ldsSize_per_cta;

    return result_warp < result_localmem ? result_warp : result_localmem;
}

void BASE::issue_block2core(std::shared_ptr<kernel_info_t> kernel)
{

    unsigned kernel_num_thread_per_warp = kernel->get_num_thread_per_warp();
    unsigned kernel_num_warp_per_cta = kernel->get_num_warp_per_cta();

    unsigned free_ctaid_in_core; // 为cta分配的core内ctaid. 对于给定kernel，存在ctaid和warp之间的确定映射
    unsigned hw_start_warpid = (unsigned)-1;

    // 找到core中空闲的一组warp和相应的ctaid
    for (int idx = 0; idx < MAX_CTA_PER_CORE; idx++)
    {
        unsigned start_warpid_try = idx * kernel_num_warp_per_cta;
        if (m_hw_warps[start_warpid_try]->is_warp_activated.read() == false)
        {
            free_ctaid_in_core = idx;
            hw_start_warpid = start_warpid_try;
            break;
        }
    }
    assert(free_ctaid_in_core != (unsigned)-1);

    dim3 ctaid_kernel = kernel->get_next_cta_id();
    unsigned ctaid_kernel_single = kernel->get_next_cta_id_single();

    // 遍历并激活每个warp
    for (unsigned widINcta = 0; widINcta < kernel_num_warp_per_cta; widINcta++)
    {
        unsigned hw_wid = widINcta + hw_start_warpid;
        m_hw_warps[hw_wid]->is_warp_activated.write(true);
        m_hw_warps[hw_wid]->m_ctaid_in_core = free_ctaid_in_core;
        m_hw_warps[hw_wid]->CSR_reg[0x800] = widINcta * kernel_num_thread_per_warp;
        m_hw_warps[hw_wid]->CSR_reg[0x801] = kernel_num_warp_per_cta;
        m_hw_warps[hw_wid]->CSR_reg[0x802] = kernel_num_thread_per_warp;
        m_hw_warps[hw_wid]->CSR_reg[0x803] = kernel->get_metadata_baseaddr();
        m_hw_warps[hw_wid]->CSR_reg[0x804] = free_ctaid_in_core;
        m_hw_warps[hw_wid]->CSR_reg[0x805] = widINcta;
        m_hw_warps[hw_wid]->CSR_reg[0x806] = ldsBaseAddr_core + free_ctaid_in_core * kernel->get_ldsSize_per_cta();
        m_hw_warps[hw_wid]->CSR_reg[0x807] =
            kernel->get_pdsBaseAddr() +
            ctaid_kernel_single * kernel_num_warp_per_cta *
                kernel_num_thread_per_warp * kernel->get_pdsSize_per_thread();
        m_hw_warps[hw_wid]->CSR_reg[0x808] = ctaid_kernel.x;
        m_hw_warps[hw_wid]->CSR_reg[0x809] = ctaid_kernel.y;
        m_hw_warps[hw_wid]->CSR_reg[0x80a] = ctaid_kernel.z;

        m_hw_warps[hw_wid]->CSR_reg[0x300] = 0x00001800; // WHY?

        m_hw_warps[hw_wid]->is_warp_activated.write(true);
        cout << "SM " << sm_id << " warp " << hw_wid << " is activated at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        m_hw_warps[hw_wid]->pc.write(kernel->get_startaddr());
        m_hw_warps[hw_wid]->fetch_valid.write(true);

        sc_bv<8> _validmask = 0;
        for (int i = 0; i < kernel_num_thread_per_warp; i++)
        {
            _validmask[i] = 1;
        }
        m_hw_warps[hw_wid]->current_mask.write(_validmask);
    }
    m_num_warp_activated += kernel_num_warp_per_cta;
    m_current_kernel_running.write(true);
    m_current_kernel_completed.write(false);
    kernel->increment_cta_id();
    cout << "SM " << sm_id << " issue 1 block of kernel \"" << kernel->get_kname() << "\" at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
}

void increment_x_then_y_then_z(dim3 &i, const dim3 &bound)
{
    i.x++;
    if (i.x >= bound.x)
    {
        i.x = 0;
        i.y++;
        if (i.y >= bound.y)
        {
            i.y = 0;
            if (i.z < bound.z)
                i.z++;
        }
    }
}