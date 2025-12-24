#ifndef L1D_CACHE_SYSTEM_HPP
#define L1D_CACHE_SYSTEM_HPP

#include "../cache/interfaces.h"
#include "../cache/l1_tlm_adapter.hpp"
#include "../cache/l2_tlm.hpp"
#include "../cache/sc_l1cache.hpp"
#include "physical_mem.hpp"
#include <array>
#include <systemc>

class L1D_Cache_System : public sc_core::sc_module {
public:
    // 时钟端口
    sc_core::sc_in_clk clk;

    // 构造函数
    L1D_Cache_System(
        sc_core::sc_module_name name, int l1_id, L2_Cache& l2_ref,
        std::shared_ptr<PhysicalMemoryInterface> pmem_ptr,
        std::shared_ptr<spdlog::logger> logger = nullptr
    );

    virtual ~L1D_Cache_System();

    const int m_sm_id;

    // 获取统计信息接口
    int get_l1_hit_count() const;
    int get_l1_miss_count() const;
    // 上层（LSU/SM）通过回调接口与 L1D 交互
    int
    accept(std::unique_ptr<lsu_mem_cmd_t>&, std::function<void(std::unique_ptr<lsu_mem_cmd_t>)>);
    std::map<
        std::pair<int, int>,
        std::pair<
            std::function<void(std::unique_ptr<lsu_mem_cmd_t>)>, std::unique_ptr<lsu_mem_cmd_t>>>
        callback_table;

private:
    // 内部模块
    SC_L1_CACHE* l1;
    L1_TLM_Adapter* adapter;
    sc_core::sc_fifo<LSU_2_dcache_coreReq> internal_fifo_lsu_2_l1d { 32 };
    sc_core::sc_fifo<dcache_2_LSU_coreRsp> internal_fifo_l1d_2_lsu { 32 };
    sc_core::sc_fifo<dcache_2_L2_memReq> fifo_l1_to_adapter { 32 };
    sc_core::sc_fifo<L2_2_dcache_memRsp> fifo_adapter_to_l1 { 32 };
    // 从内部 FIFO 读取 L1D 响应，并通过 callback_table 回调上层
    void forward_rsp();
    // 内存接口引用
    L2_Cache& l2;

    std::shared_ptr<spdlog::logger> m_logger;
};

#endif // L1D_CACHE_SYSTEM_HPP
