#include "ventus_cyclesim_impl.hpp"
#include "sysc/kernel/sc_simcontext.h"
#include "top_gpgpu.hpp"

// make libsystemc happy
int sc_main(int argc, char* argv[]) { return 0; }


void ventus_cyclesim_t::constructor(const ventus_cyclesim_config_t* config) {
    m_config = *config;
    sc_set_time_resolution(1, SC_NS);
    m_dut = new Top_gpgpu(m_config.ramulator.config_filename);
    m_result.error = false;
    m_result.time_exceed = false;
    m_result.idle = false;
}

void ventus_cyclesim_t::destructor() { delete m_dut; }

const ventus_cyclesim_step_result_t* ventus_cyclesim_t::step() {
    if (sc_time_stamp().value() >= m_config.sim_time_max) {
        m_result.time_exceed = true;
    } else {
        sc_core::sc_start(PERIOD, SC_NS);
        m_result.time_exceed = false;
    }
    m_result.idle = m_dut->is_idle();
    return &m_result;
}
