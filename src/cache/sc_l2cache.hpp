#ifndef SC_L2CACHE_H_
#define SC_L2CACHE_H_

#include "DEBUG_L2_model.h"
#include <systemc.h>
#include <iostream>

class SC_L2_CACHE : public sc_core::sc_module {
public:
    sc_in_clk clk{"clk"};
    
    // L2交互端口：输入内存请求，输出内存响应
    sc_fifo_in<dcache_2_L2_memReq> dcache_2_L2_memReq_port;
    sc_fifo_out<L2_2_dcache_memRsp> L2_2_dcache_coreRsp_port;

    SC_L2_CACHE::SC_L2_CACHE(sc_core::sc_module_name name)
    : sc_module(name),L2(DEBUG_L2_model()),
      dcache_2_L2_memReq_port("dcache_2_L2_memReq_port"),
      L2_2_dcache_coreRsp_port("L2_2_dcache_coreRsp_port")
{
    SC_HAS_PROCESS(SC_L2_CACHE);
    SC_THREAD(l2_cycle);
    sensitive << clk.pos();
}

    void l2_cycle() {
        while (true) {
            wait();

            // 如果有请求数据到达，并且 L2 内部接收请求的寄存器空闲，则读入请求
            if (dcache_2_L2_memReq_port.num_available() != 0) {
                auto req = dcache_2_L2_memReq_port.read();
                // 这里传入当前时间可以用 sc_time_stamp()，也可转换成需要的单位
                L2.DEBUG_L2_memReq_process(req, sc_time_stamp().to_default_time_units());
            }
            L2.cycle();
            // 如果 L2 模型有响应数据产生，则写入输出端口
            if (!L2.return_Q_is_empty()) {
                auto rsp = L2.DEBUG_serial_pop();
                L2_2_dcache_coreRsp_port.write(rsp);
            }
        }
    }
private:
    DEBUG_L2_model L2;

};

#endif