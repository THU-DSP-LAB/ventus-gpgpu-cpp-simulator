#include "host.hpp"
#include "CTA_Scheduler.hpp"
#include "context_model.hpp"
#include "membox_sv39/memory.h"
#include "task.hpp"
#include <memory>

Host::Host(sc_core::sc_module_name name, Memory* mem, CTA_Scheduler* cta)
    : sc_module(name)
    , m_cta(cta)
    , m_mem(mem) {
    SC_HAS_PROCESS(Host);
    SC_THREAD(mainThread);
}

void Host::add_kernel(std::shared_ptr<kernel_info_t> kernel) {
    assert(!kernel->is_running() && !kernel->is_finished());
    m_kernels.push_back(kernel);
}

void Host::add_task(std::shared_ptr<task_t> task) {
    assert(!task->is_running() && !task->is_finished());
    m_tasks.push_back(task);
}

void Host::task_add_kernel(int taskid, std::shared_ptr<kernel_info_t> kernel) {
    assert(!kernel->is_running() && !kernel->is_finished());
    assert(taskid < m_tasks.size());
    assert(!m_tasks[taskid]->is_finished());
    m_tasks[taskid]->add_kernel(kernel);
}

std::shared_ptr<task_t> Host::get_task(int id) {
    assert(id < m_tasks.size());
    return m_tasks[id];
}

int Host::get_num_kernel_total() const {
    int cnt = get_num_kernel();
    for (auto task : m_tasks) {
        cnt += task->get_num_kernel();
    }
    return cnt;
}

void Host::mainThread() {
    while (true) {
        wait(clk.posedge_event());

        for (auto& kernel : m_kernels) {
            if (!kernel->is_running() && !kernel->is_finished()) {
                kernel->activate(m_mem, nullptr);
                m_cta->kernel_add(kernel);
            }
        }

        for (auto& task : m_tasks) {
            if (!task->is_running() && !task->is_finished()) {
                task->activate(m_mem);
            } else if (!task->is_finished()) {
                task->exec(m_mem, m_cta);
            }
        }
    }
}
