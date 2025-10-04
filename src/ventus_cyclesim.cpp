#include "ventus_cyclesim_impl.hpp"

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
