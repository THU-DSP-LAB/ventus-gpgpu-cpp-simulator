#include "subcore.hpp"
#include <memory>
#include <spdlog/spdlog.h>

void Subcore::DECODE() {
    sc_bv<32> scinsbit;
    while (true) {
        wait(clk.posedge_event());
        decode_output = decode_t { -1, nullptr }; // default: no valid instruction decoded

        // pipeline flush: regext
        for (int warp_id = 0; warp_id < m_hw_warps.size(); warp_id++) {
            if (fetch_need_flush(warp_id)) {
                // pipeline flushed, regext cleared
                m_hw_warps[warp_id]->regext.valid = false;
            }
        }

        // check previous fetch result
        auto& fetch = fetch2_reg.read();
        auto& warp_id = fetch.warp_id;
        if (fetch.from == fetch_t::FETCH_FROM::NONE || !fetch.success) {
            // no valid fetch, do not decode
            decode_output = decode_t { -1, nullptr };
            ev_decode_finish.notify();
            continue;
        }

        // check before instruction decode
        auto& hwarp = m_hw_warps.at(warp_id);
        auto& regext = hwarp->regext;
        if (fetch_need_flush(warp_id)) {
            // pipeline flushed, do not decode
            decode_output = decode_t { -1, nullptr };
            assert(!regext.valid); // cleared above
            ev_decode_finish.notify();
            continue;
        }

        //
        // instruction decode start
        //

        auto instr = std::make_shared<I_TYPE>(fetch2_instr.read(), fetch.pc);
        decode_output.warp_id = warp_id;

        // decode step 1: find instruction in decode table
        bool foundBitIns = 0;
        std::bitset<32> _ins = instr->origin32bit;
        if ((_ins & std::bitset<32>(0x7f)) == 0) {
            // Auxiliary self-defined debug/print instruction only for this simulator
            // this is not in decode table
            instr->op = (int)CUSTOM_PRINT_;
            foundBitIns = true;
        } else { // find instruction in decode table
            for (const auto& instable_item : *m_instruction_table) {
                std::bitset<32> masked_ins
                    = std::bitset<32>(instr->origin32bit) & instable_item.mask;
                // std::cout << "warp" << warp_id << " DECODE: mask=" << instable_item.mask <<
                // ", masked_ins=" << masked_ins << " at " << sc_time_stamp() << "," <<
                // sc_delta_count_at_current_time() << std::endl;
                auto it = instable_item.itable.find(masked_ins);
                if (it != instable_item.itable.end()) {
                    instr->op = it->second;
                    foundBitIns = true;
                    break;
                }
            }
        }
        if (!foundBitIns) { // instruction not found in decode table
            // 发现非法指令，但不能直接报错，因为这条指令可能后续不会实际发射执行
            // 例如，可能是.text段之后的垃圾数据，在执行前就会跳转走
            instr->op = INVALID_;
            decode_output.instr = std::move(instr);
            ev_decode_finish.notify();
            continue;
        } else {
            // std::cout << "warp" << warp_id << " DECODE: match ins bit=" <<
            // std::bitset<32>(instr->origin32bit) << " with " <<
            // magic_enum::enum_name((OP_TYPE)instr->op) << " at " << sc_time_stamp() << "," <<
            // sc_delta_count_at_current_time() << std::endl;
        }

        // decode step 2: extract fields from instruction bits
        // firstly deal with some special instructions: regext(i) & custom_print
        if (instr->op == (int)REGEXT_) {
            // hwarp->decode_valid = false; // regext ends here
            decode_output.instr = nullptr; // regext ends here in decode
            regext.valid = true;

            regext.extimm = 0;
            regext.ext3 = extractBits32(instr->origin32bit, 31, 29);
            regext.ext2 = extractBits32(instr->origin32bit, 28, 26);
            regext.ext1 = extractBits32(instr->origin32bit, 25, 23);
            regext.extd = extractBits32(instr->origin32bit, 22, 20);
#ifdef SPIKE_OUTPUT
            SPDLOG_LOGGER_TRACE(
                m_logger, "SM {} warp {} 0x{:x} {} REGEXT(s3,s2,s1,d)={},{},{},{}", m_sm_id,
                warpid_convert(m_subcore_id, warp_id), instr->currentpc, *instr, regext.ext3,
                regext.ext2, regext.ext1, regext.extd
            );
#endif
        } else if (instr->op == (int)REGEXTI_) {
            // hwarp->decode_valid = false; // regext ends here
            decode_output.instr = nullptr; // regext ends here in decode
            regext.valid = true;

            regext.extimm = extractBits32(instr->origin32bit, 31, 26);
            regext.ext3 = 0;
            regext.ext2 = extractBits32(instr->origin32bit, 25, 23);
            regext.ext1 = 0;
            regext.extd = extractBits32(instr->origin32bit, 22, 20);
#ifdef SPIKE_OUTPUT
            SPDLOG_LOGGER_TRACE(
                m_logger, "SM {} warp {} 0x{:x} {} REGEXTI(s3,s2,s1,d)={},{},{},{}", m_sm_id,
                warpid_convert(m_subcore_id, warp_id), instr->currentpc, *instr, regext.ext3,
                regext.ext2, regext.ext1, regext.extd
            );
#endif
        } else if (instr->op == (int)CUSTOM_PRINT_) {
            // Auxiliary self-defined debug/print instruction only for this simulator
            instr->ddd = m_decode_table->at(OP_TYPE::VADD_VX_); // they are similar
            instr->ddd.sel_execunit = DecodeParams::INVALID_EXECUNIT;
            instr->ddd.alu_fn = DecodeParams::FN_X;
            instr->ddd.wvd = false;
            instr->ddd.wxd = false;
            instr->s1 = extractBits32(instr->origin32bit, 19, 15);
            instr->s2 = extractBits32(instr->origin32bit, 24, 20);
            instr->d = 0;
            if (regext.valid) {
                instr->is_extended = true;
                instr->s1 += regext.ext1 << 5;
                instr->s2 += regext.ext2 << 5;
                instr->s3 += ((instr->ddd.fp && !instr->ddd.isvec) ? regext.ext3 : regext.extd)
                    << 5;
                instr->d += regext.extd << 5;
                regext.valid = false;
            }
            decode_output.instr = std::move(instr);
        } else if (m_decode_table->contains((OP_TYPE)instr->op)) {
            // normal instruction: op != REGEXT_ && op != REGEXTI_
            instr->ddd = m_decode_table->at((OP_TYPE)instr->op);
            instr->ddd.decode_ext(instr->origin32bit);
            // hwarp->decode_valid = hwarp->fetch_valid;
            if (instr->ddd.tc)
                instr->ddd.sel_execunit = DecodeParams::TC;
            else if (instr->ddd.sfu)
                instr->ddd.sel_execunit = DecodeParams::SFU;
            else if (instr->ddd.fp)
                instr->ddd.sel_execunit = DecodeParams::VFPU;
            else if (instr->ddd.csr != 0)
                instr->ddd.sel_execunit = DecodeParams::CSR;
            else if (instr->ddd.mul)
                instr->ddd.sel_execunit = DecodeParams::MUL;
            else if (instr->ddd.mem_cmd != 0)
                instr->ddd.sel_execunit = DecodeParams::LSU;
            else if (instr->ddd.isvec) {
                if (instr->op == JOIN_)
                    instr->ddd.sel_execunit = DecodeParams::SIMTSTK;
                else
                    instr->ddd.sel_execunit = DecodeParams::VALU;
            } else if (instr->ddd.barrier)
                instr->ddd.sel_execunit = DecodeParams::WPSCHEDLER;
            else
                instr->ddd.sel_execunit = DecodeParams::SALU;

            instr->s1 = extractBits32(instr->origin32bit, 19, 15);
            instr->s2 = extractBits32(instr->origin32bit, 24, 20);
            instr->s3 = (instr->ddd.fp && !instr->ddd.isvec)
                ? extractBits32(instr->origin32bit, 31, 27)
                : extractBits32(instr->origin32bit, 11, 7);
            instr->d = extractBits32(instr->origin32bit, 11, 7);
            if (regext.valid) {
                instr->is_extended = true;
                instr->imm += regext.extimm << 5;
                instr->s1 += regext.ext1 << 5;
                instr->s2 += regext.ext2 << 5;
                // 这里与Chisel实现有所不同，Chisel要么使用extd，要么就不扩展（认定ext3=0）
                // c.reg_idx3 := Mux(c.fp & !c.isvec, Cat(0.U(3.W),io.inst(i)(31, 27)),
                // Cat(regextInfo(i).regPrefix(0) ,io.inst(i)(11, 7)))
                instr->s3 += ((instr->ddd.fp && !instr->ddd.isvec) ? regext.ext3 : regext.extd)
                    << 5;
                instr->d += regext.extd << 5;
                regext.valid = false;
#ifdef SPIKE_OUTPUT
                SPDLOG_LOGGER_TRACE(
                    m_logger,
                    "SM {} warp {} 0x{:x} {} REGEXT(s3,s2,s1,d)={},{},{},{} is used to set "
                    "s3,s2,s1,d={},{},{},{}",
                    m_sm_id, warpid_convert(m_subcore_id, warp_id), instr->currentpc, *instr,
                    regext.ext3, regext.ext2, regext.ext1, regext.extd, instr->s3, instr->s2,
                    instr->s1, instr->d
                );
#endif
            }

            // immediate extraction
            scinsbit = instr->origin32bit;
            switch (instr->ddd.sel_imm) {
            case DecodeParams::sel_imm_t::IMM_I:
                instr->imm = scinsbit.range(31, 20).to_int(); // to_int() sign-extend
                break;
            case DecodeParams::sel_imm_t::IMM_S:
                instr->imm = (scinsbit.range(31, 25), scinsbit.range(11, 7)).to_int();
                break;
            case DecodeParams::sel_imm_t::IMM_B:
                instr->imm = (scinsbit.range(31, 31), scinsbit.range(7, 7), scinsbit.range(30, 25),
                              scinsbit.range(11, 8))
                                 .to_int()
                    << 1;
                break;
            case DecodeParams::sel_imm_t::IMM_U:
                instr->imm = (scinsbit.range(31, 12)).to_int() << 12;
                break;
            case DecodeParams::sel_imm_t::IMM_J:
                instr->imm = (scinsbit.range(31, 31), scinsbit.range(19, 12),
                              scinsbit.range(20, 20), scinsbit.range(30, 21))
                                 .to_int()
                    << 1;
                break;
            case DecodeParams::sel_imm_t::IMM_Z:
                instr->imm = (scinsbit.range(19, 15)).to_uint();
                break;
            case DecodeParams::sel_imm_t::IMM_2:
                instr->imm = (scinsbit.range(24, 20)).to_int();
                break;
            case DecodeParams::sel_imm_t::IMM_V: // 和scala不一样，需要修改，加位拓展
                instr->imm = (scinsbit.range(19, 15)).to_int();
                break;
            case DecodeParams::sel_imm_t::IMM_L11:
                instr->imm = (scinsbit.range(30, 20)).to_int();
                break;
            case DecodeParams::sel_imm_t::IMM_S11:
                instr->imm = (scinsbit.range(30, 25), scinsbit.range(11, 7)).to_int();
                break;
            default:
                break;
            }
            // hwarp->decode_ins = tmpins;
            decode_output.instr = std::move(instr);
        } else {
            // 发现非法指令，但不能直接报错，因为这条指令可能后续不会实际发射执行
            // 例如，可能是.text段之后的垃圾数据，在执行前就会跳转走
            instr->op = INVALID_;
            decode_output.instr = std::move(instr);
        }
        ev_decode_finish.notify(); // decode finish, notify IBUF to process input
    }
}
