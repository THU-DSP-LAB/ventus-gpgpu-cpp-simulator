#include "ventus_cyclesim_impl.hpp"
#include "gvm_global_var.hpp"
#include "cyclesim_gvm.hpp"
#include "parameters.h"
#include "sysc/kernel/sc_simcontext.h"
#include "top_gpgpu.hpp"

// make libsystemc happy
int sc_main(int argc, char* argv[]) { return 0; }

void ventus_cyclesim_t::constructor(const ventus_cyclesim_config_t* config) {
    m_config = *config;
    sc_set_time_resolution(1, TIME_UNIT);
    g_cta2warp_data.clear();
    g_insn_dispatch_data.clear();
    g_xreg_wb_data.clear();
    g_warp_xreg_init_data.clear();
    g_vreg_wb_data.clear();
    g_bar_done_data.clear();
    m_dut = new Top_gpgpu(
        config->ramulator.enable ? config->ramulator.filename : nullptr,
        m_config.waveform.enable ? m_config.waveform.filename : nullptr
    );
    m_gvm.logger = m_dut->get_logger();
    m_result.error = false;
    m_result.time_exceed = false;
    m_result.idle = false;
}

void ventus_cyclesim_t::destructor() {
    delete m_dut;
    g_cta2warp_data.clear();
    g_insn_dispatch_data.clear();
    g_xreg_wb_data.clear();
    g_warp_xreg_init_data.clear();
    g_vreg_wb_data.clear();
    g_bar_done_data.clear();
}

const ventus_cyclesim_step_result_t* ventus_cyclesim_t::step() {
    m_result.error = false;
    if (sc_time_stamp().value() >= m_config.sim_time_max) {
        m_result.time_exceed = true;
    } else {
        sc_core::sc_start(PERIOD, TIME_UNIT);
        m_result.time_exceed = false;
        if (cyclesim_gvm_enabled()) {
            m_gvm.getDut();
            if (m_gvm.gvmStep() != 0) {
                m_result.error = true;
            }
        }
    }
    m_result.idle = m_dut->is_idle();
    return &m_result;
}
