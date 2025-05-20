#pragma once

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

#if __GNUC__ >= 4
#define DLL_PUBLIC __attribute__((visibility("default")))
#define DLL_LOCAL __attribute__((visibility("hidden")))
#else
#define DLL_PUBLIC
#define DLL_LOCAL
#endif

#include <stdint.h>

typedef struct ventus_cyclesim_t ventus_cyclesim_t;
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

typedef struct ventus_kernel_metadata_t { // 这个metadata是供驱动使用的，而不是给硬件的
    // Additional data
    const char* name;   // kernel name
    uint64_t kernel_id; // kernel id
    void* data;         // use this as you like, such as callback function argument

    // Raw metadata
    uint64_t startaddr;
    uint64_t kernel_size[3];   // 每个kernel的workgroup三维数目
    uint64_t wf_size;          // 每个warp的thread数目
    uint64_t wg_size;          // 每个workgroup的warp数目
    uint64_t metaDataBaseAddr; // CSR_KNL的值，
    uint64_t ldsSize;          // 每个workgroup使用的local memory的大小
    uint64_t pdsSize;          // 每个thread用到的private memory大小
    uint64_t sgprUsage;        // 每个wavefront(warp)使用的标量寄存器数目
    uint64_t vgprUsage;        // 每个wavefront(warp)(also thread)使用的向量寄存器数目
    uint64_t
        pdsBaseAddr; // private memory的基址，要转成每个workgroup的基地址， wf_size*wg_size*pdsSize
    uint64_t num_buffer;   // buffer的数目，包括pc
    uint64_t* buffer_base; // 各buffer的基址。第一块buffer是给硬件用的metadata
    uint64_t* buffer_size; // 各buffer的size，以Bytes为单位。实际使用的大小，用于初始化.data
    uint64_t* buffer_allocsize; // 各buffer的size，以Bytes为单位。分配的大小

    // Runtime-generated metadata
    uint64_t pagetable; // pagetable root (physical address) (address space ID)
} ventus_kernel_metadata_t;

typedef struct {
    uint64_t sim_time_max; // 最大仿真时间限制
    struct {
        const char* config_filename;
    } ramulator;
    // struct {               // These log sinks can be enabled simultaneously
    //     struct {           // Write log to a file (append to its tail)
    //         bool enable;
    //         const char* level; // "trace", "debug", "info", "warn", "error", "critical"
    //         const char* filename;
    //     } file;
    //     struct { // console log
    //         bool enable;
    //         const char* level;
    //     } console;
    //     const char* level;
    // } log;
    // struct {
    //     uint64_t pagesize; // 物理内存页大小
    //     uint64_t auto_alloc; // 若访存到未分配的物理页，自动分配（如此则与实际硬件内存行为相同）
    //     // 注意，自动分配的物理内存是不会释放的，除非整个仿真结束
    // } pmem;
    // struct { // 波形输出功能，这里只设置正常仿真流程，对仿真快照回溯后的波形输出无影响
    //     bool enable;         // 是否启用？仿真快照回溯后将自动启用
    //     uint64_t time_begin; // 输出波形的起始时刻
    //     uint64_t time_end;   // 输出波形的结束时刻，end > begin才有波形输出
    //     int levels;          // 波形输出的层级
    //     const char* filename;
    // } waveform;
    // struct { // 仿真快照，当仿真出错时可回溯仿真进度到最旧快照，开启波形记录重新仿真
    //     bool enable;
    //     uint64_t time_interval; // 快照时间间隔
    //     int num_max;            // 最大快照数量，超限时新快照将顶替最旧快照
    //     const char* filename;   // 快照输出的FST波形文件名
    // } snapshot;
    // struct {               // verilator运行时命令行参数，以argc,argv形式传入
    //     int argc;          // 注意argc可以为0
    //     const char** argv; // 共有argc个char*字符串，[0]成员不是程序名，而是首个verilator参数
    // } verilator;
} ventus_cyclesim_config_t;

typedef struct {
    bool error;       // Simulation got fatal error, or RTL $finish()
    bool time_exceed; // Simulation time exceeds limit
    bool idle;        // All given kernels has finished
} ventus_cyclesim_step_result_t;

// =
// API functions:
// =

//
// Helper functions
//

// Give you a recommended default config.
DLL_PUBLIC void ventus_cyclesim_get_default_config(ventus_cyclesim_config_t* config);
// Get current simulation time.
DLL_PUBLIC uint64_t ventus_cyclesim_get_time(const ventus_cyclesim_t* sim);
// Check if the simulated GPU is idle (no kernel is running).
DLL_PUBLIC bool ventus_cyclesim_is_idle(const ventus_cyclesim_t* sim);

//
// Init, calculate, and finish
//

// Init the simulation.
DLL_PUBLIC ventus_cyclesim_t* ventus_cyclesim_init(const ventus_cyclesim_config_t* config);

DLL_PUBLIC void ventus_cyclesim_config(
    ventus_cyclesim_t* sim, const ventus_cyclesim_config_t* config
);
DLL_PUBLIC const ventus_cyclesim_config_t* ventus_cyclesim_get_config(ventus_cyclesim_t* sim);

// Finish the simulation.
// If error occurred in the simulation, and snapshot feature enabled,
//   it will rollback to the oldest snapshot to find out what happened.
// You can force the rollback by passing `snapshot_rollback_forcing = true`
DLL_PUBLIC void ventus_cyclesim_finish(ventus_cyclesim_t* sim, bool snapshot_rollback_forcing);

// Calculate 1 unit-time of simulation.
// Return the result of this step: ok, error, time_exceed, or idle.
// If error occurred, calling this function has no effect, you should consider finish the
// simulation.
DLL_PUBLIC const ventus_cyclesim_step_result_t* ventus_cyclesim_step(ventus_cyclesim_t* sim);

//
// Push new kernels to gpu for execution.
//

// After a kernel finishing its execution, the finish_callback will be called, with metadata passed,
//   aka. `finish_callback(metadata)` will be called.

// It's allowed to delay data-loading until the kernel is actually activated on GPU,
// by using data_load_callback
// **Temporary api**, May be removed in the future
DLL_PUBLIC void ventus_cyclesim_add_kernel__delay_data_loading(
    ventus_cyclesim_t* sim, const ventus_kernel_metadata_t* metadata,
    void (*load_data_callback)(const ventus_kernel_metadata_t*),
    void (*finish_callback)(const ventus_kernel_metadata_t*)
);
// It's recommended to use this ↓. Remember to load data to GPU before calling this.
DLL_PUBLIC void ventus_cyclesim_add_kernel(
    ventus_cyclesim_t* sim, const ventus_kernel_metadata_t* metadata,
    void (*finish_callback)(const ventus_kernel_metadata_t*)
);

//
// Physical memory interface
//

// Physical page alloc & free
// These functions are not needed by actual hardware memory, only for reducing simulation memory
// usage. If config.pmem.auto_alloc is set, you don't need to call these functions.
DLL_PUBLIC int ventus_cyclesim_pmem_page_alloc(ventus_cyclesim_t* sim, paddr_t base);
DLL_PUBLIC int ventus_cyclesim_pmem_page_free(ventus_cyclesim_t* sim, paddr_t base);

// Physical memory read & write
// copy data from host to device
DLL_PUBLIC int ventus_cyclesim_pmemcpy_h2d(
    ventus_cyclesim_t* sim, paddr_t dst, const void* src, size_t size
);
// copy data from device to host
DLL_PUBLIC int ventus_cyclesim_pmemcpy_d2h(
    ventus_cyclesim_t* sim, void* dst, paddr_t src, size_t size
);

DLL_PUBLIC paddr_t ventus_cyclesim_vmem_create(ventus_cyclesim_t* sim);
DLL_PUBLIC void ventus_cyclesim_vmem_destroy(ventus_cyclesim_t* sim, paddr_t pagetable_root);
DLL_PUBLIC uint64_t ventus_cyclesim_vmem_alloc(
    ventus_cyclesim_t* sim, paddr_t pagetable_root, uint64_t vaddr, uint64_t size
);
DLL_PUBLIC void ventus_cyclesim_vmem_free(
    ventus_cyclesim_t* sim, paddr_t pagetable_root, uint64_t vaddr, uint64_t size
);
DLL_PUBLIC void ventus_cyclesim_vmemcpy_h2d(
    ventus_cyclesim_t* sim, paddr_t pagetable_root, uint64_t dst, const void* src, uint64_t size
);
DLL_PUBLIC void ventus_cyclesim_vmemcpy_d2h(
    ventus_cyclesim_t* sim, paddr_t pagetable_root, void* dst, uint64_t src, uint64_t size
);

#undef DLL_PUBLIC
#undef DLL_LOCAL

#ifdef __cplusplus
} // extern "C"
#endif
