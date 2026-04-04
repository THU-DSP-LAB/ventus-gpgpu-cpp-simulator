#pragma once

#include "gvm.hpp"
#include "top_gpgpu.hpp"
#include "ventus_cyclesim.h"
extern "C" struct ventus_cyclesim_t {
    Top_gpgpu* m_dut;
    ventus_cyclesim_step_result_t m_result;
    ventus_cyclesim_config_t m_config;
    gvm_t m_gvm;

    void constructor(const ventus_cyclesim_config_t* config);
    void destructor();
    void config(const ventus_cyclesim_config_t* config) { m_config = *config; };
    const ventus_cyclesim_step_result_t* step();
};
