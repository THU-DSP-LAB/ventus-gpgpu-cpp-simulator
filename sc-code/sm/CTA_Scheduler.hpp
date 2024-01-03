#ifndef CTA_SCHEDULER_H_
#define CTA_SCHEDULER_H_

#include <iostream>
#include <fstream>
#include <vector>
#include "BASE.h"
#include "../context_model.hpp"
#include "../utils.hpp"
#include "../gpgpu_model.hpp"

class BASE;

class CTA_Scheduler : public sc_core::sc_module
{
public:
    sc_in_clk clk{"clk"};
    sc_vector<sc_port<IO_out_if<int>>> outs;

public:
    CTA_Scheduler(sc_core::sc_module_name name) : sc_module(name)
    {
        SC_HAS_PROCESS(CTA_Scheduler);
        SC_THREAD(schedule_kernel2core);
    }
    void CTA_INIT();
    void MONITOR_WARP_RET();

public:
    sc_event ev_activate_warp;

    void readHexFile(const std::string &filename, int itemSize, std::vector<uint64_t> &items);
    void assignMetadata(const std::vector<uint64_t> &metadata, meta_data_t &mtd);
    void activate_warp();
    void schedule_kernel2core();
    void set_running_kernels(std::vector<std::shared_ptr<kernel_info_t>> &_kernels) { m_running_kernels = _kernels; }
    std::shared_ptr<kernel_info_t> select_kernel();

    BASE **sm_group;
    meta_data_t mtd;

private:
    bool isHexCharacter(char c);
    int charToHex(char c);
    void freeMetadata(meta_data_t &mtd);

    sc_event_or_list all_warp_ev_kernel_ret;

    std::vector<std::shared_ptr<kernel_info_t>> m_running_kernels;
    std::vector<std::shared_ptr<kernel_info_t>> m_executed_kernels; // kernel第一次被issue，加入executed列表
    uint32_t m_last_issued_kernel = 0;
    uint32_t m_last_issue_core = 0;
};

#endif