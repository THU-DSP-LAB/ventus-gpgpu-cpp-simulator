#include "subcore.hpp"
#include <spdlog/spdlog.h>

namespace {
uint32_t high_unsigned_product(uint32_t lhs, uint32_t rhs) {
    return static_cast<uint32_t>((static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs)) >> 32);
}

uint32_t high_signed_product(int32_t lhs, int32_t rhs) {
    return static_cast<uint32_t>((static_cast<__int128_t>(lhs) * static_cast<__int128_t>(rhs)) >> 32);
}

uint32_t high_signed_unsigned_product(int32_t lhs, uint32_t rhs) {
    return static_cast<uint32_t>((static_cast<__int128_t>(lhs) * static_cast<__int128_t>(rhs)) >> 32);
}

uint32_t low_product(uint32_t lhs, uint32_t rhs) {
    return lhs * rhs;
}

uint32_t low_madd(uint32_t lhs, uint32_t rhs, uint32_t addend) {
    return low_product(lhs, rhs) + addend;
}

uint32_t low_msub(uint32_t lhs, uint32_t rhs, uint32_t minuend) {
    return minuend - low_product(lhs, rhs);
}

uint32_t signed_unsigned_high(int32_t lhs, uint32_t rhs) {
    return high_signed_unsigned_product(lhs, rhs);
}
}

void Subcore::MUL_IN() {
    mul_in_t new_data;
    int a_delay, b_delay;
    while (true) {
        wait();
        if (emito_mul) {
            if (mul_ready_old == false)
                std::cout << "mul error: not ready at " << sc_time_stamp() << ","
                          << sc_delta_count_at_current_time() << "\n";
            mul_unready.notify();
            switch (emit_ins.read().op) {

            default:
                new_data.ins = emit_ins;
                new_data.warp_id = emitins_warpid;
                for (int i = 0; i < m_hw_warps[new_data.warp_id]->CSR_reg[0x802]; i++) {
                    new_data.rsv1_data[i] = tomul_data1[i];
                    new_data.rsv2_data[i] = tomul_data2[i];
                    new_data.rsv3_data[i] = tomul_data3[i];
                }
                mul_dq.push(new_data);
                a_delay = 3;
                b_delay = 1;
                // std::cout << "mul: receive VADD_VV_, will notify eq, at " << sc_time_stamp()
                // <<","<< sc_delta_count_at_current_time() << "\n";
                if (a_delay == 0)
                    mul_eva.notify();
                else if (muleqa_triggered)
                    mul_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                else {
                    mul_eqa.notify(sc_time((a_delay)*PERIOD, SC_NS));
                    ev_mulfifo_pushed.notify();
                }
                if (b_delay == 0)
                    mul_evb.notify();
                else {
                    mul_eqb.notify(sc_time((b_delay)*PERIOD, SC_NS));
                    ev_mulready_updated.notify();
                }
                break;

                // default:
                //     std::cout << "mul error: receive wrong ins " << emit_ins << " at " <<
                //     sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                // break;
            }
        } else {
            if (!muleqa_triggered) {
                ev_mulfifo_pushed.notify();
            }
            if (!muleqb_triggered)
                ev_mulready_updated.notify();
        }
    }
}

void Subcore::MUL_CALC() {
    mulfifo_elem_num = 0;
    mulfifo_empty = 1;
    muleqa_triggered = false;
    mul_in_t multmp1;
    mul_out_t multmp2;
    bool succeed;
    while (true) {
        wait(mul_eva | mul_eqa.default_event());
        if (mul_eqa.default_event().triggered()) {
            muleqa_triggered = true;
            wait(SC_ZERO_TIME);
            muleqa_triggered = false;
        }
        // std::cout << "mul_eqa.default_event triggered at " << sc_time_stamp() << "," <<
        // sc_delta_count_at_current_time() << "\n";
        multmp1 = mul_dq.front();
        mul_dq.pop();
        auto& hwarp = m_hw_warps[multmp1.warp_id];
        std::array<iuf32_t, hw_num_thread> src1, src2, src3, dst;
        for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
            src1[i].i32 = multmp1.rsv1_data[i];
            src2[i].i32 = multmp1.rsv2_data[i];
            src3[i].i32 = multmp1.rsv3_data[i];
        }
        if (multmp1.ins.ddd.wxd | multmp1.ins.ddd.wvd) {
            multmp2.ins = multmp1.ins;
            multmp2.warp_id = multmp1.warp_id;

            auto calc_helper
                = [&multmp1, num_thread = hwarp->CSR_reg[0x802], &src1, &src2, &src3,
                   &dst](std::function<iuf32_t(iuf32_t op1, iuf32_t op2, iuf32_t op3)> calc) {
                      exec_calc_helper(multmp1.ins, num_thread, src1, src2, src3, dst, calc);
                  };

            switch (multmp1.ins.ddd.alu_fn) {

            case DecodeParams::alu_fn_t::FN_MUL: // VMUL.VV, VMUL.VX
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = low_product(op1.u32, op2.u32) };
                });
                break;

            case DecodeParams::alu_fn_t::FN_MULH:
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = high_signed_product(op1.i32, op2.i32) };
                });
                break;

            case DecodeParams::alu_fn_t::FN_MULHU:
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = high_unsigned_product(op1.u32, op2.u32) };
                });
                break;

            case DecodeParams::alu_fn_t::FN_MULHSU:
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = signed_unsigned_high(op1.i32, op2.u32) };
                });
                break;

            case DecodeParams::alu_fn_t::FN_MACC:
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = low_madd(op1.u32, op2.u32, op3.u32) };
                });
                break;

            case DecodeParams::alu_fn_t::FN_NMSAC:
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = low_msub(op1.u32, op2.u32, op3.u32) };
                });
                break;

            case DecodeParams::alu_fn_t::FN_MADD: // VMADD
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = low_madd(op2.u32, op3.u32, op1.u32) };
                });
                break;

            case DecodeParams::alu_fn_t::FN_NMSUB:
                calc_helper([](iuf32_t op1, iuf32_t op2, iuf32_t op3) {
                    return iuf32_t { .u32 = low_msub(op2.u32, op3.u32, op1.u32) };
                });
                break;

            default:
                SPDLOG_LOGGER_ERROR(
                    m_logger, "MUL_CALC unrecognized ins {} alu_fn={} @ {},{}", multmp1.ins,
                    static_cast<int>(multmp1.ins.ddd.alu_fn), sc_time_stamp().to_string(),
                    sc_delta_count_at_current_time()
                );
                assert(0);
                break;
            }
            for (int i = 0; i < hwarp->CSR_reg[0x802]; i++) {
                multmp2.rdv1_data[i] = dst[i].i32;
            }
            mulfifo.push(multmp2);
        } else {
            switch (multmp1.ins.op) {

            default:
                SPDLOG_LOGGER_ERROR(
                    m_logger, "MUL_CALC unrecognized no-write ins {} @ {},{}", multmp1.ins,
                    sc_time_stamp().to_string(), sc_delta_count_at_current_time()
                );
                assert(0);
                break;
            }
        }

        ev_mulfifo_pushed.notify();
    }
}

void Subcore::MUL_CTRL() {
    mul_ready = true;
    mul_ready_old = true;
    muleqb_triggered = false;
    while (true) {
        wait(mul_eqb.default_event() | mul_unready | mul_evb);
        if (mul_eqb.default_event().triggered()) {
            mul_ready = true;
            mul_ready_old = mul_ready;
            muleqb_triggered = true;
            wait(SC_ZERO_TIME);
            muleqb_triggered = false;
            ev_mulready_updated.notify();
        } else if (mul_evb.triggered()) {
            mul_ready = true;
            mul_ready_old = mul_ready;
            ev_mulready_updated.notify();
        } else if (mul_unready.triggered()) {
            mul_ready = false;
            mul_ready_old = mul_ready;
            ev_mulready_updated.notify();
        }
    }
}
