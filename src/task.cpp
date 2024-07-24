#include "task.hpp"
#include "CTA_Scheduler.hpp"
#include "context_model.hpp"
#include "membox_sv39/memory.h"
#include "utils/log.h"
#include <any>
#include <cstdint>
#include <functional>
#include <memory>

task_t::task_t(uint32_t id, const std::string name, uint64_t pagetable)
    : m_id(id)
    , m_name(name)
    , m_pagetable(pagetable)
    , m_step_id_running(-1)
    , m_step_is_running(false)
    , m_is_running(false)
    , m_is_finished(false) { }

void task_t::add_kernel(std::shared_ptr<kernel_info_t> kernel) { m_steps.push_back(std::any(kernel)); }

void task_t::exec(Memory* mem, CTA_Scheduler* cta) {
    assert(mem && cta);
    if (!m_step_is_running) {
        exec_nextstep(mem, cta);
    }
}

void task_t::exec_nextstep(Memory* mem, CTA_Scheduler* cta) {
    if (m_step_id_running == m_steps.size() - 1) { // 此task已经运行完毕
        finish();
        return;
    }
    m_step_id_running++;

    std::any thisstep = m_steps[m_step_id_running];
    if (thisstep.type() == typeid(std::shared_ptr<kernel_info_t>)) {
        std::shared_ptr<kernel_info_t> kernel = std::any_cast<std::shared_ptr<kernel_info_t>>(thisstep);
        assert(kernel);
        std::function<void()> cb_func = std::bind(&task_t::callback_kernel_finish, this, kernel);
        kernel->activate(mem, cb_func);
        cta->kernel_add(kernel);
        m_step_is_running = true;
    } else {
        log_fatal("TODO: other type of task step not implemented yet");
    }
}

void task_t::activate(Memory* mem) {
    assert(mem);
    m_is_running = true;
}

void task_t::finish() {
    log_info("Task%d %s finished", m_id, m_name.c_str());
    m_is_finished = true;
    m_is_running = false;
}

void task_t::callback_kernel_finish(std::shared_ptr<kernel_info_t> kernel) {
    assert(m_steps[m_step_id_running].type() == typeid(std::shared_ptr<kernel_info_t>));
    assert(std::any_cast<std::shared_ptr<kernel_info_t>>(m_steps[m_step_id_running]) == kernel);
    m_step_is_running = false;
}

int task_t::get_num_kernel() const {
    int cnt = 0;
    for (auto step : m_steps) {
        if (step.type() == typeid(std::shared_ptr<kernel_info_t>)
            && std::any_cast<std::shared_ptr<kernel_info_t>>(step) != nullptr) {
            cnt++;
        }
    }
    return cnt;
}
