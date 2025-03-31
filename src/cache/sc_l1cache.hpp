#ifndef SC_L1CACHE_H_
#define SC_L1CACHE_H_

#include "l1_data_cache.h"
#include <systemc.h>
#include <iostream>

class SC_L1_CACHE : public sc_core::sc_module {
    sc_in_clk clk {"clk"};
    sc_fifo_in<LSU_2_dcache_coreReq>  LSU_2_dcache_coreReq_port;
    sc_fifo_out<dcache_2_LSU_coreRsp> dcache_2_LSU_coreRsp_port;  // need to be initilized outside
    


    void l1_d_cycle();

    // constructor
    SC_L1_CACHE(sc_core::sc_module_name name);

private:
    cycle_t time;
    l1_data_cache dcache;

};

#endif