#include "task.hpp"
#include "pds_layout.hpp"
#include "ventus_cyclesim.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <spdlog/spdlog.h>
#include <sys/types.h>
#include <vector>

std::shared_ptr<ventus_kernel_metadata_t> parse_metadata(const std::filesystem::path& metafile);
void kernel_load_data(
    ventus_cyclesim_t* sim, std::shared_ptr<const ventus_kernel_metadata_t> metadata,
    std::filesystem::path datafile, std::map<uint64_t, size_t>* vmem_allocated = nullptr
);
// void kernel_load_data(const ventus_kernel_metadata_t* metadata);
void kernel_finish(const ventus_kernel_metadata_t* metadata);
typedef struct kernel_callback_t {
    std::filesystem::path datafile;
    ventus_cyclesim_t* sim;
    std::function<void()> finish_callback;
} kernel_callback_t;

using f_new_kernel_t = std::function<
    int(std::string name, std::string metafile, std::string datafile, bool add_to_task)>;
int parse_arg(
    std::vector<std::string> args, uint64_t& sim_time, f_new_kernel_t new_kernel,
    std::function<int(std::string name)> new_task
);

int main(int argc, char* argv[]) {
#ifdef SPDLOG_ACTIVE_LEVEL
    spdlog::set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
#endif
    spdlog::set_pattern("%v [%l %s:%#]");
    ventus_cyclesim_config_t config;
    ventus_cyclesim_get_default_config(&config);
    config.sim_time_max = 80000;
    ventus_cyclesim_t* sim = ventus_cyclesim_init(&config);

    std::vector<std::shared_ptr<ventus_kernel_metadata_t>> kernels;
    std::vector<std::shared_ptr<task_t>> tasks;
    uint32_t cnt_kernel = 0;

    //
    // functions used during parsing arguments
    //

    auto f_new_kernel = [sim, &kernels, &tasks, &cnt_kernel](
                            std::string name, std::string metafile, std::string datafile,
                            bool add_to_task
                        ) {
        auto kernel = parse_metadata(metafile);
        char* kernel_name_cstr = new char[name.size() + 1];
        std::copy(name.begin(), name.end(), kernel_name_cstr);
        kernel_name_cstr[name.size()] = '\0';
        kernel->name = kernel_name_cstr;
        kernel->kernel_id = cnt_kernel++;
        kernel->data = new kernel_callback_t {
            .datafile = datafile,
            .sim = sim,
            .finish_callback = nullptr,
        };
        if (add_to_task) {
            if (tasks.empty()) {
                std::cerr << "Error: no task to add kernel to" << std::endl;
                return -1;
            }
            auto task = tasks.back();
            kernel->pagetable = task->m_pagetable;
            task->add_kernel(kernel);
        } else {
            kernel->pagetable = ventus_cyclesim_vmem_create(sim);
            kernels.push_back(kernel);
            SPDLOG_TRACE("Create new vmem for kernel {}, ptroot=0x{:x}", name, kernel->pagetable);
        }
        return 0;
    };

    auto f_new_task = [&tasks, &config, sim](std::string name) {
        auto ptroot = ventus_cyclesim_vmem_create(sim);
        tasks.emplace_back(std::make_shared<task_t>(
            tasks.size(), name, ptroot,
            // to destroy the virtual memory space after the task finished
            [sim, ptroot]() { ventus_cyclesim_vmem_destroy(sim, ptroot); },
            // to free the private memory of threads after a kernel of task finished
            [sim, ptroot](uint32_t vaddr, size_t size) {
                ventus_cyclesim_vmem_free(sim, ptroot, vaddr, size);
            }
        ));
        return 0;
    };

    //
    // parse cmdline arguments
    //
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
    parse_arg(args, config.sim_time_max, f_new_kernel, f_new_task);
    ventus_cyclesim_config(sim, &config);

    //
    // Use this to send a kernel to GPU
    //
    auto f_send_kernel_to_gpu = [sim](
                                    std::shared_ptr<ventus_kernel_metadata_t> kernel,
                                    std::function<void()> finish_callback,
                                    std::map<uint64_t, size_t>* vmem_allocated = nullptr
                                ) {
        kernel_callback_t* cb_data = static_cast<kernel_callback_t*>(kernel->data);
        cb_data->finish_callback = finish_callback;
        kernel_load_data(sim, kernel, cb_data->datafile, vmem_allocated);
        ventus_cyclesim_add_kernel(sim, kernel.get(), kernel_finish);
    };

    //
    // Send stand-alone kernels to GPU
    // Activate tasks
    //
    for (auto kernel : kernels) {
        f_send_kernel_to_gpu(kernel, [sim, ptroot = kernel->pagetable]() {
            // to destroy the virtual memory space after the kernel finished
            ventus_cyclesim_vmem_destroy(sim, ptroot);
        });
    }
    for (auto task : tasks) {
        task->activate();
    }

    //
    // Run simulation cycle by cycle
    //
    const ventus_cyclesim_step_result_t* result;
    do {
        result = ventus_cyclesim_step(sim);
        for (auto task : tasks) {
            task->exec(f_send_kernel_to_gpu);
        }
    } while (!result->time_exceed && !ventus_cyclesim_is_idle(sim));

    //
    // Finish simulation
    //
    uint64_t sim_end_time = ventus_cyclesim_get_time(sim);
    ventus_cyclesim_finish(sim, false);
    std::cout << "Simulation finished at time " << sim_end_time << std::endl;
    return 0;
}

void kernel_load_data(
    ventus_cyclesim_t* sim, std::shared_ptr<const ventus_kernel_metadata_t> metadata,
    std::filesystem::path datafile, std::map<uint64_t, size_t>* vmem_allocated
) {
    auto& mtd = *metadata;
    std::ifstream file(datafile);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << datafile << std::endl;
        exit(-1);
        return;
    }

    std::string line;
    int bufferIndex = 0;
    std::vector<uint8_t> buffer;
    SPDLOG_TRACE("Start loading data for kernel {}, ptroot=0x{:x}", mtd.name, mtd.pagetable);
    for (int bufferIndex = 0; bufferIndex < mtd.num_buffer; bufferIndex++) {
        buffer.reserve(mtd.buffer_size[bufferIndex]); // 提前分配空间
        uint64_t vaddr = mtd.buffer_base[bufferIndex];
        size_t vsize = mtd.buffer_allocsize[bufferIndex];
        if (mtd.pdsBaseAddr != 0 && mtd.pdsSize != 0 && vaddr == mtd.pdsBaseAddr) {
            const size_t pds_pool_size = resident_pds_pool_size(mtd);
            if (mtd.buffer_size[bufferIndex] > pds_pool_size) {
                SPDLOG_ERROR(
                    "Kernel {}: PDS buffer data size 0x{:x} exceeds resident pool size 0x{:x}",
                    mtd.name, mtd.buffer_size[bufferIndex], pds_pool_size
                );
                assert(mtd.buffer_size[bufferIndex] <= pds_pool_size);
            }
            vsize = pds_pool_size;
        }
        if (vaddr == 0x80000000 && vsize >= mtd.buffer_size[bufferIndex] + 0x5000) {
            // 0x80000000代码段默认被分配了过分大的0x10000000大小，将其缩小
            vsize = mtd.buffer_size[bufferIndex] + 0x5000;
        }
        vsize = (vsize + 0xfff) & ~0xfff;
        if (!vmem_allocated || !vmem_allocated->contains(vaddr)
            || vmem_allocated->at(vaddr) < vsize) {
            if (vmem_allocated && vmem_allocated->contains(vaddr)) {
                vsize -= vmem_allocated->at(vaddr);
                vaddr += vmem_allocated->at(vaddr);
            }
            uint64_t allocated_vaddr = ventus_cyclesim_vmem_alloc(sim, mtd.pagetable, vaddr, vsize);
            if (allocated_vaddr != vaddr) {
                SPDLOG_ERROR(
                    "Kernel {}: vmem_alloc failed: requested 0x{:x} + 0x{:x}, allocated 0x{:x}",
                    mtd.name, vaddr, vsize, allocated_vaddr
                );
                assert(allocated_vaddr == vaddr);
            }
            if (vmem_allocated) {
                (*vmem_allocated)[vaddr] = vsize;
            }
        }
        assert(!vmem_allocated || vmem_allocated->at(vaddr) == vsize);

        int readbytes = 0;
        while (readbytes < mtd.buffer_size[bufferIndex]) {
            std::getline(file, line);
            for (int i = line.length(); i > 0; i -= 2) {
                std::string hexChars = line.substr(i - 2, 2);
                uint8_t byte = std::stoi(hexChars, nullptr, 16);
                buffer.push_back(byte);
            }
            readbytes += 4;
        }
        ventus_cyclesim_vmemcpy_h2d(
            sim, mtd.pagetable, vaddr, buffer.data(), mtd.buffer_size[bufferIndex]
        );
        buffer.clear();
    }
    file.close();
}

void kernel_finish(const ventus_kernel_metadata_t* metadata) {
    assert(metadata);
    kernel_callback_t* cb_data = static_cast<kernel_callback_t*>(metadata->data);
    assert(cb_data);
    if (cb_data->finish_callback) {
        cb_data->finish_callback();
    }
    delete cb_data;
    delete[] metadata->name;
    delete[] metadata->buffer_size;
    delete[] metadata->buffer_allocsize;
    delete[] metadata->buffer_base;
}
