#ifndef CONTEXT_MODEL_H_
#define CONTEXT_MODEL_H_

#include "ventus_cyclesim.h"
#include <functional>
#include <memory>
#include <spdlog/logger.h>
#include <string>

struct dim3 {
    uint64_t x, y, z;
};

void increment_x_then_y_then_z(dim3& i, const dim3& bound);

typedef ventus_kernel_metadata_t meta_data_t;

class kernel_info_t {
public:
    kernel_info_t(
        const meta_data_t& metadata, std::function<void(const meta_data_t*)> load_data_callback,
        std::function<void(const meta_data_t*)> finish_callback,
        std::shared_ptr<spdlog::logger> logger = nullptr
    );

    //
    // Static: only determined by the testcase/host
    //

    uint32_t get_kid() { return m_metadata.kernel_id; }
    std::string get_kname() { return m_metadata.name; }

    unsigned get_startaddr() const { return m_metadata.startaddr; }
    uint32_t get_num_block() const { return m_grid_dim.x * m_grid_dim.y * m_grid_dim.z; }
    unsigned get_num_buffer() const { return m_metadata.num_buffer; }
    unsigned get_num_warp_per_cta() const { return m_metadata.wg_size; }
    unsigned get_num_thread_per_warp() const { return m_metadata.wf_size; }
    unsigned get_ldsSize_per_cta() const { return m_metadata.ldsSize; }
    unsigned get_pdsSize_per_thread() const { return m_metadata.pdsSize; }
    uint64_t get_pdsBaseAddr() const { return m_metadata.pdsBaseAddr; }
    uint64_t get_metadata_baseaddr() const { return m_metadata.metaDataBaseAddr; }
    uint64_t get_pagetable() const { return m_metadata.pagetable; }
    const meta_data_t& get_metadata() const { return m_metadata; }
    dim3 get_num_thread_local_3d() const {
        return { static_cast<uint32_t>(m_metadata.num_thread_local[0]),
                 static_cast<uint32_t>(m_metadata.num_thread_local[1]),
                 static_cast<uint32_t>(m_metadata.num_thread_local[2]) };
    }
    dim3 get_num_thread_global_3d() const {
        return { static_cast<uint32_t>(m_metadata.num_thread_global[0]),
                 static_cast<uint32_t>(m_metadata.num_thread_global[1]),
                 static_cast<uint32_t>(m_metadata.num_thread_global[2]) };
    }
    dim3 get_threadIdx_offset_3d() const {
        return { static_cast<uint32_t>(m_metadata.threadIdxOffset[0]),
                 static_cast<uint32_t>(m_metadata.threadIdxOffset[1]),
                 static_cast<uint32_t>(m_metadata.threadIdxOffset[2]) };
    }

    //
    // Dynamic: changes on GPU. Maybe they should be moved to CTA_Scheduler
    //

    bool no_more_ctas_to_run() const;
    dim3 get_next_cta_id() const { return m_next_cta; }
    unsigned get_next_cta_id_single() const;
    void increment_cta_id() { increment_x_then_y_then_z(m_next_cta, m_grid_dim); }

    // Status of kernel and blocks
    //    | waiting       | data loaded to mem, running | all blocks finished |
    enum { KERNEL_STATUS_WAIT, KERNEL_STATUS_RUNNING, KERNEL_STATUS_FINISHED } m_status;
    enum { BLOCK_STATUS_WAIT, BLOCK_STATUS_RUNNING, BLOCK_STATUS_FINISHED };
    std::vector<int> m_block_status; // status of each block

    // Which SM are blocks running on
    std::vector<int> m_block_sm_id;

    // Load initial data and get ready to run
    void activate();
    bool is_running() const { return m_status == KERNEL_STATUS_RUNNING; }

    // After kernel finished
    void finish();
    bool is_finished() const { return m_status == KERNEL_STATUS_FINISHED; }

private:
    //
    // Static: only determined by the testcase/host
    //
    meta_data_t m_metadata;
    dim3 m_grid_dim;                            // grid size 3D (number of blocks)
    std::function<void()> m_finish_callback;    // callback this when kernel finished
    std::function<void()> m_load_data_callback; // callback this when kernel finished

    //
    // Dynamic: changes on GPU. Maybe they should be moved to CTA_Scheduler
    //

    // Next block to dispatch
    dim3 m_next_cta = { 0, 0, 0 }; // start from 0 ~ (grid_dim - 1)

    //
    // logger
    //
    std::shared_ptr<spdlog::logger> m_logger;
};

#endif
