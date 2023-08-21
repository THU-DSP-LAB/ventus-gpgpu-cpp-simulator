#include "BASE.h"

void BASE::DECODE(int warp_id)
{
    I_TYPE tmpins;
    sc_bv<32> scinsbit;
    bool WILLregext = false;
    int ext1, ext2, ext3, extd;
    while (true)
    {
        // cout << "SM" << sm_id << " warp" << warp_id << " DECODE: finish at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        wait(WARPS[warp_id]->ev_decode);

        if (WARPS[warp_id]->jump == 1 |
            WARPS[warp_id]->simtstk_jump == 1)
        {
            WARPS[warp_id]->fetch_valid2 = false;
        }
        else
        { // cout << "SM" << sm_id << " warp" << warp_id << " DECODE: start at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            tmpins = I_TYPE(WARPS[warp_id]->fetch_ins, WARPS[warp_id]->pc.read());
            // if (sm_id == 0 && warp_id == 0)
            //     cout << "SM" << sm_id << " warp" << warp_id << " DECODE ins.bit=" << std::hex << tmpins.origin32bit << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";

            if (inssrc == "imem")
            {
                bool foundBitIns = 0;
                for (const auto &instable_item : instable_vec)
                {
                    std::bitset<32> masked_ins = std::bitset<32>(tmpins.origin32bit) & instable_item.mask;
                    // cout << "warp" << warp_id << " DECODE: mask=" << instable_item.mask << ", masked_ins=" << masked_ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                    auto it = instable_item.itable.find(masked_ins);
                    if (it != instable_item.itable.end())
                    {
                        tmpins.op = it->second;
                        foundBitIns = true;
                        break;
                    }
                }
                if (!foundBitIns)
                {
                    tmpins.op = INVALID_;
                    cout << "warp" << warp_id << " DECODE error: invalid bit ins " << std::bitset<32>(tmpins.origin32bit) << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }
                else
                {
                    // cout << "warp" << warp_id << " DECODE: match ins bit=" << std::bitset<32>(tmpins.origin32bit) << " with " << magic_enum::enum_name((OP_TYPE)tmpins.op) << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
                }
            }
            tmpins.ddd = decode_table[(OP_TYPE)tmpins.op];

            if (tmpins.op == (int)REGEXT_ || tmpins.op == (int)REGEXTI_)
            {
                WARPS[warp_id]->fetch_valid2 = false;
                WILLregext = true;

                ext3 = extractBits32(tmpins.origin32bit, 31, 29);
                ext2 = extractBits32(tmpins.origin32bit, 28, 26);
                ext1 = extractBits32(tmpins.origin32bit, 25, 23);
                extd = extractBits32(tmpins.origin32bit, 22, 20);
                cout << "SM" << sm_id << " warp " << warp_id << " DECODE: set regext(3,2,1,d)=" << ext3 << "," << ext2 << "," << ext1 << "," << extd << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
            }
            else
            {
                WARPS[warp_id]->fetch_valid2 = WARPS[warp_id]->fetch_valid12;
                // tmpins.ddd.mem = (tmpins.ddd.mem_cmd & 1) | ((tmpins.ddd.mem_cmd) >> 1 & 1);
                if (tmpins.ddd.tc)
                    tmpins.ddd.sel_execunit = DecodeParams::TC;
                else if (tmpins.ddd.sfu)
                    tmpins.ddd.sel_execunit = DecodeParams::SFU;
                else if (tmpins.ddd.fp)
                    tmpins.ddd.sel_execunit = DecodeParams::VFPU;
                else if (tmpins.ddd.csr != 0)
                    tmpins.ddd.sel_execunit = DecodeParams::CSR;
                else if (tmpins.ddd.mul)
                    tmpins.ddd.sel_execunit = DecodeParams::MUL;
                else if (tmpins.ddd.mem_cmd != 0)
                    tmpins.ddd.sel_execunit = DecodeParams::LSU;
                else if (tmpins.ddd.isvec)
                {
                    if (tmpins.op == JOIN_)
                        tmpins.ddd.sel_execunit = DecodeParams::SIMTSTK;
                    else
                        tmpins.ddd.sel_execunit = DecodeParams::VALU;
                }
                else if (tmpins.ddd.barrier)
                    tmpins.ddd.sel_execunit = DecodeParams::WPSCHEDLER;
                else
                    tmpins.ddd.sel_execunit = DecodeParams::SALU;

                if (sm_id == 0 && warp_id == 0 && tmpins.origin32bit == (uint32_t)0x5208a157)
                    cout << "SM" << sm_id << " warp" << warp_id << " DECODE: decoding ins " << std::hex << tmpins.origin32bit << std::dec
                         << ", isvec=" << WARPS[warp_id]->fetch_ins.ddd.isvec << ", sel_execunit=" << magic_enum::enum_name(tmpins.ddd.sel_execunit) << "\n";

                tmpins.s1 = extractBits32(tmpins.origin32bit, 19, 15);
                tmpins.s2 = extractBits32(tmpins.origin32bit, 24, 20);
                tmpins.s3 = (tmpins.ddd.fp & !tmpins.ddd.isvec)
                                ? extractBits32(tmpins.origin32bit, 31, 27)
                                : extractBits32(tmpins.origin32bit, 11, 7);
                tmpins.d = extractBits32(tmpins.origin32bit, 11, 7);
                if (WILLregext)
                {
                    tmpins.s1 += ext1 << 5;
                    tmpins.s2 += ext2 << 5;
                    tmpins.s3 += ext3 << 5;
                    tmpins.d += extd << 5;
                    WILLregext = false;
                }
                scinsbit = tmpins.origin32bit;
                switch (tmpins.ddd.sel_imm)
                {
                case DecodeParams::IMM_I:
                    tmpins.imm = scinsbit.range(31, 20).to_int(); // to_int()会自动补符号位，to_uint()补0
                    break;
                case DecodeParams::IMM_S:
                    tmpins.imm = (scinsbit.range(31, 25), scinsbit.range(11, 7)).to_int();
                    break;
                case DecodeParams::IMM_B:
                    tmpins.imm = (scinsbit.range(31, 31), scinsbit.range(7, 7), scinsbit.range(30, 25), scinsbit.range(11, 8)).to_int() << 1;
                    break;
                case DecodeParams::IMM_U:
                    tmpins.imm = (scinsbit.range(31, 12)).to_int() << 12;
                    break;
                case DecodeParams::IMM_J:
                    tmpins.imm = (scinsbit.range(31, 31), scinsbit.range(19, 12), scinsbit.range(20, 20), scinsbit.range(30, 21)).to_int() << 1;
                    break;
                case DecodeParams::IMM_Z:
                    tmpins.imm = (scinsbit.range(19, 15)).to_uint();
                    break;
                case DecodeParams::IMM_2:
                    tmpins.imm = (scinsbit.range(24, 20)).to_int();
                    break;
                case DecodeParams::IMM_V: // 和scala不一样，需要修改，加位拓展
                    tmpins.imm = (scinsbit.range(19, 15)).to_int();
                    break;
                case DecodeParams::IMM_L11:
                    tmpins.imm = (scinsbit.range(30, 20)).to_int();
                    break;
                case DecodeParams::IMM_S11:
                    tmpins.imm = (scinsbit.range(30, 25), scinsbit.range(11, 7)).to_int();
                    break;
                default:
                    break;
                }

                WARPS[warp_id]->decode_ins = tmpins;
            }
        }
    }
}
