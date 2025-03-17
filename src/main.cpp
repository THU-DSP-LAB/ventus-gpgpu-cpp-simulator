#include "context_model.hpp"
#include "ventus_cyclesim.h"
#include <filesystem>
#include <iostream>

void kernel_load_data(const ventus_kernel_metadata_t* metadata);
typedef struct kernel_load_data_callback_t {
    std::filesystem::path datafile;
    ventus_cyclesim_t* sim;
} kernel_load_data_callback_t;

int main(int argc, char* argv[]) {
    ventus_cyclesim_config_t config;
    config.sim_time_max = 80000;
    ventus_cyclesim_t* sim = ventus_cyclesim_init(&config);

    paddr_t vmem0 = ventus_cyclesim_vmem_create(sim);
    kernel_info_t kernel0(
        0, "matadd", "testcase/adv_matadd/matadd.metadata", "testcase/adv_matadd/matadd.data", vmem0
    );
    ventus_kernel_metadata_t metadata0 = kernel0.get_metadata();
    metadata0.data
        = new kernel_load_data_callback_t { .datafile = "testcase/adv_matadd/matadd.data",
                                            .sim = sim };
    ventus_cyclesim_add_kernel__delay_data_loading(sim, &metadata0, kernel_load_data, nullptr);
    paddr_t vmem1 = ventus_cyclesim_vmem_create(sim);
    kernel_info_t kernel1(
        1, "vecadd", "testcase/adv_vecadd/vecadd_1b4w4t.metadata",
        "testcase/adv_vecadd/vecadd_1b4w4t.data", vmem1
    );
    ventus_kernel_metadata_t metadata1 = kernel1.get_metadata();
    metadata1.data
        = new kernel_load_data_callback_t { .datafile = "testcase/adv_vecadd/vecadd_1b4w4t.data",
                                            .sim = sim };
    ventus_cyclesim_add_kernel__delay_data_loading(sim, &metadata1, kernel_load_data, nullptr);

    const ventus_cyclesim_step_result_t* result;
    do {
        result = ventus_cyclesim_step(sim);
    } while (result->time_exceed == false);
    ventus_cyclesim_finish(sim, false);

    std::cout << "This is my main function!" << std::endl;
    return 0;
}

void kernel_load_data(const ventus_kernel_metadata_t* metadata) {
    kernel_load_data_callback_t* cb_data
        = static_cast<kernel_load_data_callback_t*>(metadata->data);
    const meta_data_t& mtd = *metadata;
    std::ifstream file(cb_data->datafile);
    if (!file.is_open()) {
        log_fatal("Failed to open file: %s", cb_data->datafile.c_str());
        exit(-1);
        return;
    }

    std::string line;
    int bufferIndex = 0;
    std::vector<uint8_t> buffer;
    for (int bufferIndex = 0; bufferIndex < mtd.num_buffer; bufferIndex++) {
        buffer.reserve(mtd.buffer_allocsize[bufferIndex]); // 提前分配空间
        uint64_t vaddr = ventus_cyclesim_vmem_alloc(
            cb_data->sim, mtd.pagetable, mtd.buffer_base[bufferIndex],
            mtd.buffer_allocsize[bufferIndex]
        );
        assert(vaddr == mtd.buffer_base[bufferIndex]);
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
        buffer.resize(mtd.buffer_allocsize[bufferIndex]);
        ventus_cyclesim_vmemcpy_h2d(
            cb_data->sim, mtd.pagetable, mtd.buffer_base[bufferIndex], buffer.data(),
            mtd.buffer_size[bufferIndex]
        );
        buffer.clear();
    }
    // buffers[mtd.num_buffer-1] is localmem(LDS)
    // It contains no initial data, mtd.buffer_size[mtd.num_buffer-1] = 0

    file.close();
}
