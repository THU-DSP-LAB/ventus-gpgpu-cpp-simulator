#pragma once

#include "top_gpgpu.hpp"
#include "ventus_cyclesim.h"
extern "C" struct ventus_cyclesim_t {
    Top_gpgpu* m_dut;
    ventus_cyclesim_step_result_t m_result;
    ventus_cyclesim_config_t m_config;

    void constructor(const ventus_cyclesim_config_t* config);
    void destructor();
    const ventus_cyclesim_step_result_t* step();
};
