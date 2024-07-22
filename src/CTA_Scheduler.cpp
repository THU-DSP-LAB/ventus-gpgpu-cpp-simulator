#include "CTA_Scheduler.hpp"
#include <memory>

bool CTA_Scheduler::isHexCharacter(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

int CTA_Scheduler::charToHex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1; // Invalid character
}

std::shared_ptr<kernel_info_t> CTA_Scheduler::select_kernel()
{
    if (m_last_issued_kernel >= 0 && m_running_kernels[m_last_issued_kernel] != nullptr &&
        !m_running_kernels[m_last_issued_kernel]->no_more_ctas_to_run())
    {   // 贪婪策略
        //if (std::find(m_executed_kernels.begin(), m_executed_kernels.end(),
        //              m_running_kernels[m_last_issued_kernel]) == m_executed_kernels.end())
        //{
        //    m_executed_kernels.push_back(m_running_kernels[m_last_issued_kernel]);
        //    m_running_kernels[m_last_issued_kernel]->start_cycle = uint64_t(sc_time_stamp().to_double() / PERIOD);
        //    assert(0);
        //}
        return m_running_kernels[m_last_issued_kernel];
    }

    for (unsigned i = 0; i < m_running_kernels.size(); i++)
    {   // 贪婪不成则轮询
        unsigned idx = (i + m_last_issued_kernel + 1) % (max_concurrent_kernel < m_running_kernels.size() ? max_concurrent_kernel : m_running_kernels.size());
        if (m_running_kernels[idx] != nullptr && !m_running_kernels[idx]->no_more_ctas_to_run())
        {
            if (std::find(m_executed_kernels.begin(), m_executed_kernels.end(),
                          m_running_kernels[idx]) == m_executed_kernels.end())
            {
                m_executed_kernels.push_back(m_running_kernels[idx]);
                m_running_kernels[idx]->start_cycle = uint64_t(sc_time_stamp().to_double() / PERIOD);
            } else assert(0);
            m_last_issued_kernel = idx;
            return m_running_kernels[idx];
        }
    }

    return nullptr;
}

void CTA_Scheduler::schedule_kernel2core()
{
    while (true)
    {
        wait(clk.posedge_event());

        if(!rst_n) {
            for(int i = 0; i < NUM_SM; i++) {
                sm_group[i]->m_current_kernel_completed = false;
                sm_group[i]->m_current_kernel_running = false;
            }
            continue;
        }

        // For each SM, check its status. Dispatch new CTA or change to new kernel if needed.
        for (int i = 0; i < NUM_SM; i++)
        {
            const unsigned sm_idx = (i + m_last_issue_core + 1) % NUM_SM;
            BASE * const sm = sm_group[sm_idx];

            // Check if this SM needs changing to a new kernel, and change it if needed
            auto kernel = sm->get_current_kernel();
            if (kernel == nullptr || !sm->m_current_kernel_running)
            {
                kernel = select_kernel();
                if (kernel != nullptr) {
                    sm->set_kernel(kernel);
                    kernel->m_num_sm_running_this++;
                }
            } 
            else if(kernel->no_more_ctas_to_run() && sm->m_current_kernel_running)
            {
                // check
                bool warp_all_finished = true;  // default
                for(auto &warp : sm->m_hw_warps) {
                    if(warp->is_warp_activated) {
                        warp_all_finished = false;
                        break;
                    }
                }
                if (warp_all_finished) {        // change to a new kernel
                    sm->m_current_kernel_completed = true;
                    sm->m_current_kernel_running = false;       // The new kernel will start to run later
                    log_debug("SM%d finish kernel%d %s", sm_idx, kernel->get_kid(), kernel->get_kname().c_str());
                    kernel->m_num_sm_running_this--;
                    if(kernel->m_num_sm_running_this == 0) {
                        kernel->finish();
                    }
                    kernel = select_kernel();   // get new kernel
                    if (kernel != nullptr){
                        sm->set_kernel(kernel);
                        kernel->m_num_sm_running_this++;
                    }
                }
            }

            for(int w = 0; w < hw_num_warp; w++) {
                sm->m_issue_block2warp[w] = false;    // default: not issued
            }

            // issue 1 block for each SM
            if (kernel != nullptr && !kernel->no_more_ctas_to_run() && sm->can_issue_1block(kernel))
            {
                sm->issue_block2core(kernel);
                m_last_issue_core = sm_idx;
                break;
            }
        }
    }
}

bool CTA_Scheduler::kernel_add(std::shared_ptr<kernel_info_t> kernel) {
    m_running_kernels.push_back(kernel);
    return true;
}


