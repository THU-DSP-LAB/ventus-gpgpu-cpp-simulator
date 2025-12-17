#ifndef BASE_H_
#define BASE_H_

#include "../parameters.h"
#include "physical_mem.hpp"
#include "sv39.hpp"
#include "subcore.hpp"
#include <array>
#include <bitset>
#include <functional>
#include <memory>
#include <queue>
#define SC_INCLUDE_DYNAMIC_PROCESSES
#include <systemc.h>
#include "../cache/interfaces.h"
#include "L1D_Cache_System.hpp"
class kernel_info_t;
class CTA_Scheduler;

class BASE : public sc_core::sc_module {
public:
    const int sm_id;
    sc_in_clk clk { "clk" };
    sc_in<bool> rst_n { "rst_n" };
    std::shared_ptr<spdlog::logger> m_logger;

    // Memory Access: global & local(shared)
    SV39_basic m_mmu;
    std::array<uint8_t, hw_lds_size> m_local_mem; // SharedMem/LDS of this SM
    int sharedMem_request(const std::unique_ptr<lsu_mem_cmd_t>& cmd);

    // DDR interface
    // using mem_interface_t = std::function<
    //     int(std::unique_ptr<lsu_mem_cmd_t>& cmd,
    //         std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> callback)>;
    // mem_interface_t m_l1d_cache_accept;

    std::array<std::unique_ptr<Subcore>, SUBCORE_NUM> m_subcores;

    void lsu_main();
    void lsu_l1d_read_callback(std::unique_ptr<lsu_mem_cmd_t> cmd);
    void lsu_l1d_write_callback(std::unique_ptr<lsu_mem_cmd_t> cmd);
    void lsu_new_req();

    BASE(
        sc_core::sc_module_name name, int _sm_id,
        const std::shared_ptr<const std::vector<instable_t>>& instruction_table,
        const std::shared_ptr<const std::map<OP_TYPE, decodedat>>& decode_table,
        std::shared_ptr<PhysicalMemoryInterface> gmem, 
        std::shared_ptr<spdlog::logger> logger = nullptr
    );

public:
    // shared components of subcore's warp_scheduler
    typedef struct {  // hardware block slot in SM, records block(CTA) information
        bool valid;   // a block is running on this slot
        int num_warp; // number of warps that are currently running on this SM (redundant, can be
                      // inferred from hw_warp_running)
        std::array<bool, hw_num_warp> hw_warp_running; // warps of this block are running on these
                                                       // hardware-warps (hw_warp_idx)
        std::array<bool, hw_num_warp>
            warp_reach_barrier; // these warps have reached barrier (software_warp_idx in block)
        uint32_t barrier_addr;  // barrier pc, for debug assert
    } block_slot_t;
    std::array<block_slot_t, MAX_CTA_PER_CORE> m_block_slots;

    // @brief: access block_slot when warp reaches barrier
    // @param: subcore_id: the requesting subcore id
    // @param: subcore_warp_id: the requesting hardware warp id in subcore
    // @param: blk_slot_id: which block slot does this warp belong to
    // @param: warp_id_in_blk: software warp id in block
    // @param: pc: the pc address that reaches barrier
    void warp_reach_barrier(
        int subcore_id, int subcore_warp_id, int blk_slot_id, int warp_id_in_blk, vaddr_t pc
    );

    // @brief: a warp finished execution, clear the block_slot, back to CTA scheduler
    // @param: subcore_id: the requesting subcore id
    // @param: subcore_warp_id: the requesting hardware warp id in subcore
    // @param: blk_slot_id: which block slot does this warp belong to
    // @param: warp_id_in_blk: software warp id in block
    void warp_endprg(int subcore_id, int subcore_warp_id, int blk_slot_id, int warp_id_in_blk);

    //
    // exec
    //

    // lsu
    std::bitset<SUBCORE_NUM> lsu_subcore_req_valid;
    uint8_t lsu_subcore_req_arbiter_last = 0; // round-robin arbiter
    std::bitset<SUBCORE_NUM> lsu_subcore_req_updated;
    sc_event ev_lsu_subcore_req_all_updated;
    // 指令成功发射到lsu则返回0并将data unique_ptr转移，否则返回-1
    int lsu_subcore_req(
        bool valid, uint32_t subcore_id, uint32_t subcore_warp_id, I_TYPE instr, vaddr_t pds_base,
        paddr_t pagetable_root, std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data1,
        std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data2,
        std::unique_ptr<std::array<reg_t, hw_num_thread>>& src_data3
    );
    struct lsu_subcore_req_t {
        uint32_t subcore_id;
        uint32_t subcore_warp_id;
        I_TYPE instr;
        vaddr_t pds_base;
        paddr_t pagetable_root;
        std::unique_ptr<std::array<reg_t, hw_num_thread>> src_data1;
        std::unique_ptr<std::array<reg_t, hw_num_thread>> src_data2;
        std::unique_ptr<std::array<reg_t, hw_num_thread>> src_data3;
    };
    std::queue<lsu_subcore_req_t> m_lsu_subcore_req_queue;
    sc_vector<sc_signal<bool>> emito_lsu { "emito_lsu", SUBCORE_NUM };

    typedef struct lsu_mshr_t {
        bool valid;
        uint8_t delay; // 额外的延迟周期数
        uint8_t warp_id;
        I_TYPE instr;
        std::shared_ptr<const std::array<uint8_t, hw_num_thread>> wordOffset1H;
        sc_bv<hw_num_thread> finished_mask;
        std::unique_ptr<std::array<reg_t, hw_num_thread>> data; // load instr's writeback data
        std::shared_ptr<const std::array<uint32_t, hw_num_thread>> addr; // for debug
    } lsu_mshr_t;
    std::array<lsu_mshr_t, LSU_MSHR_SIZE> m_lsu_mshr;
    std::queue<std::unique_ptr<lsu_mem_cmd_t>> m_lsu_mem_cmd_queue;
    L1D_Cache_System* m_l1d_cache = nullptr;
    // warp scheduler barrier

    // CTA Scheduler interface
    uint32_t m_last_activated_hardware_warp_id = 0; // for round-robin
    void receive_warp(
        uint32_t blk_idx_in_kernel, uint32_t warp_idx_in_blk, std::shared_ptr<kernel_info_t> kernel,
        uint32_t blk_slot_idx, uint32_t lds_baseaddr
    );
    std::function<void(int sm_id, int blk_slot_idx, int warp_idx_in_blk)>
        m_warp_finish_callback; // warp执行完毕后回调通知CTA Scheduler
};

#endif
