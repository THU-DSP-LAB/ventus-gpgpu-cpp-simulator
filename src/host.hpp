#pragma once
#include <memory>
#include <systemc.h>
#include <vector>

class task_t;
class kernel_info_t;
class CTA_Scheduler;
class Memory;

class Host : public sc_core::sc_module {
public:
    sc_in_clk clk{"clk"};
    sc_in<bool> rst_n{"rst_n"};

    Host(sc_core::sc_module_name name, Memory *mem, CTA_Scheduler *cta);
    void add_task(std::shared_ptr<task_t>);
    void add_kernel(std::shared_ptr<kernel_info_t>);

private:
    void mainThread();

    typedef struct {
        std::shared_ptr<task_t> ptr;
        bool dispatched;
        bool finished;
    } task_wrapper_t;
    typedef struct {
        std::shared_ptr<kernel_info_t> ptr;
        bool dispatched;
    } kernel_wrapper_t;
    std::vector<task_wrapper_t> m_tasks;
    std::vector<kernel_wrapper_t> m_kernels;      // 无上级task的零散kernel

    CTA_Scheduler *m_cta;
    Memory *m_mem;
};

