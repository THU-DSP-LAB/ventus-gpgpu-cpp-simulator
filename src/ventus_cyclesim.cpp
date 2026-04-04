#include "ventus_cyclesim_impl.hpp"
#include "../../spike/gvmref/gvmref_interface.h"
#include "cyclesim_gvm.hpp"
#include "parameters.h"
#include <cassert>
#include <unordered_map>

static std::unordered_map<uint64_t, uint64_t> g_cyclesim_gvm_kernel_wg_id_base;

ventus_cyclesim_t* ventus_cyclesim_init(const ventus_cyclesim_config_t* config) {
    ventus_cyclesim_t* sim = new ventus_cyclesim_t();
    sim->constructor(config);
    return sim;
}

void ventus_cyclesim_get_default_config(ventus_cyclesim_config_t* config) {
    config->sim_time_max = ~0ull;
    config->ramulator.enable = true;
    config->ramulator.filename = VENTUS_CYCLESIM_PROJECT_DIR "/ramulator_config.yaml";
    config->waveform.enable = false;
    config->waveform.filename = "cyclesim";
}

int ventus_cyclesim_get_param_u64(ventus_cyclesim_param_id_t param, uint64_t* value) {
    if (value == nullptr) return -1;
    switch (param) {
    case VENTUS_CYCLESIM_PARAM_NUM_SM:
        *value = NUM_SM;
        return 0;
    case VENTUS_CYCLESIM_PARAM_NUM_WARP_PER_SM:
        *value = hw_num_warp;
        return 0;
    case VENTUS_CYCLESIM_PARAM_NUM_THREAD_PER_WARP:
        *value = hw_num_thread;
        return 0;
    case VENTUS_CYCLESIM_PARAM_MAX_CTA_PER_SM:
        *value = MAX_CTA_PER_CORE;
        return 0;
    case VENTUS_CYCLESIM_PARAM_LOCAL_MEM_SIZE:
        *value = hw_lds_size;
        return 0;
    default:
        return -1;
    }
}

void ventus_cyclesim_config(ventus_cyclesim_t* sim, const ventus_cyclesim_config_t* config) {
    sim->config(config);
}

const ventus_cyclesim_config_t* ventus_cyclesim_get_config(ventus_cyclesim_t* sim) {
    return &sim->m_config;
}

const ventus_cyclesim_step_result_t* ventus_cyclesim_step(ventus_cyclesim_t* sim) {
    return sim->step();
}

void ventus_cyclesim_finish(ventus_cyclesim_t* sim, bool snapshot_rollback_forcing) {
    sim->destructor();
}

uint64_t ventus_cyclesim_get_time(const ventus_cyclesim_t* sim) { return sc_time_stamp().value(); }
bool ventus_cyclesim_is_idle(const ventus_cyclesim_t* sim) { return sim->m_dut->is_idle(); }

void ventus_cyclesim_add_kernel__delay_data_loading(
    ventus_cyclesim_t* sim, const ventus_kernel_metadata_t* metadata,
    void (*load_data_callback)(const ventus_kernel_metadata_t*),
    void (*finish_callback)(const ventus_kernel_metadata_t*)
) {
    sim->m_dut->add_kernel(*metadata, load_data_callback, finish_callback);
}

void ventus_cyclesim_add_kernel(
    ventus_cyclesim_t* sim, const ventus_kernel_metadata_t* metadata,
    void (*finish_callback)(const ventus_kernel_metadata_t*)
) {
    sim->m_dut->add_kernel(*metadata, nullptr, finish_callback);
}

int ventus_cyclesim_pmem_page_alloc(ventus_cyclesim_t* sim, paddr_t base) { return 0; }
int ventus_cyclesim_pmem_page_free(ventus_cyclesim_t* sim, paddr_t base) { return 0; }

int ventus_cyclesim_pmemcpy_d2h(ventus_cyclesim_t* sim, void* dst, paddr_t src, uint64_t size) {
    sim->m_dut->pmemcpy_d2h(dst, src, size);
    return 0;
}

int ventus_cyclesim_pmemcpy_h2d(
    ventus_cyclesim_t* sim, paddr_t dst, const void* src, uint64_t size
) {
    sim->m_dut->pmemcpy_h2d(dst, src, size);
    return 0;
}

paddr_t ventus_cyclesim_vmem_create(ventus_cyclesim_t* sim) { return sim->m_dut->vmem_create(); }

void ventus_cyclesim_vmem_destroy(ventus_cyclesim_t* sim, paddr_t pagetable_root) {
    sim->m_dut->vmem_destroy(pagetable_root);
}

void ventus_cyclesim_vmemcpy_h2d(
    ventus_cyclesim_t* sim, paddr_t ptroot, vaddr_t dst, const void* src, uint64_t size
) {
    sim->m_dut->vmemcpy_h2d(ptroot, dst, src, size);
}

void ventus_cyclesim_vmemcpy_d2h(
    ventus_cyclesim_t* sim, paddr_t ptroot, void* dst, vaddr_t src, size_t size
) {
    sim->m_dut->vmemcpy_d2h(ptroot, dst, src, size);
}

vaddr_t ventus_cyclesim_vmem_alloc(
    ventus_cyclesim_t* sim, paddr_t ptroot, vaddr_t vaddr, size_t size
) {
    return sim->m_dut->vmem_alloc(ptroot, vaddr, size);
}

void ventus_cyclesim_vmem_free(
    ventus_cyclesim_t* sim, paddr_t ptroot, vaddr_t vaddr, size_t size
) {
    sim->m_dut->vmem_free(ptroot, vaddr, size);
}

void ventus_cyclesim_gvm_reset_kernel_wg_id_base() { g_cyclesim_gvm_kernel_wg_id_base.clear(); }

void ventus_cyclesim_gvm_set_kernel_wg_id_base(uint64_t kernel_id, uint64_t software_wg_id_base) {
    g_cyclesim_gvm_kernel_wg_id_base[kernel_id] = software_wg_id_base;
}

uint64_t ventus_cyclesim_gvm_get_kernel_wg_id_base(uint64_t kernel_id) {
    auto it = g_cyclesim_gvm_kernel_wg_id_base.find(kernel_id);
    assert(it != g_cyclesim_gvm_kernel_wg_id_base.end());
    return it->second;
}

extern "C" int fw_vt_dev_open() {
    return cyclesim_gvm_enabled() ? gvmref_vt_dev_open() : 0;
}

extern "C" int fw_vt_dev_close() {
    return cyclesim_gvm_enabled() ? gvmref_vt_dev_close() : 0;
}

extern "C" int fw_vt_buf_alloc(
    uint64_t size, uint64_t* vaddr, int BUF_TYPE, uint64_t taskID, uint64_t kernelID
) {
    return cyclesim_gvm_enabled() ? gvmref_vt_buf_alloc(size, vaddr, BUF_TYPE, taskID, kernelID)
                                  : 0;
}

extern "C" int fw_vt_buf_free(
    uint64_t size, uint64_t* vaddr, uint64_t taskID, uint64_t kernelID
) {
    return cyclesim_gvm_enabled() ? gvmref_vt_buf_free(size, vaddr, taskID, kernelID) : 0;
}

extern "C" int fw_vt_one_buf_free(
    uint64_t size, uint64_t* vaddr, uint64_t taskID, uint64_t kernelID
) {
    return cyclesim_gvm_enabled() ? gvmref_vt_one_buf_free(size, vaddr, taskID, kernelID) : 0;
}

extern "C" int fw_vt_copy_to_dev(
    uint64_t dev_vaddr, const void* src_addr, uint64_t size, uint64_t taskID, uint64_t kernelID
) {
    return cyclesim_gvm_enabled()
        ? gvmref_vt_copy_to_dev(dev_vaddr, src_addr, size, taskID, kernelID)
        : 0;
}

extern "C" int fw_vt_start(void* metaData, uint64_t taskID) {
    return cyclesim_gvm_enabled() ? gvmref_vt_start(metaData, taskID) : 0;
}

extern "C" int fw_vt_upload_kernel_file(const char* filename, int taskID) {
    return cyclesim_gvm_enabled() ? gvmref_vt_upload_kernel_file(filename, taskID) : 0;
}
