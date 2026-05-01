#include "subcore.hpp"
#include <cmath>
#include <limits>
#include <spdlog/spdlog.h>

namespace {
constexpr uint32_t F32_SIGN_BIT_MASK = 0x80000000u;
constexpr uint32_t F32_VALUE_BITS_MASK = 0x7fffffffu;

int32_t f32_to_i32(float value) {
    if (std::isnan(value)) {
        return std::numeric_limits<int32_t>::max();
    }
    if (value >= 2147483648.0f) {
        return std::numeric_limits<int32_t>::max();
    }
    if (value <= -2147483648.0f) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(value);
}

uint32_t f32_to_u32(float value) {
    if (std::isnan(value)) {
        return std::numeric_limits<uint32_t>::max();
    }
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 4294967296.0f) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(value);
}

}

void Subcore::VFPU_IN() {
    vfpu_in_t new_data;
    int a_delay, b_delay;
    while (true) {
        wait();
        if (emito_vfpu) {
            if (vfpu_ready_old == false) {
                std::cout << "vfpu error: not ready at " << sc_time_stamp() << ","
                          << sc_delta_count_at_current_time() << "\n";
            }
            vfpu_unready.notify();
            new_data.ins = emit_ins;
            new_data.warp_id = emitins_warpid;

            for (int i = 0; i < m_hw_warps[new_data.warp_id]->CSR_reg[0x802]; i++) {
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
            else {
                vfpu_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                ev_vfpufifo_pushed.notify();
            }
            if (b_delay == 0)
                vfpu_evb.notify();
            else {
                vfpu_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                ev_vfpuready_updated.notify();
            }
        } else {
            if (!vfpueqa_triggered)
                ev_vfpufifo_pushed.notify();
            if (!vfpueqb_triggered)
                ev_vfpuready_updated.notify();
        }
    }
}

void Subcore::VFPU_CALC() {
    vfpufifo_elem_num = 0;
    vfpufifo_empty = true;
    vfpueqa_triggered = false;
    // vfpu_in_t vfputmp1;
    vfpu_out_t vfputmp2;
    bool succeed;
    float source_f1, source_f2, source_f3;
    while (true) {
        wait(vfpu_eva | vfpu_eqa.default_event());
        if (vfpu_eqa.default_event().triggered()) {
            vfpueqa_triggered = true;
            wait(SC_ZERO_TIME);
            vfpueqa_triggered = false;
        }
        const vfpu_in_t vfputmp1 = vfpu_dq.front();
        vfpu_dq.pop();
        auto& hwarp = m_hw_warps[vfputmp1.warp_id];
        std::array<iuf32_t, hw_num_thread> src1, src2, src3, dst;
        for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
            src1[i].u32 = vfputmp1.vfpuSdata1[i];
            src2[i].u32 = vfputmp1.vfpuSdata2[i];
            src3[i].u32 = vfputmp1.vfpuSdata3[i];
        }
        auto calc_helper
            = [&vfputmp1, num_thread = hwarp->CSR_reg[0x802], &src1, &src2, &src3,
               &dst](std::function<iuf32_t(iuf32_t op1, iuf32_t op2, iuf32_t op3)> calc) {
                  exec_calc_helper(vfputmp1.ins, num_thread, src1, src2, src3, dst, calc);
              };

        if (vfputmp1.ins.ddd.wxd | vfputmp1.ins.ddd.wvd) {
            vfputmp2.ins = vfputmp1.ins;
            vfputmp2.warp_id = vfputmp1.warp_id;
            switch (vfputmp1.ins.ddd.alu_fn) {
            case DecodeParams::alu_fn_t::FN_FADD: // VFADD.VF, VFADD.VV, FADD.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = op1.f32 + op2.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FSUB: // VFRSUB.VF, VFSUB.VF, VFSUB.VV, FADD.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = op1.f32 - op2.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FMUL: // VFMUL.VF, VFMUL.VV, FMUL.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = op1.f32 * op2.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FMADD: // VFMACC.VF, VFMACC.VV, FMADD.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = +(op1.f32 * op2.f32) + op3.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_VFMADD: // VFMADD.VF, VFMADD.VV
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = +(op1.f32 * op3.f32) + op2.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FLT: // VMFGT.VF, VMFLT.VF, VMFLT.VV , FLT.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .i32 = (op1.f32 < op2.f32) };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FEQ: // VMFEQ.VF, VMFEQ.VV, FEQ.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .i32 = (op1.f32 == op2.f32) };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FLE: // VMFGE.VF, VMFLE.VF, VMFLE.VV, FLE.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .i32 = (op1.f32 <= op2.f32) };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FNE: // VMFNE.VF, VMFNE.VV
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .i32 = (op1.f32 != op2.f32) };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FSGNJ: // VFSGNJ.VF, VFSGNJ.VV, FSGNJ.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = (op1.u32 & F32_VALUE_BITS_MASK) | (op2.u32 & F32_SIGN_BIT_MASK) };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FSGNJN: // VFSGNJN.VF, VFSGNJN.VV, FSGNJN.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = (op1.u32 & F32_VALUE_BITS_MASK) | (~op2.u32 & F32_SIGN_BIT_MASK) };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FSGNJX: // VFSGNJX.VF, VFSGNJX.VV, FSGNJX.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t {
                        .u32 = (op1.u32 & F32_VALUE_BITS_MASK) | ((op1.u32 ^ op2.u32) & F32_SIGN_BIT_MASK)
                    };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FMAX: // VFMAX.VF, VFMAX.VV, FMAX.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = (op1.f32 > op2.f32) ? op1.f32 : op2.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_FMIN: // VFMIN.VF, VFMIN.VV, FMIN.S
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = (op1.f32 < op2.f32) ? op1.f32 : op2.f32 };
                });
                break;
            case DecodeParams::alu_fn_t::FN_F2I: {
                const bool force_rm_rtz = vfputmp1.ins.ddd.force_rm_rtz;
                calc_helper([force_rm_rtz](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    float value = op1.f32;
                    if (!force_rm_rtz) {
                        value = std::nearbyint(value);
                    }
                    return iuf32_t { .i32 = f32_to_i32(value) };
                });
                break;
            }
            case DecodeParams::alu_fn_t::FN_F2IU: {
                const bool force_rm_rtz = vfputmp1.ins.ddd.force_rm_rtz;
                calc_helper([force_rm_rtz](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    float value = op1.f32;
                    if (!force_rm_rtz) {
                        value = std::nearbyint(value);
                    }
                    return iuf32_t { .u32 = f32_to_u32(value) };
                });
                break;
            }
            case DecodeParams::alu_fn_t::FN_I2F: {
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = static_cast<float>(op1.i32) };
                });
                break;
            }
            case DecodeParams::alu_fn_t::FN_IU2F: {
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .f32 = static_cast<float>(op1.u32) };
                });
                break;
            }
            default:
                SPDLOG_LOGGER_ERROR(m_logger, "VFPU unrecognized ins {}", vfputmp1.ins);
                assert(0);
                break;
            }
            if (vfputmp1.ins.ddd.wxd) {
                vfputmp2.rds1_data = dst[0].i32;
            } else if (vfputmp1.ins.ddd.wvd) {
                for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
                    vfputmp2.rdf1_data[i] = dst[i].i32;
                }
            }
            vfpufifo.push(vfputmp2);
        } else {
        }

        ev_vfpufifo_pushed.notify();
    }
}

void Subcore::VFPU_CTRL() {
    vfpu_ready = true;
    vfpu_ready_old = true;
    vfpueqb_triggered = false;
    while (true) {
        wait(vfpu_eqb.default_event() | vfpu_unready | vfpu_evb);
        if (vfpu_eqb.default_event().triggered()) {
            vfpu_ready = true;
            vfpu_ready_old = vfpu_ready;
            vfpueqb_triggered = true;
            wait(SC_ZERO_TIME);
            vfpueqb_triggered = false;
            ev_vfpuready_updated.notify();
        } else if (vfpu_evb.triggered()) {
            vfpu_ready = true;
            vfpu_ready_old = vfpu_ready;
            ev_vfpuready_updated.notify();
        } else if (vfpu_unready.triggered()) {
            vfpu_ready = false;
            vfpu_ready_old = vfpu_ready;
            ev_vfpuready_updated.notify();
        }
    }
}
