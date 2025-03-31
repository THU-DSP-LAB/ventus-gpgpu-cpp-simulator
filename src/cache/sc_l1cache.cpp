#include "sc_l1cache.hpp"

SC_L1_CACHE::SC_L1_CACHE(sc_core::sc_module_name name)
    : sc_module(name), dcache(l1_data_cache()), time(0),
      LSU_2_dcache_coreReq_port("LSU_2_dcache_coreReq_port"),
      dcache_2_LSU_coreRsp_port("dcache_2_LSU_coreRsp_port")
{
    SC_HAS_PROCESS(SC_L1_CACHE);

    SC_THREAD(l1_d_cycle);
}

void SC_L1_CACHE::l1_d_cycle() {
    while (true) {
        wait(clk.posedge_event());

        if(LSU_2_dcache_coreReq_port.num_available() != 0 && !dcache.m_coreReq.is_valid()) {
            dcache.m_coreReq.update_with(LSU_2_dcache_coreReq_port.read());
        }

        dcache.cycle(time);

        if(!dcache.m_coreRsp_Q.m_Q.empty() && dcache_2_LSU_coreRsp_port.num_free() != 0) {
            dcache_2_LSU_coreRsp_port.write(dcache.m_coreRsp_Q.m_Q.front());
            dcache.m_coreRsp_Q.m_Q.pop_front();
        }

        time++;
    }
}
