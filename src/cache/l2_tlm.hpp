#ifndef L2_TLM_H_
#define L2_TLM_H_

#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h> 
#include <cstdio>

#include "physical_mem.hpp"
#include "l1_tlm_adapter.hpp"
#include "sv39.hpp"
#include <spdlog/spdlog.h>

class RamulatorWrapper;
class L2_Cache : public sc_core::sc_module
{
  public:
    // TLM target socket
    static constexpr unsigned NL1 = 32;  // 假设最多 4 路 L1
    tlm_utils::multi_passthrough_target_socket<L2_Cache, 32> target_socket;

    L2_Cache(sc_core::sc_module_name name, std::shared_ptr<PhysicalMemoryInterface> pmem) 
        : target_socket("target_socket"), m_mem(pmem), m_mmu(std::make_unique<SV39_basic>(pmem, nullptr))
    {
        SC_HAS_PROCESS(L2_Cache);

        // 注册非阻塞 forward 回调
        target_socket.register_nb_transport_fw(this, &L2_Cache::nb_transport_fw);

        // 用一个线程去处理队列中的事务
        SC_THREAD(process_queue);
    }
    RamulatorWrapper* ramulator = nullptr;
    void bind_ramulator(RamulatorWrapper* wrapper) { ramulator = wrapper; }
  private:
    // 记录正在等待处理的请求
    struct req { unsigned socket_id; tlm::tlm_generic_payload* trans; };
    std::deque<req> req_queue;
    // PhysicalMemoryInterface *m_mem;
    std::shared_ptr<PhysicalMemoryInterface> m_mem;
    // MMU 用于虚拟地址到物理地址的翻译
    std::unique_ptr<SV39_basic> m_mmu;

    // forward path 回调：收到 L1_TLM_Adapter 发起的 nb_transport_fw
    tlm::tlm_sync_enum nb_transport_fw(int socket_id, tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase, sc_core::sc_time &delay);

    std::optional<std::pair<uint32_t, int>> lr_reservation;
    sc_core::sc_mutex amo_lock;
    // 线程：模拟 L2 行为(多周期、hit/miss、等待等)
    void process_queue();
};
#endif
