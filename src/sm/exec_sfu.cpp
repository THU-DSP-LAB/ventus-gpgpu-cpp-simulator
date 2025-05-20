#include "subcore.hpp"

void Subcore::SFU_IN() {
    sfu_in_t new_data;
    int a_delay, b_delay;
    while (true) {
        wait();
        if (emito_sfu) {
            if (sfu_ready_old == false)
                std::cout << "sfu error: not ready at " << sc_time_stamp() << ","
                          << sc_delta_count_at_current_time() << "\n";
            sfu_unready.notify();
            switch (emit_ins.read().op) {

            default:
                new_data.ins = emit_ins;
                new_data.warp_id = emitins_warpid;
                for (int i = 0; i < m_hw_warps[new_data.warp_id]->CSR_reg[0x802]; i++) {
                    new_data.rsv1_data[i] = tosfu_data1[i];
                    new_data.rsv2_data[i] = tosfu_data2[i];
                }
                sfu_dq.push(new_data);
                a_delay = 3;
                b_delay = 1;
                // std::cout << "sfu: receive VADD_VV_, will notify eq, at " << sc_time_stamp()
                // <<","<< sc_delta_count_at_current_time() << "\n";
                if (a_delay == 0)
                    sfu_eva.notify();
                else if (sfueqa_triggered)
                    sfu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                else {
                    sfu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                    ev_sfufifo_pushed.notify();
                }
                if (b_delay == 0)
                    sfu_evb.notify();
                else {
                    sfu_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                    ev_sfuready_updated.notify();
                }
                break;

                // default:
                //     std::cout << "sfu error: receive wrong ins " << emit_ins << " at " <<
                //     sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                // break;
            }
        } else {
            if (!sfueqa_triggered) {
                ev_sfufifo_pushed.notify();
            }
            if (!sfueqb_triggered)
                ev_sfuready_updated.notify();
        }
    }
}

void Subcore::SFU_CALC() {
    sfufifo_elem_num = 0;
    sfufifo_empty = 1;
    sfueqa_triggered = false;
    sfu_in_t sfutmp1;
    sfu_out_t sfutmp2;
    bool succeed;
    while (true) {
        wait(sfu_eva | sfu_eqa.default_event());
        if (sfu_eqa.default_event().triggered()) {
            sfueqa_triggered = true;
            wait(SC_ZERO_TIME);
            sfueqa_triggered = false;
        }
        // std::cout << "sfu_eqa.default_event triggered at " << sc_time_stamp() << "," <<
        // sc_delta_count_at_current_time() << "\n";
        sfutmp1 = sfu_dq.front();
        sfu_dq.pop();
        auto& hwarp = m_hw_warps[sfutmp1.warp_id];
        std::array<iuf32_t, hw_num_thread> src1, src2, dst;
        for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
            src1[i].i32 = sfutmp1.rsv1_data[i];
            src2[i].i32 = sfutmp1.rsv2_data[i];
        }
        if (sfutmp1.ins.ddd.wxd | sfutmp1.ins.ddd.wvd) {
            sfutmp2.ins = sfutmp1.ins;
            sfutmp2.warp_id = sfutmp1.warp_id;

            // helper function for instruction execution
            auto calc_helper
                = [&sfutmp1, num_thread = hwarp->CSR_reg[0x802], &src1, &src2,
                   &dst](std::function<iuf32_t(iuf32_t op1, iuf32_t op2, iuf32_t op3)> calc) {
                      exec_calc_helper(sfutmp1.ins, num_thread, src1, src2, src2, dst, calc);
                  };

            switch (sfutmp1.ins.ddd.alu_fn) {
            case DecodeParams::alu_fn_t::FN_REMU: // VREMU.VV, VREMU.VX, REMU
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = (op2.u32 == 0) ? op1.u32 : op1.u32 % op2.u32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_DIVU: // VDIVU.VV, VDIVU.VX, DIVU
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = (op2.u32 == 0) ? (uint32_t)(-1) : op1.u32 / op2.u32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_REM: // VREM.VV, VREM.VX, REM
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .i32 = (op2.i32 == 0) ? op1.i32 : op1.i32 % op2.i32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_DIV: // VDIV.VV, VDIV.VX, DIV
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .i32 = (op2.i32 == 0) ? -1 : op1.i32 / op2.i32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FDIV: // VFDIV.VV, VFDIV.VF, VFRDIV.VF, FDIV
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = op1.f32 / op2.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_EXP: // VFEXP.V
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
                    if (sfutmp1.ins.mask[i] == 1) {
                        dst[i].f32 = expf(src2[i].f32);
                    }
                }
                break;
            case DecodeParams::alu_fn_t::FN_FSQRT: // VFSQRT.V, FSQRT.S
                if (sfutmp1.ins.ddd.isvec) {       // VFSQRT.V
                    assert(sfutmp1.ins.ddd.reverse);
                    for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
                        if (sfutmp1.ins.mask[i] == 1) {
                            dst[i].f32 = sqrtf(src2[i].f32);
                        }
                    }
                } else { // FSQRT.S
                    dst[0].f32 = sqrtf(src1[0].f32);
                }
                break;
            default:
                std::cout << "SFU_CALC warning: switch to unrecognized ins" << sfutmp1.ins << " at "
                          << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                assert(0);
                break;
            }

            for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
                sfutmp2.rdv1_data[i] = dst[i].i32;
            }
            sfufifo.push(sfutmp2);
        } else {
            switch (sfutmp1.ins.op) {
            default:
                std::cout << "SFU_CALC warning: switch to unrecognized ins" << sfutmp1.ins << " at "
                          << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                assert(0);
                break;
            }
        }

        ev_sfufifo_pushed.notify();
    }
}

void Subcore::SFU_CTRL() {
    sfu_ready = true;
    sfu_ready_old = true;
    sfueqb_triggered = false;
    while (true) {
        wait(sfu_eqb.default_event() | sfu_unready | sfu_evb);
        if (sfu_eqb.default_event().triggered()) {
            sfu_ready = true;
            sfu_ready_old = sfu_ready;
            sfueqb_triggered = true;
            wait(SC_ZERO_TIME);
            sfueqb_triggered = false;
            ev_sfuready_updated.notify();
        } else if (sfu_evb.triggered()) {
            sfu_ready = true;
            sfu_ready_old = sfu_ready;
            ev_sfuready_updated.notify();
        } else if (sfu_unready.triggered()) {
            sfu_ready = false;
            sfu_ready_old = sfu_ready;
            ev_sfuready_updated.notify();
        }
    }
}
