// Example:

// int sc_main(int argc, char* argv[])
// {
//     SC_L1_CACHE l1("l1");
//     L1_TLM_Adapter l1_adapter("l1_adapter");
//     L2_Cache l2("l2");

//     // ============= 连接 FIFO (L1 <--> Adapter) =============
//     static sc_core::sc_fifo<dcache_2_L2_memReq>  fifo_l1_to_adapter("fifo_l1_to_adapter", 3);
//     static sc_core::sc_fifo<L2_2_dcache_memRsp> fifo_adapter_to_l1("fifo_adapter_to_l1", 3);

//     // L1 端口 -> Adapter
//     l1.dcache_2_L2_memReq_port(fifo_l1_to_adapter);
//     l1_adapter.memReq_in(fifo_l1_to_adapter);

//     // Adapter -> L1
//     l1_adapter.memRsp_out(fifo_adapter_to_l1);
//     l1.L2_2_dcache_memRsp_port(fifo_adapter_to_l1);

//     // ============= 连接 TLM socket (Adapter <--> L2) ============
//     l1_adapter.initiator_socket.bind(l2.target_socket);

//     sc_core::sc_clock clk("clk", 10, SC_NS);
//     l1.clk(clk);

//     sc_start();
//     return 0;
// }

#ifndef L1_TLM_ADAPTER_H_
#define L1_TLM_ADAPTER_H_

#include <tlm_utils/simple_initiator_socket.h>

#include <map>
#include <tlm>

#include "sc_l1cache.hpp"

struct DCacheMemReqExtension : public tlm::tlm_extension<DCacheMemReqExtension> {
    dcache_2_L2_memReq req;

    // 必须实现 clone 和 copy_from，用于 TLM 在需要时复制 extension
    virtual tlm_extension_base* clone() const override {
        auto* ext = new DCacheMemReqExtension(*this);
        return ext;
    }

    virtual void copy_from(const tlm_extension_base& ext) override {
        const DCacheMemReqExtension& other = static_cast<const DCacheMemReqExtension&>(ext);
        req = other.req;
    }
};
struct L2MemRspExtension : public tlm::tlm_extension<L2MemRspExtension> {
    L2_2_dcache_memRsp rsp;

    // 必须实现 clone() 和 copy_from()，以便 TLM 在需要时复制 extension
    virtual tlm_extension_base* clone() const override {
        auto* ext = new L2MemRspExtension(*this);
        return ext;
    }
    virtual void copy_from(const tlm_extension_base& ext) override {
        auto& other = static_cast<const L2MemRspExtension&>(ext);
        rsp = other.rsp;
    }
};

class L1_TLM_Adapter : public sc_core::sc_module {
public:
    // ============== L1 侧 FIFO 接口 ==============
    sc_core::sc_fifo_in<dcache_2_L2_memReq> memReq_in;
    sc_core::sc_fifo_out<L2_2_dcache_memRsp> memRsp_out;

    // ============= TLM 侧 initiator socket =============
    tlm_utils::simple_initiator_socket<L1_TLM_Adapter> initiator_socket;

    SC_CTOR(L1_TLM_Adapter)
        : initiator_socket("initiator_socket") {
        SC_THREAD(send_req_thread);

        initiator_socket.register_nb_transport_bw(this, &L1_TLM_Adapter::nb_transport_bw);
    }

private:
    std::map<uint32_t, tlm::tlm_generic_payload*> inflight_map;
    uint32_t unique_req_id_counter = 0;
    std::unordered_map<uint32_t, uint32_t> adapter_source_map; // new_key -> original a_source
    // =====================================================
    // 线程：从 FIFO 读 memReq -> 发起 TLM 事务
    // =====================================================
    void send_req_thread();

    // =====================================================
    // 回调：当 L2 调用 nb_transport_bw() 送回响应
    // =====================================================
    tlm::tlm_sync_enum nb_transport_bw(
        tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_core::sc_time& delay
    );
};

#endif