#pragma once

#include "frontend/frontend.h"
#include "memory_system/memory_system.h"
#include "parameters.h"
#include "physical_mem.hpp"
#include "sv39.hpp"
#include <list>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <systemc.h>

class RamulatorWrapper : public sc_core::sc_module {
public:
    RamulatorWrapper(
        const char* config_filename,
        std::shared_ptr<spdlog::logger> logger = spdlog::default_logger()
    );
    sc_in_clk clk { "clock" };
    const bool m_enable_ramulator = true;

    // todo: 当前暂且采用LSU与L1D之间的接口，等将来cache接入后改为L2与DDR之间的接口
    // 只支持读写，不支持flush/invalidate/atomic
    // 读操作完成后会调用callback函数
    // 写操作仅对Ramulator内部的时序仿真有影响，完成后不会调用callback
    int request(
        int sm_id, std::unique_ptr<lsu_mem_cmd_t>& cmd,
        std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> callback
    );

    // 每周期调用这个函数
    void tick();

    std::shared_ptr<PhysicalMemoryInterface> get_memory() const { return m_mem; }

private:
    // ramulator frontend
    std::unique_ptr<Ramulator::IFrontEnd> m_frontend;
    // ramulator memory system (only for timing simulation, no data storage)
    std::unique_ptr<Ramulator::IMemorySystem> m_memorysystem;
    // physical global memory (only for data storage, no timing information)
    std::shared_ptr<PhysicalMemoryInterface> m_mem;

    // mmu地址翻译
    // 当前缺乏L1 cache，本模块暂时相应L1请求，因此将此功能暂放于本模块中
    std::unique_ptr<SV39_basic> m_mmu;

    int m_tick_frontend;
    int m_tick_memorysystem;
    uint64_t m_tick_count = 0;

    struct request_t {
        int sm_id;
        std::unique_ptr<lsu_mem_cmd_t> cmd;
        std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> callback;
    };
    std::list<request_t> m_pending_requests;
    std::shared_ptr<spdlog::logger> m_logger;
};
