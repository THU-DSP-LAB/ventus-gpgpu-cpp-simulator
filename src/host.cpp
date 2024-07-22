#include "host.hpp"
#include "CTA_Scheduler.hpp"
#include "membox_sv39/memory.h"
#include "task.hpp"
#include <memory>

Host::Host(sc_core::sc_module_name name, Memory *mem, CTA_Scheduler* cta)
    : sc_module(name)
    , m_cta(cta)
    , m_mem(mem) {
    SC_HAS_PROCESS(Host);
    SC_THREAD(mainThread);
}

void Host::add_kernel(std::shared_ptr<kernel_info_t> kernel) {
    kernel_wrapper_t kwrapper;
    kwrapper.ptr = kernel;
    kwrapper.dispatched = false;
    m_kernels.push_back(kwrapper);
}

void Host::add_task(std::shared_ptr<task_t> task) {
    task_wrapper_t twrapper;
    twrapper.ptr = task;
    twrapper.dispatched = false;
    m_tasks.push_back(twrapper);
}

void Host::mainThread() {
    while (true) {
        wait(clk.posedge_event());

        for (auto kwrapper : m_kernels) {
            if (!kwrapper.dispatched) {
                m_cta->kernel_add(kwrapper.ptr);
            }
        }

        for (auto twrapper : m_tasks) {
            if (!twrapper.dispatched && !twrapper.finished) {
                twrapper.ptr->exec(m_mem, m_cta);
                twrapper.dispatched = true;
            }
        }
    }
}
