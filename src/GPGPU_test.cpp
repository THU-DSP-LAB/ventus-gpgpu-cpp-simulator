#include "CTA_Scheduler.hpp"
#include "context_model.hpp"
#include "host.hpp"
#include "membox_sv39/memory.h"
#include "parameters.h"
#include "sm/BASE.h"
#include "sm/BASE_sti.h"
#include "task.hpp"
#include "utils/log.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

// #define TRACE_VCD

int parse_arg(
    std::vector<std::string> args, int& numcycle,
    std::function<
        int(std::string name, std::string metafile, std::string datafile, bool add_to_task)>
        new_kernel,
    std::function<int(std::string name)> new_task
);
int cmdarg_callback_new_task(Host* host, Memory* mem, std::string name);
int cmdarg_callback_new_kernel(
    Host* host, Memory* mem, std::string name, std::string metafile, std::string datafile,
    bool add_to_task
);

__attribute__((visibility("default"))) int sc_main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(true);
    // 虚拟内存与页表
    Memory mem(1ull << 32ull);

    // 硬件实例化
    BASE** BASE_impl;
    BASE_impl = new BASE*[NUM_SM];
    for (int i = 0; i < NUM_SM; i++) {
        BASE_impl[i] = new BASE(("SM" + std::to_string(i)).c_str(), i, &mem);
    }
    BASE_sti BASE_sti_impl("BASE_STI");
    for (int i = 0; i < NUM_SM; i++) {
        for (auto& warp_ : BASE_impl[i]->m_hw_warps) {
            if (warp_ != nullptr) {
                BASE_impl[i]->ev_warp_dispatch_list &= warp_->ev_warp_dispatch;
            }
        }
    }

    CTA_Scheduler cta_impl("CTA_Scheduler", BASE_impl);
    for (int i = 0; i < NUM_SM; i++) {
        BASE_impl[i]->m_warp_finish_callback
            = [&cta_impl](int sm_id, int blk_slot_idx, int warp_idx_in_blk) {
                  cta_impl.warp_finished(sm_id, blk_slot_idx, warp_idx_in_blk);
              };
    }
    Host host_impl("Host_GPGPU_Driver", &mem, &cta_impl);

    // clock & reset signal connect
    sc_clock clk("clk", PERIOD, SC_NS, 0.5, 0, SC_NS, false);
    sc_signal<bool> rst_n("rst_n");
    for (int i = 0; i < NUM_SM; i++) {
        (*BASE_impl[i]).clk(clk);
        (*BASE_impl[i]).rst_n(rst_n);
    }
    BASE_sti_impl.rst_n(rst_n);
    cta_impl.clk(clk);
    cta_impl.rst_n(rst_n);
    host_impl.clk(clk);
    host_impl.rst_n(rst_n);

    // parse cmdline arguments
    std::vector<std::string> args;
    if (argc == 1) { // Default arguments
        puts("[Info] using default cmdline arguments: -f ventus_args.txt");
        args.push_back("-f");
        args.push_back("ventus_args.txt");
    } else {
        for (int i = 1; i < argc; i++) {
            args.push_back(argv[i]);
        }
    }
    int sim_time = 8000000;
    auto f_new_kernel = [&host_impl, &mem](
                            std::string name, std::string metafile, std::string datafile,
                            bool add_to_task
                        ) {
        return cmdarg_callback_new_kernel(&host_impl, &mem, name, metafile, datafile, add_to_task);
    };
    auto f_new_task = [&host_impl, &mem](std::string name) {
        return cmdarg_callback_new_task(&host_impl, &mem, name);
    };
    parse_arg(args, sim_time, f_new_kernel, f_new_task);
    log_debug("Finish reading runtime args");

#ifdef TRACE_VCD
    sc_trace_file* tf[hw_num_warp];
    BASE* recordwave_SM = BASE_impl[1];
    for (int i = 0; i < hw_num_warp; i++) {
        if (recordwave_SM->m_hw_warps[i] != nullptr) {

            tf[i] = sc_create_vcd_trace_file(("output/wave_warp" + std::to_string(i)).c_str());
            tf[i]->set_time_unit(1, SC_NS);
            for (int j = 0; j < 32; j++) {
                sc_trace(
                    tf[i], recordwave_SM->m_hw_warps[i]->CSR_reg[j],
                    "CSR.data(" + std::to_string(j) + ")"
                );
            }
            sc_trace(tf[i], clk, "Clk");
            sc_trace(tf[i], rst_n, "Rst_n");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->jump, "jump");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->jump_addr, "jump_addr");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->branch_sig, "branch_sig");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->fetch_valid, "fetch_valid");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->fetch_valid2, "fetch_valid2");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->pc, "pc");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->fetch_ins, "fetch_ins");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->decode_ins, "decode_ins");
            sc_trace(
                tf[i], recordwave_SM->m_hw_warps[i]->dispatch_warp_valid, "dispatch_warp_valid"
            );
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->ibuf_empty, "ibuf_empty");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->ibuf_swallow, "ibuf_swallow");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->ibuftop_ins, "ibuftop_ins");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->ififo_elem_num, "ififo_elem_num");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->wait_bran, "wait_bran");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->can_dispatch, "can_dispatch");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->is_warp_activated, "is_warp_activated");
            sc_trace(tf[i], recordwave_SM->opc_full, "opc_full");
            sc_trace(tf[i], recordwave_SM->last_dispatch_warpid, "last_dispatch_warpid");
            sc_trace(tf[i], recordwave_SM->issue_ins, "issue_ins");
            sc_trace(tf[i], recordwave_SM->issueins_warpid, "issueins_warpid");
            sc_trace(tf[i], recordwave_SM->dispatch_valid, "dispatch_valid");
            sc_trace(tf[i], recordwave_SM->dispatch_ready, "dispatch_ready");
            sc_trace(tf[i], recordwave_SM->opcfifo_elem_num, "opcfifo_elem_num");
            sc_trace(tf[i], recordwave_SM->emit_ins, "emit_ins");
            sc_trace(tf[i], recordwave_SM->emitins_warpid, "emitins_warpid");
            sc_trace(tf[i], recordwave_SM->doemit, "doemit");
            sc_trace(tf[i], recordwave_SM->findemit, "findemit");
            sc_trace(tf[i], recordwave_SM->emit_idx, "emit_idx");
            sc_trace(tf[i], recordwave_SM->emito_salu, "emito_salu");
            sc_trace(tf[i], recordwave_SM->emito_valu, "emito_valu");
            sc_trace(tf[i], recordwave_SM->emito_vfpu, "emito_vfpu");
            sc_trace(tf[i], recordwave_SM->emito_lsu, "emito_lsu");
            // salu
            sc_trace(tf[i], recordwave_SM->tosalu_data1, "tosalu_data.data1");
            sc_trace(tf[i], recordwave_SM->tosalu_data2, "tosalu_data.data2");
            sc_trace(tf[i], recordwave_SM->tosalu_data3, "tosalu_data.data3");
            sc_trace(tf[i], recordwave_SM->salu_ready, "salu_ready");
            sc_trace(tf[i], recordwave_SM->salufifo_empty, "salufifo_empty");
            sc_trace(tf[i], recordwave_SM->salutmp2, "salutmp2");
            sc_trace(tf[i], recordwave_SM->salutop_dat, "salutop_dat");
            sc_trace(tf[i], recordwave_SM->salufifo_elem_num, "salufifo_elem_num");
            // valu
            sc_trace(tf[i], recordwave_SM->valu_ready, "valu_ready");
            sc_trace(tf[i], recordwave_SM->valuto_simtstk, "valuto_simtstk");
            sc_trace(tf[i], recordwave_SM->branch_elsemask, "branch_elsemask");
            sc_trace(tf[i], recordwave_SM->branch_elsepc, "branch_elsepc");
            sc_trace(tf[i], recordwave_SM->vbranch_ins, "vbranch_ins");
            sc_trace(tf[i], recordwave_SM->vbranchins_warpid, "vbranchins_warpid");

            sc_trace(tf[i], recordwave_SM->valufifo_empty, "valufifo_empty");
            sc_trace(tf[i], recordwave_SM->valutop_dat, "valutop_dat");
            sc_trace(tf[i], recordwave_SM->valufifo_elem_num, "valufifo_elem_num");

            // simt-stack
            sc_trace(tf[i], recordwave_SM->emito_simtstk, "emito_simtstk");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->simtstk_jump, "simtstk_jump");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->simtstk_jumpaddr, "simtstk_jumpaddr");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->current_mask, "current_mask");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->vbran_sig, "vbran_sig");

            sc_trace(tf[i], recordwave_SM->vfpu_ready, "vfpu_ready");
            sc_trace(tf[i], recordwave_SM->vfpufifo_empty, "vfpufifo_empty");
            sc_trace(tf[i], recordwave_SM->vfputop_dat, "vfputop_dat");
            sc_trace(tf[i], recordwave_SM->vfpufifo_elem_num, "vfpufifo_elem_num");
            sc_trace(tf[i], recordwave_SM->lsu_ready, "lsu_ready");
            sc_trace(tf[i], recordwave_SM->lsufifo_empty, "lsufifo_empty");
            sc_trace(tf[i], recordwave_SM->lsutop_dat, "lsutop_dat");
            sc_trace(tf[i], recordwave_SM->lsufifo_elem_num, "lsufifo_elem_num");
            sc_trace(tf[i], recordwave_SM->write_s, "write_s");
            sc_trace(tf[i], recordwave_SM->write_v, "write_v");
            sc_trace(tf[i], recordwave_SM->write_f, "write_f");
            sc_trace(tf[i], recordwave_SM->execpop_salu, "execpop_salu");
            sc_trace(tf[i], recordwave_SM->execpop_valu, "execpop_valu");
            sc_trace(tf[i], recordwave_SM->execpop_vfpu, "execpop_vfpu");
            sc_trace(tf[i], recordwave_SM->execpop_lsu, "execpop_lsu");
            sc_trace(tf[i], recordwave_SM->wb_ena, "wb_ena");
            sc_trace(tf[i], recordwave_SM->wb_ins, "wb_ins");
            sc_trace(tf[i], recordwave_SM->wb_warpid, "wb_warpid");
            for (int j = 0; j < 32; j++) {
                sc_trace(
                    tf[i], recordwave_SM->m_hw_warps[i]->s_regfile[j],
                    "s_regfile.data(" + std::to_string(j) + ")"
                );
            }

            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[0][0], "v_regfile(0)(0)");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[1][0], "v_regfile(1)(0)");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[2][0], "v_regfile(2)(0)");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[3][0], "v_regfile(3)(0)");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[4][0], "v_regfile(4)(0)");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[5][0], "v_regfile(5)(0)");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[6][0], "v_regfile(6)(0)");
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->v_regfile[7][0], "v_regfile(7)(0)");
            // sc_trace(tf[i], BASE_impl., "");
        }
    }
#endif // define TRACE_VCD

    std::cout << "----------Simulation start----------\n";
    auto start = std::chrono::high_resolution_clock::now();
    sc_core::sc_start(sim_time, SC_NS);

    std::cout << "----------Simulation end------------ @ " << sc_core::sc_time_stamp() << std::endl;

#ifdef TRACE_VCD
    for (auto tf_ : tf)
        sc_close_vcd_trace_file(tf_);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Time taken: " << std::dec << duration.count() / 1000 << " milliseconds"
              << std::endl;

    delete[] BASE_impl;
    return 0;
}

int cmdarg_callback_new_task(Host* host, Memory* mem, std::string name) {
    std::shared_ptr<task_t> task
        = std::make_shared<task_t>(host->get_num_task(), name, mem->createRootPageTable());
    assert(task);
    host->add_task(task);
    return 0;
}
int cmdarg_callback_new_kernel(
    Host* host, Memory* mem, std::string name, std::string metafile, std::string datafile,
    bool add_to_task
) {
    if (add_to_task) {
        int taskid = host->get_num_task() - 1;
        if (taskid == -1) {
            std::cerr << "Error: no exist task to contain kernel \"" << name << "\" yet"
                      << std::endl;
            return -1;
        }
        std::shared_ptr<kernel_info_t> kernel = std::make_shared<kernel_info_t>(
            host->get_num_kernel_total(), name, metafile, datafile,
            host->get_task(taskid)->m_pagetable
        );
        assert(kernel);
        host->task_add_kernel(taskid, kernel);
    } else {
        std::shared_ptr<kernel_info_t> kernel = std::make_shared<kernel_info_t>(
            host->get_num_kernel_total(), name, metafile, datafile, mem->createRootPageTable()
        );
        assert(kernel);
        host->add_kernel(kernel);
    }
    return 0;
}