#include "context_model.hpp"
#include "utils/log.h"
#include <functional>

kernel_info_t::kernel_info_t(
    const meta_data_t& metadata, std::function<void(const meta_data_t*)> load_data_callback,
    std::function<void(const meta_data_t*)> finish_callback
)
    : m_metadata(metadata) {
    m_grid_dim.x = metadata.kernel_size[0];
    m_grid_dim.y = metadata.kernel_size[1];
    m_grid_dim.z = metadata.kernel_size[2];
    m_finish_callback
        = finish_callback ? std::bind(finish_callback, &m_metadata) : std::function<void()>();
    m_load_data_callback
        = load_data_callback ? std::bind(load_data_callback, &m_metadata) : std::function<void()>();
    m_status = kernel_info_t::KERNEL_STATUS_WAIT;
    m_block_status.resize(get_num_block(), BLOCK_STATUS_WAIT);
    m_block_sm_id.resize(get_num_block(), -1);
    log_info(
        "kernel %s initialized, set grid_dim = %d,%d,%d", m_metadata.name, m_grid_dim.x,
        m_grid_dim.y, m_grid_dim.z
    );
}

void kernel_info_t::finish() {
    assert(m_status == KERNEL_STATUS_RUNNING);
    m_status = KERNEL_STATUS_FINISHED;
    log_info("Kernel %d %s finished", get_kid(), get_kname().c_str());
    if (m_finish_callback) {
        m_finish_callback();
    }
}

bool kernel_info_t::no_more_ctas_to_run() const {
    return (
        m_next_cta.x >= m_grid_dim.x || m_next_cta.y >= m_grid_dim.y || m_next_cta.z >= m_grid_dim.z
    );
}

unsigned kernel_info_t::get_next_cta_id_single() const {
    return m_next_cta.x + m_grid_dim.x * m_next_cta.y + m_grid_dim.x * m_grid_dim.y * m_next_cta.z;
}

// 激活Kernel，载入初始数据，随时开始运行
void kernel_info_t::activate() {
    assert(m_status == KERNEL_STATUS_WAIT);
    if (m_load_data_callback) {
        m_load_data_callback();
        log_info("Kernel%d %s load init data (callback)", m_metadata.kernel_id, m_metadata.name);
    }
    m_status = KERNEL_STATUS_RUNNING;
}
