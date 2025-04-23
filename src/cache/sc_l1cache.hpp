#ifndef SC_L1CACHE_H_
#define SC_L1CACHE_H_

#include "l1_data_cache.h"
#include <systemc.h>
#include <iostream>
#include "../parameters.h"

class SC_L1_CACHE : public sc_core::sc_module {
public:
    sc_in_clk clk {"clk"};

    // dcache <-> core pipeline, with valid-ready handshake (use sc_fifo to work as handshake)
    sc_fifo_in<LSU_2_dcache_coreReq>  LSU_2_dcache_coreReq_port;
    sc_fifo_out<dcache_2_LSU_coreRsp> dcache_2_LSU_coreRsp_port;  // need to be initilized outside
    
    // dcache <-> L2 port
    sc_fifo_in<L2_2_dcache_memRsp>    L2_2_dcache_memRsp_port; // handshake is not needed - memRsp correspond to previous memReq
    sc_fifo_out<dcache_2_L2_memReq>   dcache_2_L2_memReq_port;

    void l1_d_cycle();

    // constructor
    SC_L1_CACHE(sc_core::sc_module_name name);

private:
    cycle_t time;
    l1_data_cache dcache;

};

#endif