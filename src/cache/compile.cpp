#include "l1_tlm_adapter.hpp"
#include "l2_tlm.hpp"
#include "sc_l1cache.hpp"
#include <spdlog/spdlog.h>
#include <systemc.h>
#define NLANE 4
using vec_nlane_t = std::array<uint32_t, NLANE>;

struct SimulatedInstruction {
    std::string op;   // "lw" or "sw"
    uint32_t rd;      // 目标寄存器，如 x5 就是 5
    uint32_t imm;     // 偏移量，如 lw x5, 0(x6) 中的 0
    uint32_t rs1_val; // 地址寄存器的值（模拟），如 x6 的值为 0x1000
};

void initialize_memory(std::shared_ptr<PhysicalMemoryInterface> pmem) {
    // 从0x1000开始初始化内存
    const uint32_t start_addr = 0x1000;
    const uint32_t end_addr = 0x30000;
    // 每次写入4字节数据
    const size_t write_size = sizeof(uint32_t);

    // 使用多种有规律的数值模式
    uint32_t patterns[] = {
        0xAAAAAAAA, // 交替的1010模式
        0x55555555, // 交替的0101模式
        0x12345678, // 递增序列
        0xDEADBEEF, // 示例中的模式
        0xCAFEBABE  // 另一个有趣的值
    };

    uint32_t pattern_index = 0;

    for (uint32_t addr = start_addr; addr < end_addr; addr += write_size) {
        // 计算当前地址对应的值
        uint32_t value;

        // 使用不同的规律生成数值
        if (addr % 0x100 == 0) {
            // 每256字节写入地址值本身
            value = addr;
        } else if (addr % 0x80 == 0) {
            // 每128字节写入0xFFFFFFFF
            value = 0xFFFFFFFF;
        } else {
            // 其他地址使用循环模式
            value = patterns[pattern_index % (sizeof(patterns) / sizeof(patterns[0]))];
            pattern_index++;

            // 根据地址修改部分值
            value ^= (addr & 0xFFFF0000) >> 16;
            value += (addr & 0x0000FFFF);
        }

        // 写入内存
        pmem->write(addr, &value, write_size);
    }
    uint32_t custom_data[] = { 0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xA5A5A5A5 };
    for (size_t i = 0; i < sizeof(custom_data) / sizeof(custom_data[0]); ++i) {
        uint32_t addr = 0x8000 + i * 4;
        pmem->write(addr, &custom_data[i], sizeof(uint32_t));
    }
}
__attribute__((visibility("default"))) int sc_main(int argc, char* argv[]) {
    uint64_t max_range = 4 * 1024 * 1024 * 1024; // 4GB
    // Memory* mem = new Memory(max_range);
    std::shared_ptr<PhysicalMemoryInterface> pmem
        = std::make_shared<PhysicalMemoryBasicSim>((1ull << 30), spdlog::default_logger());

    // 正确实例化L2_Cache
    SC_L1_CACHE l1("l1");
    L1_TLM_Adapter l1_adapter("l1_adapter");
    L2_Cache l2("l2", pmem); // 传入Memory对象
    initialize_memory(pmem);
    // ============= 连接核心请求/响应 FIFO =============
    static sc_core::sc_fifo<LSU_2_dcache_coreReq> fifo_core_req("fifo_core_req", 16);
    static sc_core::sc_fifo<dcache_2_LSU_coreRsp> fifo_core_rsp("fifo_core_rsp", 16);
    l1.LSU_2_dcache_coreReq_port(fifo_core_req);
    l1.dcache_2_LSU_coreRsp_port(fifo_core_rsp);

    // ============= 连接 L1 <--> Adapter 的 FIFO =============
    static sc_core::sc_fifo<dcache_2_L2_memReq> fifo_l1_to_adapter("fifo_l1_to_adapter", 16);
    static sc_core::sc_fifo<L2_2_dcache_memRsp> fifo_adapter_to_l1("fifo_adapter_to_l1", 16);
    l1.dcache_2_L2_memReq_port(fifo_l1_to_adapter);
    l1_adapter.memReq_in(fifo_l1_to_adapter);
    l1_adapter.memRsp_out(fifo_adapter_to_l1);
    l1.L2_2_dcache_memRsp_port(fifo_adapter_to_l1);

    // ============= 连接 TLM socket =============
    l1_adapter.initiator_socket.bind(l2.target_socket);

    // 设置时钟
    sc_core::sc_clock clk("clk", PERIOD, SC_NS);
    l1.clk(clk);

    // 假设的指令输入
    std::vector<LSU_2_dcache_coreReq> test_requests;

    std::vector<SimulatedInstruction> instrs = {
        { "lw", 3, 0x0, 0x8000 },
        { "sw", 0, 0x0, 0x8000 },
        { "lw", 4, 0x0, 0x8000 },
    };
    for (size_t i = 0; i < instrs.size(); ++i) {
        const auto& instr = instrs[i];
        LSU_2_dcache_coreReq test_req;

        // 操作类型
        test_req.m_opcode
            = (instr.op == "lw") ? LSU_cache_coreReq_opcode::Read : LSU_cache_coreReq_opcode::Write;

        // 计算真实地址 = 基址 + 偏移
        uint32_t addr = instr.rs1_val + instr.imm;

        // 设置 block_idx
        test_req.m_block_idx = addr / cache_building_block::WORDSIZE;

        // 设置 block_offset (以 word 为单位)
        vec_nlane_t offset;
        for (int l = 0; l < NLANE; ++l) {
            offset[l]
                = (addr % cache_building_block::WORDSIZE) / cache_building_block::LINEWORDS + l;
        }
        test_req.m_block_offset = offset;

        // 设置写数据（如果是 store）
        vec_nlane_t data = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
        test_req.m_data = data;

        // 全部 lane 有效
        test_req.m_mask.fill(true);

        // 元信息
        test_req.m_type = 0;
        test_req.m_wid = i;
        test_req.m_reg_idxw = instr.rd;

        // 写入
        fifo_core_req.write(test_req);
        test_requests.push_back(test_req);
        sc_core::sc_start(200, SC_NS); // 仿真100ns
    }
    // 启动仿真

    // 读取并打印响应（扩展所有字段）
    for (int i = 0; i < 10; ++i) {
        if (fifo_core_rsp.num_available() > 0) {
            dcache_2_LSU_coreRsp rsp = fifo_core_rsp.read(); // 会删除这一项

            std::cout << "========== Response " << i << " ==========" << std::endl;
            std::cout << "Writeback Reg: x" << std::dec << rsp.m_reg_idxw << std::endl;
            std::cout << "Warp ID     : " << rsp.m_wid << std::endl;
            std::cout << "Is Scalar?  : " << std::boolalpha << rsp.m_wxd << std::endl;
            std::cout << "Mask        : ";
            for (int lane = 0; lane < NLANE; ++lane) {
                std::cout << rsp.m_mask[lane] << " ";
            }
            std::cout << std::endl;

            std::cout << "Data        : ";
            for (int lane = 0; lane < NLANE; ++lane) {
                std::cout << "lane[" << lane << "]=0x" << std::hex << rsp.m_data[lane] << " ";
            }
            std::cout << std::endl;
            std::cout << "=========================================" << std::endl;

        } else {
            std::cout << "No response for instruction " << i << "!" << std::endl;
            break;
        }
    }

    return 0;
}