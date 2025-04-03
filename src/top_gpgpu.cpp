#include "top_gpgpu.hpp"
#include "parameters.h"

Top_gpgpu::Top_gpgpu()
    : m_clk("clk", PERIOD, SC_NS, 0.5, 0, SC_NS, false)
    , m_rstn("rst_n") {
    m_gmem = std::make_shared<PhysicalMemory>(1ull << 32ull);
    m_sv39 = std::make_unique<SV39_supervisor>(m_gmem);
    m_rst_gen = new BASE_sti("RST_GEN");
    m_rst_gen->rst_n(m_rstn);
    for (int i = 0; i < NUM_SM; i++) {
        m_sm.push_back(new BASE(("SM" + std::to_string(i)).c_str(), i, m_gmem));
        m_sm[i]->clk(m_clk);
        m_sm[i]->rst_n(m_rstn);
        for (auto& hwarp : m_sm[i]->m_hw_warps) {
            if (hwarp != nullptr) {
                m_sm[i]->ev_warp_dispatch_list &= hwarp->ev_warp_dispatch;
            }
        }
    }
    m_cta = new CTA_Scheduler("CTA_Scheduler", m_sm.data());
    for (int i = 0; i < NUM_SM; i++) {
        m_sm[i]->m_warp_finish_callback
            = [cta = this->m_cta](int sm_id, int blk_slot_idx, int warp_idx_in_blk) {
                  cta->warp_finished(sm_id, blk_slot_idx, warp_idx_in_blk);
              };
    }
    m_cta->clk(m_clk);
    m_cta->rst_n(m_rstn);
}

Top_gpgpu::~Top_gpgpu() {
    for (auto sm : m_sm) {
        delete sm;
    }
    delete m_cta;
    delete m_rst_gen;
}

void Top_gpgpu::add_kernel(
    const ventus_kernel_metadata_t& metadata,
    std::function<void(const ventus_kernel_metadata_t*)> load_data_callback,
    std::function<void(const ventus_kernel_metadata_t*)> finish_callback
) {
    std::shared_ptr<kernel_info_t> kernel
        = std::make_shared<kernel_info_t>(metadata, load_data_callback, finish_callback);
    assert(kernel);
    kernel->activate();
    m_cta->kernel_add(kernel);
    m_kernel_cnt++;
}

int Top_gpgpu::pmemcpy_d2h(void* dst, paddr_t src, size_t size) {
    return m_gmem->read(src, dst, size);
}
int Top_gpgpu::pmemcpy_h2d(paddr_t dst, const void* src, size_t size) {
    return m_gmem->write(dst, src, size);
}

Top_gpgpu::pagetable_t Top_gpgpu::vmem_create() { return m_sv39->create_pagetable(); }
void Top_gpgpu::vmem_destroy(pagetable_t root) { m_sv39->destroy_pagetable(root); }

void Top_gpgpu::vmemcpy_d2h(pagetable_t ptroot, void* dst, uint64_t src, uint64_t size) {
    m_sv39->memcpy(ptroot, dst, src, size);
}
void Top_gpgpu::vmemcpy_h2d(pagetable_t ptroot, uint64_t dst, const void* src, uint64_t size) {
    m_sv39->memcpy(ptroot, dst, src, size);
}
Top_gpgpu::vaddr_t Top_gpgpu::vmem_alloc(pagetable_t ptroot, vaddr_t vaddr, size_t size) {
    return m_sv39->mmap(ptroot, vaddr, size);
}
void Top_gpgpu::vmem_free(pagetable_t ptroot, vaddr_t vaddr, size_t size) {
    m_sv39->munmap(ptroot, vaddr, size);
}
bool Top_gpgpu::is_idle() const { return m_cta->is_idle(); }
