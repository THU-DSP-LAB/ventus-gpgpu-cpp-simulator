#pragma once
#include "membox_sv39/memory.h"
#include <memory>
#include <vector>
#include <any>

class kernel_info_t;
class CTA_Scheduler;

class task_t {
public:
    const uint32_t m_id;
    const std::string m_name;
    const uint64_t m_pagetable;
    task_t(uint32_t id, const std::string name, uint64_t pagetable);

    void add_kernel(std::shared_ptr<kernel_info_t> kernel);
    void callback_kernel_finish(std::shared_ptr<kernel_info_t> kernel);

    void exec(Memory *mem, CTA_Scheduler *cta);
    void exec_nextstep(Memory *mem, CTA_Scheduler *cta);

private:
    enum { STEPTYPE_NONE, STEPTYPE_KERNEL, STEPTYPE_MEMCPY_D2D, STEPTYPE_MEMCPY_H2D, STEPTYPE_MEMCPY_D2H };

    std::vector<std::any> m_steps;
    int m_step_id_running;      // Init value: -1

    bool m_step_is_running;
};
