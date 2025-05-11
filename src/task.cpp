#include "task.hpp"
#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <spdlog/spdlog.h>

task_t::task_t(
    uint32_t id, const std::string name, uint64_t pagetable, std::function<void()> finish_callback,
    std::function<void(uint32_t, uint32_t)> vmem_free
)
    : m_id(id)
    , m_name(name)
    , m_pagetable(pagetable)
    , m_finish_callback(finish_callback)
    , m_vmem_free(vmem_free) { }

void task_t::add_kernel(std::shared_ptr<kernel_t> kernel) { m_steps.push_back(std::any(kernel)); }

void task_t::exec(std::function<void(
                      std::shared_ptr<kernel_t>, std::function<void()> finish_callback,
                      std::map<uint64_t, size_t>* vmem_allocated
                  )>
                      f_kernel_add) {
    assert(f_kernel_add);
    if (!m_step_is_running) {
        exec_nextstep(f_kernel_add);
    }
}

void task_t::exec_nextstep(std::function<void(
                               std::shared_ptr<kernel_t>, std::function<void()> finish_callback,
                               std::map<uint64_t, size_t>* vmem_allocated
                           )>
                               f_kernel_add) {
    if (m_status == TASKSTATUS_FINISHED) {
        return;
    }
    if (m_step_id_running == m_steps.size() - 1) { // 此task已无新step可供运行
        finish();
        return;
    }
    m_step_id_running++;

    std::any thisstep = m_steps[m_step_id_running];
    if (thisstep.type() == typeid(std::shared_ptr<kernel_t>)) {
        std::shared_ptr<kernel_t> kernel = std::any_cast<std::shared_ptr<kernel_t>>(thisstep);
        assert(kernel);
        std::function<void()> cb_func = std::bind(&task_t::callback_kernel_finish, this, kernel);
        m_kernel_private_memory_vaddr = kernel->pdsBaseAddr;
        m_kernel_private_memory_size = kernel->pdsSize * kernel->wf_size * kernel->wg_size
            * kernel->kernel_size[0] * kernel->kernel_size[1] * kernel->kernel_size[2];
        f_kernel_add(kernel, cb_func, &m_vmem_allocated);
        m_step_is_running = true;
    } else {
        SPDLOG_ERROR("TODO: other type of task step not implemented yet");
        assert(0);
    }
}

void task_t::activate() {
    assert(m_status == TASKSTATUS_IDLE);
    m_status = TASKSTATUS_RUNNING;
}

void task_t::finish() {
    assert(m_status == TASKSTATUS_RUNNING);
    SPDLOG_INFO("Task {} {} finished", m_id, m_name);
    if (m_finish_callback) {
        m_finish_callback();
    }
    m_status = TASKSTATUS_FINISHED;
}

void task_t::callback_kernel_finish(std::shared_ptr<kernel_t> kernel) {
    assert(m_steps[m_step_id_running].type() == typeid(std::shared_ptr<kernel_t>));
    assert(std::any_cast<std::shared_ptr<kernel_t>>(m_steps[m_step_id_running]) == kernel);
    if (m_vmem_free && m_kernel_private_memory_vaddr != 0) {
        m_vmem_free(m_kernel_private_memory_vaddr, m_kernel_private_memory_size);
        assert(m_vmem_allocated.contains(m_kernel_private_memory_vaddr));
        assert(m_vmem_allocated.at(m_kernel_private_memory_vaddr) == m_kernel_private_memory_size);
        m_vmem_allocated.erase(m_kernel_private_memory_vaddr);
    }
    m_kernel_private_memory_vaddr = 0;
    m_step_is_running = false;
}

int task_t::get_num_kernel() const {
    int cnt = 0;
    for (auto step : m_steps) {
        if (step.type() == typeid(std::shared_ptr<kernel_t>)
            && std::any_cast<std::shared_ptr<kernel_t>>(step) != nullptr) {
            cnt++;
        }
    }
    return cnt;
}
