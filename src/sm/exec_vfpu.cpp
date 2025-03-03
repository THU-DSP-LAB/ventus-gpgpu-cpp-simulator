#include "BASE.h"
#include <algorithm>

void BASE::VFPU_IN()
{
    vfpu_in_t new_data;
    int a_delay, b_delay;
    while (true)
    {
        wait();
        if (emito_vfpu)
        {
            if (vfpu_ready_old == false)
            {
                std::cout << "vfpu error: not ready at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            vfpu_unready.notify();
            new_data.ins = emit_ins;
            new_data.warp_id = emitins_warpid;

            for (int i = 0; i < m_hw_warps[new_data.warp_id]->CSR_reg[0x802]; i++)
            {
                new_data.vfpuSdata1[i] = tovfpu_data1[i];
                new_data.vfpuSdata2[i] = tovfpu_data2[i];
                new_data.vfpuSdata3[i] = tovfpu_data3[i];
            }

            vfpu_dq.push(new_data);
            a_delay = 5;
            b_delay = 1;

            if (a_delay == 0)
                vfpu_eva.notify();
            else if (vfpueqa_triggered)
                vfpu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
            else
            {
                vfpu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                ev_vfpufifo_pushed.notify();
            }
            if (b_delay == 0)
                vfpu_evb.notify();
            else
            {
                vfpu_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                ev_vfpuready_updated.notify();
            }
        }
        else
        {
            if (!vfpueqa_triggered)
                ev_vfpufifo_pushed.notify();
            if (!vfpueqb_triggered)
                ev_vfpuready_updated.notify();
        }
    }
}

void BASE::VFPU_CALC()
{
    vfpufifo_elem_num = 0;
    vfpufifo_empty = true;
    vfpueqa_triggered = false;
    vfpu_in_t vfputmp1;
    vfpu_out_t vfputmp2;
    bool succeed;
    float source_f1, source_f2, source_f3;
    while (true)
    {
        wait(vfpu_eva | vfpu_eqa.default_event());
        if (vfpu_eqa.default_event().triggered())
        {
            vfpueqa_triggered = true;
            wait(SC_ZERO_TIME);
            vfpueqa_triggered = false;
        }
        vfputmp1 = vfpu_dq.front();
        vfpu_dq.pop();
        auto& hwarp= m_hw_warps[vfputmp1.warp_id];
        std::array<float, hw_num_thread> src1, src2, src3, dst;
        for(int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
            src1[i] = std::bit_cast<float>(vfputmp1.vfpuSdata1[i]);
            src2[i] = std::bit_cast<float>(vfputmp1.vfpuSdata2[i]);
            src3[i] = std::bit_cast<float>(vfputmp1.vfpuSdata3[i]);
        }
        auto array_all_same_elem = [](const auto& arr) {
            return std::all_of(arr.begin(), arr.end(), [val = arr[0]](int x) { return x == val; });
        };
        if (vfputmp1.ins.ddd.wxd | vfputmp1.ins.ddd.wvd)
        {
            vfputmp2.ins = vfputmp1.ins;
            vfputmp2.warp_id = vfputmp1.warp_id;
            switch (vfputmp1.ins.ddd.alu_fn)
            {
            case DecodeParams::alu_fn_t::FN_FADD:
                // VFADD.VF, VFADD.VV
                dst.fill(src1[0] + src2[0]);
                break;
            case DecodeParams::alu_fn_t::FN_FMUL:
                dst.fill(src1[0] * src2[0]);
                break;
            case DecodeParams::alu_fn_t::FN_FMADD: // FMADD.S
                dst.fill(src1[0] * src2[0] + src3[0]);
                break;
            case DecodeParams::alu_fn_t::FN_VFMADD:
                if(vfputmp1.ins.ddd.sel_alu1 == DecodeParams::sel_alu1_t::A1_RS1) { // VFMADD.VF
                    for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                        dst[i] = src1[0] * src2[i] + src3[i];
                } else { // VFMADD.VV
                    assert(vfputmp1.ins.ddd.sel_alu1 == DecodeParams::sel_alu1_t::A1_VRS1);
                    for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                        dst[i] = src1[i] * src2[i] + src3[i];
                }
                break;
            case FSQRT_S_:
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(sqrtf32(std::bit_cast<float>(vfputmp1.vfpuSdata1[0])));
                break;
            case FCVT_W_S_:
                vfputmp2.rds1_data = (int)std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                break;
            case FCVT_WU_S_:
                vfputmp2.rds1_data = (unsigned int)std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                break;
            case FCLASS_S_:

                break;
            case FCVT_S_W_:
                vfputmp2.rdf1_data[0] = std::bit_cast<int>((float)vfputmp1.vfpuSdata1[0]);
                break;
            case FCVT_S_WU_:
                vfputmp2.rdf1_data[0] = std::bit_cast<int>((float)(unsigned)vfputmp1.vfpuSdata1[0]);
                break;
            case FMADD_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                source_f3 = std::bit_cast<float>(vfputmp1.vfpuSdata3[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 * source_f2 + source_f3);
                break;
            case FMSUB_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                source_f3 = std::bit_cast<float>(vfputmp1.vfpuSdata3[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 * source_f2 - source_f3);
                break;
            case FNMSUB_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                source_f3 = std::bit_cast<float>(vfputmp1.vfpuSdata3[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(-source_f1 * source_f2 + source_f3);
                break;
            case FNMADD_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                source_f3 = std::bit_cast<float>(vfputmp1.vfpuSdata3[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(-source_f1 * source_f2 - source_f3);
                break;
            case FADD_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 + source_f2);
                break;
            case FSUB_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 - source_f2);
                break;
            case FMUL_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 * source_f2);
                break;
            case FDIV_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 / source_f2);
                break;
            case FSGNJ_S_:
                vfputmp2.rdf1_data[0] = (vfputmp1.vfpuSdata1[0] & 0x7fffffff) | (vfputmp1.vfpuSdata2[0] & 0x80000000);
                break;
            case FSGNJN_S_:
                vfputmp2.rdf1_data[0] = (vfputmp1.vfpuSdata1[0] & 0x7fffffff) | ((vfputmp1.vfpuSdata2[0] & 0x80000000) ^ 0x80000000);
                break;
            case FSGNJX_S_:
                vfputmp2.rdf1_data[0] = (vfputmp1.vfpuSdata1[0] & 0x7fffffff) |
                                        ((vfputmp1.vfpuSdata2[0] & 0x80000000) ^ (vfputmp1.vfpuSdata2[0] & 0x80000000));
                break;
            case FMIN_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 < source_f2 ? source_f1 : source_f2);
                break;
            case FMAX_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rdf1_data[0] = std::bit_cast<int>(source_f1 > source_f2 ? source_f1 : source_f2);
                break;
            case FEQ_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rds1_data = source_f1 == source_f2;
                break;
            case FLT_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rds1_data = source_f1 < source_f2;
                break;
            case FLE_S_:
                source_f1 = std::bit_cast<float>(vfputmp1.vfpuSdata1[0]);
                source_f2 = std::bit_cast<float>(vfputmp1.vfpuSdata2[0]);
                vfputmp2.rds1_data = source_f1 <= source_f2;
                break;
            case VFMUL_VV_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(std::bit_cast<float>(vfputmp1.vfpuSdata1[i]) * std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFMUL_VF_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(std::bit_cast<float>(vfputmp1.vfpuSdata1[0]) + std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFMADD_VV_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        std::bit_cast<float>(vfputmp1.vfpuSdata1[i]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) + std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFMADD_VF_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        std::bit_cast<float>(vfputmp1.vfpuSdata1[0]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) + std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFNMADD_VV_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        -std::bit_cast<float>(vfputmp1.vfpuSdata1[i]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) - std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFNMADD_VF_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        -std::bit_cast<float>(vfputmp1.vfpuSdata1[0]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) - std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFMSUB_VV_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        std::bit_cast<float>(vfputmp1.vfpuSdata1[i]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) - std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFMSUB_VF_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        std::bit_cast<float>(vfputmp1.vfpuSdata1[0]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) - std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFNMSUB_VV_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        -std::bit_cast<float>(vfputmp1.vfpuSdata1[i]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) + std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFNMSUB_VF_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        -std::bit_cast<float>(vfputmp1.vfpuSdata1[0]) * std::bit_cast<float>(vfputmp1.vfpuSdata3[i]) + std::bit_cast<float>(vfputmp1.vfpuSdata2[i]));
                break;
            case VFMACC_VV_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        std::bit_cast<float>(vfputmp1.vfpuSdata1[i]) * std::bit_cast<float>(vfputmp1.vfpuSdata2[i]) + std::bit_cast<float>(vfputmp1.vfpuSdata3[i]));
                break;
            case VFMACC_VF_:
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++)
                    vfputmp2.rdf1_data[i] = std::bit_cast<int>(
                        std::bit_cast<float>(vfputmp1.vfpuSdata1[0]) * std::bit_cast<float>(vfputmp1.vfpuSdata2[i]) + std::bit_cast<float>(vfputmp1.vfpuSdata3[i]));
                break;
            default:
                std::cout << "VFPU_CALC warning: switch to unrecognized ins" << vfputmp1.ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                assert(0);
                break;
            }
            for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
                vfputmp2.rdf1_data[i] = std::bit_cast<int>(dst[i]);
            }
            vfpufifo.push(vfputmp2);
        }
        else
        {
        }

        ev_vfpufifo_pushed.notify();
    }
}

void BASE::VFPU_CTRL()
{
    vfpu_ready = true;
    vfpu_ready_old = true;
    vfpueqb_triggered = false;
    while (true)
    {
        wait(vfpu_eqb.default_event() | vfpu_unready | vfpu_evb);
        if (vfpu_eqb.default_event().triggered())
        {
            vfpu_ready = true;
            vfpu_ready_old = vfpu_ready;
            vfpueqb_triggered = true;
            wait(SC_ZERO_TIME);
            vfpueqb_triggered = false;
            ev_vfpuready_updated.notify();
        }
        else if (vfpu_evb.triggered())
        {
            vfpu_ready = true;
            vfpu_ready_old = vfpu_ready;
            ev_vfpuready_updated.notify();
        }
        else if (vfpu_unready.triggered())
        {
            vfpu_ready = false;
            vfpu_ready_old = vfpu_ready;
            ev_vfpuready_updated.notify();
        }
    }
}
