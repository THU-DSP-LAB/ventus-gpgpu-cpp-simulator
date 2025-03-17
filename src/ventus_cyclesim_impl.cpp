#include "ventus_cyclesim_impl.hpp"
#include "parameters.h"
#include "sysc/kernel/sc_simcontext.h"
#include "top_gpgpu.hpp"

void ventus_cyclesim_t::constructor(const ventus_cyclesim_config_t* config) {
    m_config = *config;
    m_dut = new Top_gpgpu(NUM_SM);
    m_result.error = false;
    m_result.time_exceed = false;
    m_result.idle = false;
}

void ventus_cyclesim_t::destructor() { delete m_dut; }

const ventus_cyclesim_step_result_t* ventus_cyclesim_t::step() {
    if (sc_time_stamp().to_default_time_units() >= m_config.sim_time_max) {
        m_result.time_exceed = true;
    } else {
        sc_core::sc_start(PERIOD, SC_NS);
        m_result.time_exceed = false;
    }
    return &m_result;
}
