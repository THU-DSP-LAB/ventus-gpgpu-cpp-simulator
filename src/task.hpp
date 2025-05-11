#pragma once
#include "ventus_cyclesim.h"
#include <any>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class CTA_Scheduler;
struct ventus_kernel_metadata_t;
using kernel_t = ventus_kernel_metadata_t;

class task_t {
public:
    const uint32_t m_id;
    const std::string m_name;
    const uint64_t m_pagetable;
    task_t(
        uint32_t id, const std::string name, uint64_t pagetable,
        std::function<void()> finish_callback = nullptr,
        std::function<void(uint32_t, uint32_t)> vmem_free = nullptr
    );

    void add_kernel(std::shared_ptr<kernel_t> kernel);
    void callback_kernel_finish(std::shared_ptr<kernel_t> kernel);

    int get_num_kernel() const;

    void exec(std::function<void(
                  std::shared_ptr<kernel_t>, std::function<void()> finish_callback,
                  std::map<uint64_t, size_t>* vmem_allocated
              )>
                  f_kernel_add);

    void activate();
    bool is_running() const { return m_status == TASKSTATUS_RUNNING; }

    void finish(); // 标记task结束、销毁页表
    bool is_finished() const { return m_status == TASKSTATUS_FINISHED; }

private:
    void exec_nextstep(std::function<void(
                           std::shared_ptr<kernel_t>, std::function<void()> finish_callback,
                           std::map<uint64_t, size_t>* vmem_allocated
                       )>
                           f_kernel_add);
    enum {
        STEPTYPE_NONE,
        STEPTYPE_KERNEL,
        STEPTYPE_MEMCPY_D2D,
        STEPTYPE_MEMCPY_H2D,
        STEPTYPE_MEMCPY_D2H
    };

    std::vector<std::any> m_steps;
    int m_step_id_running = -1;

    bool m_step_is_running = false;
    std::map<uint64_t, size_t> m_vmem_allocated;

    enum { TASKSTATUS_IDLE, TASKSTATUS_RUNNING, TASKSTATUS_FINISHED } m_status = TASKSTATUS_IDLE;

    uint32_t m_kernel_private_memory_vaddr = 0;
    uint32_t m_kernel_private_memory_size = 0;

    std::function<void()> m_finish_callback = nullptr; // task finish callback
    std::function<void(uint32_t vaddr, uint32_t size)> m_vmem_free = nullptr;
};
