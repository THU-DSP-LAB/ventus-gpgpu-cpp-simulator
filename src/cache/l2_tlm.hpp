#ifndef L2_TLM_H_
#define L2_TLM_H_

#include <tlm_utils/simple_target_socket.h>

#include <deque>
#include <systemc>
#include <tlm>

#include "../membox_sv39/memory.h"
#include "l1_tlm_adapter.hpp"

class L2_Cache : public sc_core::sc_module
{
  public:
    // TLM target socket
    tlm_utils::simple_target_socket<L2_Cache> target_socket;

    L2_Cache(Memory *mem) : target_socket("target_socket"), m_mem(mem)
    {
        SC_HAS_PROCESS(L2_Cache);

        // 注册非阻塞 forward 回调
        target_socket.register_nb_transport_fw(this, &L2_Cache::nb_transport_fw);

        // 用一个线程去处理队列中的事务
        SC_THREAD(process_queue);
    }

  private:
    // 记录正在等待处理的请求
    std::deque<tlm::tlm_generic_payload *> req_queue;

    Memory *m_mem;

    // forward path 回调：收到 L1_TLM_Adapter 发起的 nb_transport_fw
    tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase, sc_core::sc_time &delay);

    // 线程：模拟 L2 行为(多周期、hit/miss、等待等)
    void process_queue();
};

#endif
