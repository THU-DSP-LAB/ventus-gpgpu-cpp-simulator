#include "top_gpgpu.hpp"
#include "context_model.hpp"
#include "parameters.h"
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <spdlog/common.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

class custom_formatter : public spdlog::formatter {
public:
    using append_func_t = std::function<std::string()>;

    explicit custom_formatter(append_func_t func)
        : append_func_(std::move(func)) { }

    void format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dest) override {
        auto filename = std::strrchr(msg.source.filename, '/'); // 去除路径
        auto str = fmt::format(
            "{} {} [{} {}:{}]\n", msg.payload, append_func_(),
            spdlog::level::to_string_view(msg.level), filename + 1, msg.source.line
        );
        dest.append(str.data(), str.data() + str.size());
    }

    std::unique_ptr<spdlog::formatter> clone() const override {
        return std::make_unique<custom_formatter>(append_func_);
    }

private:
    append_func_t append_func_;
};

extern std::shared_ptr<std::map<OP_TYPE, decodedat>> gen_decodetable();
extern std::shared_ptr<std::vector<instable_t>> gen_instruction_table();

Top_gpgpu::Top_gpgpu(const char* ramulator_config_filename)
    : m_clk("clk", PERIOD, SC_NS, 0.5, 0, SC_NS, false)
    , m_rstn("rst_n")
    , m_ramulator(std::make_unique<RamulatorWrapper>(ramulator_config_filename)) {

    m_logger = std::make_shared<spdlog::logger>(
        "Ventus-CycleSim-spdlogger", std::make_shared<spdlog::sinks::stdout_sink_mt>()
    );
    m_logger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    m_logger->set_formatter(std::make_unique<custom_formatter>([]() {
        return fmt::format(
            "@{}ns,{}", sc_time_stamp().to_default_time_units(), sc_delta_count_at_current_time()
        );
    }));

    auto instruction_table = gen_instruction_table();
    auto decode_table = gen_decodetable();

    m_ramulator->clk(m_clk);
    m_gmem = m_ramulator->get_memory();
    m_sv39 = std::make_unique<SV39_supervisor>(m_gmem, m_logger);
    m_rst_gen = new BASE_sti("RST_GEN");
    m_rst_gen->rst_n(m_rstn);

    // 缓存部分
    m_l2cache = std::make_unique<L2_Cache>("L2", m_gmem);
    m_l2cache->bind_ramulator(m_ramulator.get());

    for (int i = 0; i < NUM_SM; i++) {
        auto base = new BASE(
            fmt::format("SM{}", i).c_str(), i, instruction_table, decode_table, m_gmem, m_logger
        );
        base->clk(m_clk);
        base->rst_n(m_rstn);
        m_sm.push_back(base); // 必须放前面，为后续 m_sm.data() 构造 cta 用

        std::string l1d_name = fmt::format("L1D_Cache_System{}", i);
        auto l1d_cache
            = std::make_unique<L1D_Cache_System>(l1d_name.c_str(), i, *m_l2cache, m_gmem);

        base->m_l1d_cache = l1d_cache.get();
        l1d_cache->clk(m_clk);
        l1d_Cache_Systems[i] = std::move(l1d_cache);
    }

    m_cta = new CTA_Scheduler("CTA_Scheduler", m_sm.data(), m_logger);
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
    SPDLOG_LOGGER_INFO(m_logger, "[Top_gpgpu::add_kernel] Received kernel {} with pagetable=0x{:x}", 
        metadata.name ? metadata.name : "unknown", metadata.pagetable);
    std::shared_ptr<kernel_info_t> kernel
        = std::make_shared<kernel_info_t>(metadata, load_data_callback, finish_callback, m_logger);
    assert(kernel);
    SPDLOG_LOGGER_INFO(m_logger, "[Top_gpgpu::add_kernel] Created kernel_info_t, kernel->get_pagetable()=0x{:x}", 
        kernel->get_pagetable());
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

Top_gpgpu::pagetable_t Top_gpgpu::vmem_create() { 
    pagetable_t ptroot = m_sv39->create_pagetable();
    SPDLOG_LOGGER_INFO(m_logger, "[Top_gpgpu::vmem_create] Created new pagetable_root=0x{:x}", ptroot);
    return ptroot;
}
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
void Top_gpgpu::debug_print_kernel_status() const { m_cta->debug_print_kernel_status(); }
