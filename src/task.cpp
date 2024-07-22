#include "task.hpp"
#include "CTA_Scheduler.hpp"
#include "context_model.hpp"
#include "membox_sv39/memory.h"
#include "utils/log.h"
#include <any>
#include <cstdint>
#include <memory>
#include <functional>

task_t::task_t(uint32_t id, const std::string name, uint64_t pagetable)
    : m_id(id)
    , m_name(name)
    , m_pagetable(pagetable)
    , m_step_id_running(-1)
    , m_step_is_running(false) { }

void task_t::add_kernel(std::shared_ptr<kernel_info_t> kernel) {
    m_steps.push_back(std::any(kernel));
}

void task_t::exec(Memory *mem, CTA_Scheduler *cta) {
    if(!m_step_is_running) {
        exec_nextstep(mem, cta);
    }
}

void task_t::exec_nextstep(Memory* mem, CTA_Scheduler* cta) {
    if(m_step_id_running == m_steps.size() - 1)     // 此task已经运行完毕
        return;
    m_step_id_running++;

    std::any thisstep = m_steps[m_step_id_running];
    if (thisstep.type() == typeid(std::shared_ptr<kernel_info_t>)) {
        std::shared_ptr<kernel_info_t> kernel = std::any_cast<std::shared_ptr<kernel_info_t>>(thisstep);
        assert(kernel);
        kernel->m_finish_callback = std::bind(&task_t::callback_kernel_finish, this, kernel);
        cta->kernel_add(kernel);
        m_step_is_running = true;
    } else {
        log_fatal("TODO: other type of task step not implemented yet");
    }
}

void task_t::callback_kernel_finish(std::shared_ptr<kernel_info_t> kernel) {
    assert(m_steps[m_step_id_running].type() == typeid(std::shared_ptr<kernel_info_t>));
    assert(std::any_cast<std::shared_ptr<kernel_info_t>>(m_steps[m_step_id_running]) == kernel);
    m_step_is_running = false;
}

