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
    sc_in_clk clk { "clk" };
    sc_in<bool> rst_n { "rst_n" };

    Host(sc_core::sc_module_name name, Memory* mem, CTA_Scheduler* cta);
    void add_task(std::shared_ptr<task_t>);
    void add_kernel(std::shared_ptr<kernel_info_t>);
    void task_add_kernel(int taskid, std::shared_ptr<kernel_info_t> kernel);

    std::shared_ptr<task_t> get_task(int id);

    int get_num_task() const { return m_tasks.size(); }
    int get_num_kernel() const { return m_kernels.size(); };
    int get_num_kernel_total() const;

private:
    void mainThread();

    std::vector<std::shared_ptr<task_t>> m_tasks;
    std::vector<std::shared_ptr<kernel_info_t>> m_kernels; // 无上级task的零散kernel

    int m_num_kernel_total;

    // devices
    CTA_Scheduler* m_cta;
    Memory* m_mem;
};
