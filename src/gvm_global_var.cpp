#include "gvm_global_var.hpp"
#include <vector>
#include <cstdint>
#include <array>
#include <tuple>
#include <map>

std::vector<Cta2WarpData> g_cta2warp_data;
std::vector<InsnDispatchData> g_insn_dispatch_data;
std::vector<XRegWritebackData> g_xreg_wb_data;
std::vector<WarpXRegInitData> g_warp_xreg_init_data;
uint32_t g_sgprUsage = 64;
uint32_t g_vgprUsage = 128;
std::map<std::tuple<uint32_t,uint32_t,uint32_t>, VRegWritebackData> g_vreg_wb_data;
std::vector<BarDoneData> g_bar_done_data;
