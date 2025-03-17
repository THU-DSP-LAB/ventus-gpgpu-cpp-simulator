#pragma once

#include "CTA_Scheduler.hpp"
#include "sm/BASE.h"
#include "sm/BASE_sti.h"
#include "sysc/communication/sc_clock.h"
#include "ventus_cyclesim.h"
#include <functional>

class Top_gpgpu {
    using pagetable_t = uint64_t;
    using vaddr_t = uint64_t;
    Memory* m_mem;
    std::vector<BASE*> m_sm;
    CTA_Scheduler* m_cta;
    BASE_sti* m_rst_gen;
    sc_clock m_clk;
    sc_signal<bool> m_rstn;

    int m_kernel_cnt = 0;

public:
    Top_gpgpu(uint8_t num_sm);
    ~Top_gpgpu();

    void add_kernel(std::string name, std::string metafile, std::string datafile);
    void add_kernel(
        const ventus_kernel_metadata_t& metadata,
        std::function<void(const ventus_kernel_metadata_t*)> load_data_callback,
        std::function<void(const ventus_kernel_metadata_t*)> finish_callback
    );
    void pmemcpy_d2h(void* dst, paddr_t src, size_t size);
    void pmemcpy_h2d(paddr_t dst, const void* src, size_t size);
    pagetable_t vmem_create();
    void vmem_destroy(pagetable_t pagetable_root);
    vaddr_t vmem_alloc(pagetable_t pagetable_root, vaddr_t vaddr, size_t size);
    void vmem_free(pagetable_t pagetable_root, vaddr_t vaddr, size_t size);
    void vmemcpy_d2h(pagetable_t pagetable_root, void* dst, uint64_t vaddr, uint64_t size);
    void vmemcpy_h2d(pagetable_t pagetable_root, uint64_t vaddr, const void* src, uint64_t size);
    bool is_idle() const;
};
