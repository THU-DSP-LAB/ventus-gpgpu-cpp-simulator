#pragma once

#include "../parameters.h"
#include "icache.hpp"
#include "sv39.hpp"
#include "sysc/communication/sc_writer_policy.h"
#include <array>
#include <bit>
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

    using lsu_req_interface = std::function<int(
        bool valid, uint32_t subcore_id, uint32_t subcore_warp_id, I_TYPE instr, vaddr_t pds_base,
        paddr_t pagetable_root, std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data1,
        std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data2,
        std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data3
    )>;
    using l1icache_request_interface
        = std::function<void(paddr_t ptroot, vaddr_t addr, int warpid)>;
    using l1icache_flushpipe_interface = std::function<void(int warpid)>;
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
        const warp_endprg_interface& warp_endprg,
        const l1icache_request_interface& l1icache_request,
        const l1icache_flushpipe_interface& l1icache_flushpipe, const SV39_basic& mmu,
        std::shared_ptr<spdlog::logger> logger = nullptr
    );

    void export_vcd_trace(sc_core::sc_trace_file* tf, const std::string& prefix) const;

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
    void icache_access(); // sc_thread pipeline stage fetch
    void icache_wait();   // sc_thread pipeline stage fetch2
    void DECODE();
    // ibuffer
    void cycle_IBUF_ACTION(int warp_id);
    void IBUF_PARAM(int warp_id);
    // scoreboard
    void cycle_UPDATE_SCORE(int warp_id);
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
        auto invalid_instr = std::make_shared<I_TYPE>(INVALID_, 0, 0, 0);
        for (auto& warp_ : m_hw_warps) {
            warp_->pc = -1;
            warp_->ibuftop_ins = invalid_instr;
        }
        issue_ins = *invalid_instr;
    }

    //
    // Frontend components and signals
    //

    // hardware warp (slots)
    std::array<std::unique_ptr<WARP_BONE>, SUBCORE_WARP_NUM> m_hw_warps;

    // fetch
    // pc in m_hw_warp, not here
    // L0 icache in subcore
    inline constexpr static unsigned l0icache_line_size
        = L1D_BLOCK_NUM_WORD * sizeof(uint32_t); // bytes
    struct l0icache_line_t {
        bool valid;
        paddr_t pagetable_root;
        vaddr_t addr_base;
        l0icache_line_t()
            : valid(false) {
            assert(std::popcount(l0icache_line_size) == 1);
        }
    };
    std::array<l0icache_line_t, hw_num_warp> m_l0icache;
    // fetch status for FETCH & FETCH2 pipeline stages
    struct fetch_t {
        vaddr_t pc;
        int warp_id;
        enum FETCH_FROM { NONE, L0ICACHE, L1ICACHE } from;
        bool success;
        friend std::ostream& operator<<(std::ostream& os, const fetch_t& v) {
            auto strmap = std::unordered_map<FETCH_FROM, std::string> {
                { FETCH_FROM::NONE, "NONE" },
                { FETCH_FROM::L0ICACHE, "L0ICACHE" },
                { FETCH_FROM::L1ICACHE, "L1ICACHE" },
            };
            auto str = fmt::format(
                "fetch_t{{pc=0x{:x}, warp_id={}, from={}, success={}}}", v.pc, v.warp_id,
                strmap[v.from], v.success ? "true" : "false"
            );
            os << str;
            return os;
        }
        bool operator==(const fetch_t& that) const = default;
    };
    sc_signal<fetch_t> fetch_reg { "fetch_reg" };   // fetch流水级末的寄存器
    sc_signal<fetch_t> fetch2_reg { "fetch2_reg" }; // fetch2流水级末的寄存器
    sc_signal<uint32_t> fetch2_instr { "fetch2_instr" }; // fetch2流水级末的寄存器，取回的指令
    // l1i rsp notify and pushed into here and consumed in fetch2 stage in the same cycle
    sc_event ev_l1icache_rsp;
    std::queue<ICacheRsp> l1icache_rsp_queue;

    // combinational logic of warp scheduler,
    // determining which warp to fetch from icache this cycle
    int warp_scheduler_fetch_select() const;
    // combinational logic to check if pc needs rewind (icache miss/ibuf full)
    bool pc_need_rewind(int warp_id) const;
    // combinational logic to check if ibuf can accept new instructions this cycle
    bool ibuf_in_ready(int warp_id) const;
    bool ibuf_in_ready(const std::unique_ptr<WARP_BONE>& hwarp) const;
    // combinational logic to check if PC/FETCH/FETCH2 pipeline stages need flush
    // (jump/pc_rewind/endprg)
    bool fetch_need_flush(int warp_id) const;

    // interfaces to/from L0 icache and L1 icache
    int l0icache_access(paddr_t pagetable_root, vaddr_t addr) const;
    l1icache_request_interface f_l1icache_request;
    l1icache_flushpipe_interface f_l1icache_flushpipe;

public:
    void l1icache_response(const ICacheRsp& rsp);

private:
    // decode
    struct decode_t {
        int warp_id;
        std::shared_ptr<I_TYPE> instr; // =nullptr: no valid instruction decoded
        bool operator==(const decode_t& that) const = default;
        friend std::ostream& operator<<(std::ostream& os, const decode_t& v) {
            auto str = (v.instr == nullptr)
                ? fmt::format("decode_t{{null}}")
                : fmt::format("decode_t{{pc=0x{:x}, warp_id={}}}", v.instr->currentpc, v.warp_id);
            os << str;
            return os;
        }
    };
    decode_t decode_output;    // decode结果，组合逻辑输出直接传递给IBUF输入
    sc_event ev_decode_finish; // decode完成事件，通知IBUF可以开始处理输入

    // ibuffer is in WARP_BONE

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
    bool opc_in_ready() const;
    bool opc_in_ready(int warp_id) const;
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
    sc_vector<sc_signal<bool, SC_MANY_WRITERS>> wait_barrier {
        "wait_barrier_subcorewarp", SUBCORE_WARP_NUM
    }; // warp触及barrier正在等待。最后一个到达barrier的warp的线程会解放所有其他线程，因此需要SC_MANY_WRITERS
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
            && !m_hw_warps.at(subcore_warp_id)->endprg_flush_pipe
            && !m_hw_warps.at(subcore_warp_id)->will_warp_activate;
    }
    void receive_warp(
        uint32_t blk_idx_in_kernel, uint32_t warp_idx_in_blk, std::shared_ptr<kernel_info_t> kernel,
        uint32_t lds_baseaddr, uint32_t blk_slot_idx, uint32_t subcore_warp_idx
    );

    // receive warp from CTA scheduler
};