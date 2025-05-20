#pragma once

#include "../parameters.h"
#include "sv39.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <spdlog/logger.h>
#include <systemc.h>

// warp ID conversion: [subcore_id, warp_id_in_subcore] <-> warp_id_in_core
uint32_t warpid_convert(uint32_t subcore_id, uint32_t subcore_warp_id);
std::pair<uint32_t, uint32_t> warpid_convert(uint32_t warp_id);

class kernel_info_t;

class Subcore : public sc_core::sc_module {
public:
    static constexpr uint8_t num_warp = SUBCORE_WARP_NUM;
    const uint32_t m_sm_id;
    const uint32_t m_subcore_id;
    sc_in_clk clk { "clk" };
    sc_in<bool> rst_n { "rst_n" };

    using lsu_req_interface = std::function<
        int(bool valid, uint32_t subcore_id, uint32_t subcore_warp_id, I_TYPE instr, vaddr_t pds_base,
            paddr_t pagetable_root, std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data1,
            std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data2,
            std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data3)>;
    using warp_barrier_req_interface
        = std::function<void(int subcore_warp_id, int blk_slot_id, int warp_id_in_blk, vaddr_t pc)>;
    using warp_endprg_interface
        = std::function<void(int subcore_warp_id, int blk_slot_id, int warp_id_in_blk)>;
    void warp_barrier_set(int subcore_warp_id, bool val);

    Subcore(
        sc_core::sc_module_name name, uint32_t sm_id, uint32_t subcore_id,
        const std::shared_ptr<const std::vector<instable_t>>& instruction_table,
        const std::shared_ptr<const std::map<OP_TYPE, decodedat>>& decode_table,
        const lsu_req_interface& lsu_subcore_req,
        const warp_barrier_req_interface& warp_barrier_req,
        const warp_endprg_interface& warp_endprg, const SV39_basic& mmu,
        std::shared_ptr<spdlog::logger> logger = nullptr
    );

private:
    std::shared_ptr<spdlog::logger> m_logger;
    SV39_basic m_mmu;

    //
    // decode table
    //

    // from 32bit-instr to operation
    const std::shared_ptr<const std::vector<instable_t>> m_instruction_table;
    // from operation to decode data
    const std::shared_ptr<const std::map<OP_TYPE, decodedat>> m_decode_table;

    //
    // Internal module actions
    //

    // fetch
    void PROGRAM_COUNTER(int warp_id);
    void INSTRUCTION_REG(int warp_id);
    void DECODE(int warp_id);
    // ibuffer
    void cycle_IBUF_ACTION(int warp_id, I_TYPE& dispatch_ins_, I_TYPE& _readdata3);
    void IBUF_PARAM(int warp_id);
    // scoreboard
    void cycle_UPDATE_SCORE(
        int warp_id, I_TYPE& tmpins, std::set<SCORE_TYPE>::iterator& it, REG_TYPE& regtype_,
        bool& insertscore
    );
    void JUDGE_DISPATCH(int warp_id);
    bool cycle_JUDGE_DISPATCH(int warp_id);
    void BEFORE_DISPATCH(int warp_id);
    // issue
    void WARP_SCHEDULER();
    // opc
    void OPC_FIFO();
    void OPC_FETCH();
    void OPC_EMIT();
    bank_t bank_decode(int warp_id, int srcaddr);
    warpaddr_t bank_undecode(int bank_id, int addr);

    // regfile
    std::pair<int, int> reg_arbiter(
        const std::array<std::array<bank_t, 3>, OPCFIFO_SIZE>& addr_arr, // opc_srcaddr
        const std::array<std::array<bool, 3>, OPCFIFO_SIZE>& valid_arr,  // opc_valid
        std::array<std::array<bool, 3>, OPCFIFO_SIZE>& ready_arr,        // opc_ready
        int bank_id, std::array<int, BANK_NUM>& REGcurrentIdx,
        std::array<int, BANK_NUM>& read_bank_addr
    );
    void READ_REG();
    void WRITE_REG(int warp_id);
    // exec
    void SALU_IN();
    void SALU_CALC();
    void SALU_CTRL();
    void VALU_IN();
    void VALU_CALC();
    void VALU_CTRL();
    void VFPU_IN();
    void VFPU_CALC();
    void VFPU_CTRL();
    void SIMT_STACK(int warp_id);
    void CSR_IN();
    void CSR_CALC();
    void CSR_CTRL();
    void MUL_IN();
    void MUL_CALC();
    void MUL_CTRL();
    void SFU_IN();
    void SFU_CALC();
    void SFU_CTRL();
    void TC_IN();
    void TC_CALC();
    void TC_CTRL();
    // writeback
    void WRITE_BACK();

    static void exec_calc_helper(
        const I_TYPE& ins, int num_thread_active, const std::array<iuf32_t, hw_num_thread>& src1,
        const std::array<iuf32_t, hw_num_thread>& src2,
        const std::array<iuf32_t, hw_num_thread>& src3, std::array<iuf32_t, hw_num_thread>& dst,
        std::function<iuf32_t(iuf32_t op1, iuf32_t op2, iuf32_t op3)> calc
    );

    // initialize
    void hardware_warps_reset() {
        for (auto& warp_ : m_hw_warps) {
            warp_->pc = -1;
            warp_->ibuftop_ins = I_TYPE(INVALID_, 0, 0, 0);
        }
        issue_ins = I_TYPE(INVALID_, 0, 0, 0);
    }

    //
    // Frontend components and signals
    //

    // hardware warp (slots)
    std::array<std::unique_ptr<WARP_BONE>, SUBCORE_WARP_NUM> m_hw_warps;

    // issue
    sc_event_and_list ev_warp_dispatch_list;
    sc_signal<I_TYPE> issue_ins { "issue_ins" };
    sc_signal<int> issueins_warpid { "issueins_warpid" };
    // ↓需为sc_signal，否则dispatch判断对i的循环边界i < last_dispatch_warpid + hw_num_warp会变化
    sc_signal<int> last_dispatch_warpid { "last_dispatch_warpid" };
    sc_signal<bool> dispatch_valid { "dispatch_valid" };

    // warp scheduler
    sc_event ev_warp_assigned;
    // opc
    sc_signal<int> last_emit_entryid { "last_emit_entryid" };
    sc_event ev_opc_pop, ev_regfile_readdata, ev_opc_judge_emit, ev_opc_store, ev_opc_collect;
    StaticEntry<opcfifo_t, OPCFIFO_SIZE> opcfifo;
    std::array<std::array<bool, 3>, OPCFIFO_SIZE> opc_valid;
    std::array<std::array<bool, 3>, OPCFIFO_SIZE> opc_ready;
    std::array<std::array<bank_t, 3>, OPCFIFO_SIZE> opc_srcaddr;
    std::array<std::array<bool, 3>, OPCFIFO_SIZE> opc_banktype; // 0-s, 1-v
    std::array<int, BANK_NUM> read_bank_addr;                   // regfile arbiter给出
    std::array<int, BANK_NUM> REGcurrentIdx;                    // OPC轮询到哪了
    std::array<std::array<reg_t, hw_num_thread>, BANK_NUM> read_data;
    // ↓轮询选出哪个了（索引，有这个数据，ready其实没有用了）
    std::array<std::pair<int, int>, BANK_NUM> REGselectIdx;
    sc_signal<int> emit_idx { "emit_idx" }; // 上一周期emit的ins在opc中的索引，最大是BANK_NUM
    sc_signal<bool> opc_full { "opc_full" };
    bool opc_empty;
    sc_signal<I_TYPE> emit_ins { "emit_ins" };
    sc_signal<int> emitins_warpid { "emitins_warpid" };
    sc_signal<int> opcfifo_elem_num { "opcfifo_elem_num" };
    bool findemit; // 轮询时，找到了全ready且执行单元也ready的entry
    sc_signal<bool> doemit { "doemit" };
    // regfile
    sc_signal<int> rdv1_addr { "rdv1_addr" };
    // sc_signal<reg_t> rds1_data { "rds1_data" };
    sc_vector<sc_signal<reg_t>> rdv1_data { "rdv1_data", hw_num_thread };

    //
    // Backend(exec) components and signals
    //

    // lsu interface (shared by multiple subcores)
    lsu_req_interface f_lsu_subcore_req;
    sc_event ev_lsufifo_pushed;
    std::queue<lsu_out_t> lsufifo;
    bool lsufifo_empty;
    int lsufifo_elem_num;
    sc_signal<bool> execpop_lsu { "execpop_lsu" };

public:
    void lsu_writeback(
        const I_TYPE& instr, int subcore_warp_id,
        std::unique_ptr<std::array<reg_t, hw_num_thread>> data
    );
    void lsu_writeback_event() { ev_lsufifo_pushed.notify(); };

private:
    // salu
    sc_signal<bool> emito_salu { "emito_salu" };
    sc_signal<reg_t> tosalu_data1 { "tosalu_data1" }, tosalu_data2 { "tosalu_data2" },
        tosalu_data3 { "tosalu_data3" }; // OPC TO SALU
    bool salu_ready;
    sc_signal<bool> salu_ready_old { "salu_ready_old" };
    sc_event_queue salu_eqa, salu_eqb; // 分别负责a time和b time，最后一个是SALU_IN的，优先级比eqb低
    sc_event salu_eva, salu_evb, salu_unready, salu_nothinghappen, ev_salufifo_pushed,
        ev_saluready_updated;
    std::queue<salu_in_t> salu_dq;
    StaticQueue<salu_out_t, 3> salufifo;
    salu_out_t salutop_dat;
    bool salufifo_empty, salufifo_push;
    int salufifo_elem_num;
    salu_in_t salutmp1;
    salu_out_t salutmp2;
    // ↓例如eqa_triggered，仅在eqa被触发时，delta 0变为1，delta 1给SALU_IN看，同时又变回0
    sc_signal<bool> salueqa_triggered { "salueqa_triggered" },
        salueqb_triggered { "salueqb_triggered" };
    sc_signal<bool> execpop_salu { "execpop_salu" };

    // valu
    sc_signal<bool> emito_valu { "emito_valu" };
    sc_vector<sc_signal<reg_t>> tovalu_data1 { "tovalu_data1", hw_num_thread }, // OPC TO VALU
        tovalu_data2 { "tovalu_data2", hw_num_thread },
        tovalu_data3 { "tovalu_data3", hw_num_thread };
    bool valu_ready;
    sc_signal<bool> valu_ready_old { "Valu_ready_old" };
    sc_event_queue valu_eqa, valu_eqb;
    sc_event valu_eva, valu_evb, valu_unready, valu_nothinghappen, ev_valufifo_pushed,
        ev_valuready_updated;
    std::queue<valu_in_t> valu_dq;
    StaticQueue<valu_out_t, 3> valufifo;
    valu_out_t valutop_dat;
    bool valufifo_empty, valufifo_push;
    int valufifo_elem_num;
    sc_signal<bool> valueqa_triggered { "valueqa_triggered" },
        valueqb_triggered { "valueqb_triggered" };
    sc_signal<bool> execpop_valu { "execpop_valu" };

    // vfpu
    sc_signal<bool> emito_vfpu { "emito_vfpu" };
    sc_vector<sc_signal<int32_t>> tovfpu_data1 { "tovfpu_data1", hw_num_thread }, // OPC TO VFPU
        tovfpu_data2 { "tovfpu_data2", hw_num_thread },
        tovfpu_data3 { "tovfpu_data3", hw_num_thread };
    bool vfpu_ready;
    sc_signal<bool> vfpu_ready_old { "vfpu_ready_old" };
    sc_event_queue vfpu_eqa, vfpu_eqb;
    sc_event vfpu_eva, vfpu_evb, vfpu_unready, vfpu_nothinghappen, ev_vfpufifo_pushed,
        ev_vfpuready_updated;
    std::queue<vfpu_in_t> vfpu_dq;
    StaticQueue<vfpu_out_t, 3> vfpufifo;
    vfpu_out_t vfputop_dat;
    bool vfpufifo_empty, vfpufifo_push;
    int vfpufifo_elem_num;
    sc_signal<bool> vfpueqa_triggered { "vfpueqa_triggered" },
        vfpueqb_triggered { "vfpueqb_triggered" };
    sc_signal<bool> execpop_vfpu { "execpop_vfpu" };

    // simt stack
    // ↓由于wait_bran的存在，这两个不会同时为1
    sc_signal<bool> emito_simtstk { "emito_simtstk" };                    // 对应join
    sc_signal<bool, SC_MANY_WRITERS> valuto_simtstk { "valuto_simtstk" }; // 对应beq类
    simtstack_t simtstk_newelem;                                          // from VALU to SIMT-stack
    sc_signal<int> simtstk_new_warpid { "simtstk_new_warpid" };           // newelem对应的warp
    sc_signal<sc_bv<hw_num_thread>> branch_elsemask {
        "branch_elsemask"
    }; // VALU计算出的elsemask，将发给SIMT-stack，elsemask为1表示判断跳转
    sc_signal<sc_bv<hw_num_thread>> branch_ifmask { "branch_ifmask" }; // 与elsemask相反
    sc_signal<uint32_t> branch_elsepc { "branch_elsepc" }; // VALU处理分支跳转的else分支pc
    sc_signal<I_TYPE> vbranch_ins { "vbranch_ins" };
    sc_signal<int> vbranchins_warpid { "vbranchins_warpid" };

    // csr
    sc_signal<bool> emito_csr { "emito_csr" };
    sc_signal<int32_t> tocsr_data1 { "tocsr_data1" }, tocsr_data2 { "tocsr_data2" }; // OPC TO CSR
    bool csr_ready;
    sc_signal<bool> csr_ready_old { "csr_ready_old" };
    sc_event_queue csr_eqa, csr_eqb;
    sc_event csr_eva, csr_evb, csr_unready, csr_nothinghappen, ev_csrfifo_pushed,
        ev_csrready_updated;
    std::queue<csr_in_t> csr_dq;
    StaticQueue<csr_out_t, 10> csrfifo;
    csr_out_t csrtop_dat;
    bool csrfifo_empty;
    int csrfifo_elem_num;
    sc_signal<bool> csreqa_triggered { "csreqa_triggered" },
        csreqb_triggered { "csreqb_triggered" };
    sc_signal<bool> execpop_csr { "execpop_csr" };

    // mul
    sc_signal<bool> emito_mul { "emito_mul" };
    sc_vector<sc_signal<reg_t>> tomul_data1 { "tomul_data1", hw_num_thread },
        tomul_data2 { "tomul_data2", hw_num_thread }, tomul_data3 { "tomul_data3", hw_num_thread };
    bool mul_ready;
    sc_signal<bool> mul_ready_old { "mul_ready_old" };
    sc_event_queue mul_eqa, mul_eqb;
    sc_event mul_eva, mul_evb, mul_unready, mul_nothinghappen, ev_mulfifo_pushed,
        ev_mulready_updated;
    std::queue<mul_in_t> mul_dq;
    StaticQueue<mul_out_t, 3> mulfifo;
    mul_out_t multop_dat;
    bool mulfifo_empty, mulfifo_push;
    int mulfifo_elem_num;
    sc_signal<bool> muleqa_triggered { "muleqa_triggered" },
        muleqb_triggered { "muleqb_triggered" };
    sc_signal<bool> execpop_mul { "execpop_mul" };

    // sfu
    sc_signal<bool> emito_sfu { "emito_sfu" };
    sc_vector<sc_signal<reg_t>> tosfu_data1 { "tosfu_data1", hw_num_thread },
        tosfu_data2 { "tosfu_data2", hw_num_thread };
    bool sfu_ready;
    sc_signal<bool> sfu_ready_old { "sfu_ready_old" };
    sc_event_queue sfu_eqa, sfu_eqb;
    sc_event sfu_eva, sfu_evb, sfu_unready, sfu_nothinghappen, ev_sfufifo_pushed,
        ev_sfuready_updated;
    std::queue<sfu_in_t> sfu_dq;
    StaticQueue<sfu_out_t, 3> sfufifo;
    sfu_out_t sfutop_dat;
    bool sfufifo_empty, sfufifo_push;
    int sfufifo_elem_num;
    sc_signal<bool> sfueqa_triggered { "sfueqa_triggered" },
        sfueqb_triggered { "sfueqb_triggered" };
    sc_signal<bool> execpop_sfu { "execpop_sfu" };

    // tc
    sc_signal<bool> emito_tc { "emito_tc" };
    sc_vector<sc_signal<int32_t>> totc_data1 { "totc_data1", hw_num_thread }, // OPC TO TC
        totc_data2 { "totc_data2", hw_num_thread }, totc_data3 { "totc_data3", hw_num_thread };
    bool tc_ready;
    sc_signal<bool> tc_ready_old { "tc_ready_old" };
    sc_event_queue tc_eqa, tc_eqb;
    sc_event tc_eva, tc_evb, tc_unready, tc_nothinghappen, ev_tcfifo_pushed, ev_tcready_updated;
    std::queue<tc_in_t> tc_dq;
    StaticQueue<tc_out_t, 3> tcfifo;
    tc_out_t tctop_dat;
    bool tcfifo_empty, tcfifo_push;
    int tcfifo_elem_num;
    sc_signal<bool> tceqa_triggered { "tceqa_triggered" }, tceqb_triggered { "tceqb_triggered" };
    sc_signal<bool> execpop_tc { "execpop_tc" };

    // warp_scheduler exec part (barrier & endprg)
    std::array<bool, SUBCORE_WARP_NUM> wait_barrier; // warp触及barrier正在等待
    sc_signal<bool> emito_warpscheduler { "emito_wrpschdler" };
    warp_barrier_req_interface f_warp_barrier_req;
    warp_endprg_interface f_warp_endprg;
    void warp_barrier_release(uint8_t subcore_warp_id);

    // writeback
    sc_signal<bool> write_s { "write_s" }, write_v { "write_v" }, write_f { "write_f" };
    sc_signal<I_TYPE> wb_ins { "wb_ins" };
    sc_signal<int> wb_warpid { "wb_warpid" };
    sc_signal<bool> wb_ena { "wb_ena" };

    // debug，没实际用处
    sc_signal<bool> dispatch_ready { "dispatch_ready" };

public:
    bool is_warp_idle(int subcore_warp_id) const {
        return !m_hw_warps.at(subcore_warp_id)->is_warp_activated
            && !m_hw_warps.at(subcore_warp_id)->will_warp_activate;
    }
    void receive_warp(
        uint32_t blk_idx_in_kernel, uint32_t warp_idx_in_blk, std::shared_ptr<kernel_info_t> kernel,
        uint32_t lds_baseaddr, uint32_t blk_slot_idx, uint32_t subcore_warp_idx
    );

    // receive warp from CTA scheduler
};