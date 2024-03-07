#include "CTA_Scheduler.hpp"

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

void CTA_Scheduler::freeMetadata(meta_data_t &mtd)
{
    delete[] mtd.buffer_base;
    delete[] mtd.buffer_size;
}

void CTA_Scheduler::activate_warp()
{

    SC_REPORT_INFO("CTA_Scheduler", "Activating warps...");

    // 处理metadata数据
    uint64_t knum_workgroup = mtd.kernel_size[0] * mtd.kernel_size[1] * mtd.kernel_size[2]; // k means kernel
    std::cout << "CTA: knum_workgroup=" << knum_workgroup << "\n";
    if (knum_workgroup > 2)
        std::cout << "CTA warning: currently not support so many workgroups\n";
    int warp_limit = hw_num_warp;
    std::cout << "wg_size=" << mtd.wg_size << "\n";
    if (mtd.wg_size > warp_limit)
        std::cout << "CTA error: wg_size=" << mtd.wg_size << " > warp_limit per SM\n";
    for (int i = 0; i < knum_workgroup; i++)
    {
        int warp_counter = 0;
        while (warp_counter < mtd.wg_size)
        {
            // sm_group[i]->m_hw_warps[warp_counter] = new WARP_BONE;
            // sm_group[i]->m_hw_warps[warp_counter]->warp_id = warp_counter;

            std::cout << "CTA: SM" << i << " warp" << warp_counter << " is activated\n";
            sm_group[i]->m_hw_warps[warp_counter]->is_warp_activated = true;
            sm_group[i]->m_hw_warps[warp_counter]->will_warp_activate = true;

            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x300] = 0x00001800;

            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x800] = (mtd.wg_size - warp_counter - 1) * mtd.wf_size;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x801] = mtd.wg_size;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x802] = mtd.wf_size;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x803] = mtd.metaDataBaseAddr;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x804] = 0;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x805] = (mtd.wg_size - warp_counter - 1); // warp标号反了
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x806] = ldsBaseAddr_core;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x807] = 0;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x808] = 0;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x809] = 0;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x810] = 0;
            sm_group[i]->m_hw_warps[warp_counter]->CSR_reg[0x811] = 0;
            ++warp_counter;
        }
        sm_group[i]->mtd = mtd;
        sm_group[i]->mtd.num_buffer = sm_group[i]->mtd.num_buffer;
        sm_group[i]->m_num_warp_activated = warp_counter;
    }
}

void CTA_Scheduler::CTA_INIT()
{
    CTA_Scheduler::activate_warp();
}

std::shared_ptr<kernel_info_t> CTA_Scheduler::select_kernel()
{
    if (m_running_kernels[m_last_issued_kernel] != nullptr &&
        !m_running_kernels[m_last_issued_kernel]->no_more_ctas_to_run())
    {
        if (std::find(m_executed_kernels.begin(), m_executed_kernels.end(),
                      m_running_kernels[m_last_issued_kernel]) == m_executed_kernels.end())
        {
            m_executed_kernels.push_back(m_running_kernels[m_last_issued_kernel]);
            m_running_kernels[m_last_issued_kernel]->start_cycle = uint64_t(sc_time_stamp().to_double() / PERIOD);
        }
        return m_running_kernels[m_last_issued_kernel];
    }

    for (unsigned i = 0; i < m_running_kernels.size(); i++)
    {
        unsigned idx = (i + m_last_issued_kernel + 1) % (max_concurrent_kernel < m_running_kernels.size() ? max_concurrent_kernel : m_running_kernels.size());
        if (m_running_kernels[idx] != nullptr && !m_running_kernels[idx]->no_more_ctas_to_run())
        {
            if (std::find(m_executed_kernels.begin(), m_executed_kernels.end(),
                          m_running_kernels[idx]) == m_executed_kernels.end())
            {
                m_executed_kernels.push_back(m_running_kernels[idx]);
                m_running_kernels[idx]->start_cycle = uint64_t(sc_time_stamp().to_double() / PERIOD);
            }
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

        for (int i = 0; i < NUM_SM; i++)
        {
            unsigned sm_idx = (i + m_last_issue_core + 1) % NUM_SM;
            // select kernel for each SM
            auto kernel = sm_group[sm_idx]->get_current_kernel();
            if (kernel == nullptr ||
                (kernel->no_more_ctas_to_run() && sm_group[sm_idx]->is_current_kernel_completed()))
            {
                kernel = select_kernel();
                if (kernel != nullptr)
                {
                    sm_group[sm_idx]->set_kernel(kernel);
                }
            }

            // issue 1 block for each SM
            if (kernel != nullptr && !kernel->no_more_ctas_to_run() && sm_group[sm_idx]->can_issue_1block(kernel))
            {
                sm_group[sm_idx]->issue_block2core(kernel);
                m_last_issue_core = sm_idx;
                break;
            }
        }
    }
}
