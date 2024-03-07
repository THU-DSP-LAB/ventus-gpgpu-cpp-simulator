#include "parameters.h"
#include "sm/BASE.h"
#include "sm/BASE_sti.h"
#include "CTA_Scheduler.hpp"
#include <chrono>
#include <fstream>

int sc_main(int argc, char *argv[])
{
    // 处理命令行参数
    std::string metafile, datafile, numcycle, kernelName;
    int numkernel = 0;
    std::vector<std::shared_ptr<kernel_info_t>> m_running_kernels;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--numkernel") == 0)
        {
            numkernel = std::stoi(argv[i + 1]);
            i++;
            m_running_kernels.resize(numkernel);
            for (int j = 0; j < numkernel; j++)
            {
                kernelName = argv[i + 1];
                i++;
                metafile = argv[i + 1];
                i++;
                datafile = argv[i + 1];
                i++;
                std::cout << "Initializing kernel " << kernelName << " info ...\n";
                m_running_kernels[j] = std::make_shared<kernel_info_t>(kernelName, "./testcase/" + metafile, "./testcase/" + datafile);
            }
        }
        if (strcmp(argv[i], "--metafile") == 0)
        { // like "vecadd/vecadd.riscv.meta"
            metafile = argv[i + 1];
            i++;
        }
        if (strcmp(argv[i], "--datafile") == 0)
        {
            datafile = argv[i + 1];
            i++;
        }
        if (strcmp(argv[i], "--numcycle") == 0)
        {
            numcycle = argv[i + 1];
            i++;
        }
    }
    std::cout << "Finish reading runtime args\n";

    std::cout << "----------Initializing SM data-structures----------\n";
    BASE **BASE_impl;
    BASE_impl = new BASE *[NUM_SM];
    for (int i = 0; i < NUM_SM; i++)
    {
        BASE_impl[i] = new BASE(("SM" + std::to_string(i)).c_str(), i);
    }
    BASE_sti BASE_sti_impl("BASE_STI");

    std::cout << "----------Initializing CTAs----------\n";
    CTA_Scheduler cta_impl("CTA_Scheduler");
    cta_impl.set_running_kernels(m_running_kernels);
    cta_impl.sm_group = BASE_impl;
    // cta_impl.CTA_INIT();

    for (int i = 0; i < NUM_SM; i++)
    {
        BASE_impl[i]->set_CTA_Scheduler(&cta_impl);
    }

    for (int i = 0; i < NUM_SM; i++)
    {
        for (auto &warp_ : BASE_impl[i]->m_hw_warps)
        {
            if (warp_ != nullptr)
            {
                BASE_impl[i]->ev_issue_list &= warp_->ev_issue;
            }
        }
    }

    sc_clock clk("clk", PERIOD, SC_NS, 0.5, 0, SC_NS, false);
    sc_signal<bool> rst_n("rst_n");

    for (int i = 0; i < NUM_SM; i++)
    {
        (*BASE_impl[i]).clk(clk);
        (*BASE_impl[i]).rst_n(rst_n);
    }
    BASE_sti_impl.rst_n(rst_n);

    cta_impl.clk(clk);

    sc_trace_file *tf[hw_num_warp];
    BASE *recordwave_SM = BASE_impl[1];
    for (int i = 0; i < hw_num_warp; i++)
    {
        if (recordwave_SM->m_hw_warps[i] != nullptr)
        {

            tf[i] = sc_create_vcd_trace_file(("output/wave_warp" + std::to_string(i)).c_str());
            tf[i]->set_time_unit(1, SC_NS);
            for (int j = 0; j < 32; j++)
            {
                sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->CSR_reg[j], "CSR.data(" + std::to_string(j) + ")");
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
            sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->dispatch_warp_valid, "dispatch_warp_valid");
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
            for (int j = 0; j < 32; j++)
            {
                sc_trace(tf[i], recordwave_SM->m_hw_warps[i]->s_regfile[j], "s_regfile.data(" + std::to_string(j) + ")");
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

    std::cout << "----------Simulation start----------\n";
    auto start = std::chrono::high_resolution_clock::now();
    sc_core::sc_start(std::stoi(numcycle), SC_NS);

    std::cout << "----------Simulation end------------\n";

    for (auto tf_ : tf)
        sc_close_vcd_trace_file(tf_);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Time taken: " << std::dec << duration.count() / 1000 << " milliseconds" << std::endl;

    delete[] BASE_impl;
    return 0;
}
