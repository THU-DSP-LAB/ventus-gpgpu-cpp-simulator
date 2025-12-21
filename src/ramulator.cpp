#include "ramulator.hpp"
#include "base/config.h"
#include "base/request.h"
#include "parameters.h"
#include "physical_mem.hpp"
#include "sysc/kernel/sc_module.h"
#include <ctime>
#include <memory>
#include <spdlog/spdlog.h>

RamulatorWrapper::RamulatorWrapper(const char* config_file, std::shared_ptr<spdlog::logger> logger)
    // : sc_module(sc_core::sc_module_name("RamulatorWrapper")) {
    : sc_module("RamulatorWrapper")
    , m_enable_ramulator(config_file != nullptr)
    , m_logger(logger ? logger : spdlog::default_logger()) {

    m_mem = std::make_shared<PhysicalMemoryBasicSim>(1ull << 32);
    m_mmu = std::move(std::make_unique<SV39_basic>(m_mem));

    if (config_file == nullptr) {
        return;
    }

    YAML::Node config = Ramulator::Config::parse_config_file(config_file, {});

    m_frontend.reset(Ramulator::Factory::create_frontend(config));
    m_memorysystem.reset(Ramulator::Factory::create_memory_system(config));

    m_frontend->connect_memory_system(m_memorysystem.get());
    m_memorysystem->connect_frontend(m_frontend.get());

    m_tick_frontend = m_frontend->get_clock_ratio();
    m_tick_memorysystem = m_memorysystem->get_clock_ratio();

    SC_HAS_PROCESS(RamulatorWrapper);
    SC_THREAD(tick);
}

int RamulatorWrapper::request(
    int sm_id, int source_id, paddr_t addr, std::function<void(int sourceId)> callback
) {
    if (!m_enable_ramulator) {
        // Ramulator disabled, respond immediately
        if (callback) {
            callback(source_id);
        }
        return 0;
    }
    // TODO: check paddr alignment
    auto ramulator_callback = [callback, source_id](Ramulator::Request& _) {
        if (callback) {
            callback(source_id);
        }
    };
    if (m_frontend->receive_external_requests(0, addr, sm_id, ramulator_callback)) {
        return 0; // request accepted
    } else {
        return 1; // memory controller busy, request not accepted, try again later
    }
}

int RamulatorWrapper::request(
    int sm_id, std::unique_ptr<lsu_mem_cmd_t>& cmd_,
    std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> callback
) {
    assert(cmd_);
    //
    // 说明：
    // - 当前设计下，虚拟地址翻译由上层 MMU / L1 完成；
    // - RamulatorWrapper 只接收“物理块地址”，不再在此处做二次 SV39 翻译。
    //
    // 因此，这里根据 cache_tag / cache_setIdx 还原出来的地址，直接视为物理块地址使用。
    //
    uint32_t paddr_block = (((cmd_->cache_tag << log2Ceil(L1D_NUM_SET)) | cmd_->cache_setIdx)
                            << log2Ceil(L1D_BLOCK_NUM_WORD))
        << 2;

    if (cmd_->opcode == L1D_OPCODE_READ) {
        std::cout << "[Ramulator-READ] SM" << sm_id << " warp=" << cmd_->warp_id << " ptroot=0x"
                  << std::hex << cmd_->pagetable_root << " paddr_block=0x" << paddr_block
                  << std::dec << std::endl;

        for (int i = 0; i < hw_num_thread; ++i) {
            if (cmd_->mask[i]) {
                uint32_t paddr = paddr_block + (cmd_->blockOffset->at(i) << 2);
                std::cout << "  [READ-thread " << i
                          << "] blockOffset=" << (int)cmd_->blockOffset->at(i) << " -> paddr=0x"
                          << std::hex << paddr << std::dec << std::endl;
            }
        }
        // Deal with the write request
        m_pending_requests.emplace_back();
        auto req = std::prev(m_pending_requests.end());
        req->sm_id = sm_id;
        req->cmd = std::move(cmd_);
        req->callback = callback;
        auto ramulator_callback = [this, req, paddr_block](Ramulator::Request& _) {
            assert(req->cmd->opcode == L1D_OPCODE_READ);
            if (req->cmd->data[0] == 0 && req->cmd->instr.currentpc == 0x800002c0) {
                std::cout << "[ramulator::read] SM" << req->sm_id << " warp" << req->cmd->warp_id
                          << " LW @ pc=0x800002c0: paddr_block=0x" << std::hex << paddr_block
                          << " blockOffset=" << static_cast<int>(req->cmd->blockOffset->at(0))
                          << std::dec << " data_read=0x" << std::hex << req->cmd->data[0]
                          << std::dec << " @ " << sc_time_stamp() << "\n";
            }
            if (req->callback) {
                req->callback(std::move(req->cmd));
            }
            m_pending_requests.erase(req);
        };
        if (!m_enable_ramulator
            || m_frontend->receive_external_requests(0, paddr_block, sm_id, ramulator_callback)) {
            for (int i = 0; i < hw_num_thread; i++) {
                if (req->cmd->mask[i]) {
                    uint32_t paddr = paddr_block + (req->cmd->blockOffset->at(i) << 2);
                    uint32_t data_before = req->cmd->data[i];
                    m_mem->read(paddr, &req->cmd->data[i], 4);
                    // Debug: Check if we're reading 0 when we shouldn't
                    if (req->cmd->data[i] == 0 && req->cmd->instr.currentpc == 0x800002c0) {
                        std::cout << "[ramulator::read] SM" << sm_id << " warp" << req->cmd->warp_id
                                  << " LW @ pc=0x800002c0: paddr_block=0x" << std::hex
                                  << paddr_block << " blockOffset="
                                  << static_cast<int>(req->cmd->blockOffset->at(i)) << " paddr=0x"
                                  << paddr << std::dec << " data_read=0x" << std::hex
                                  << req->cmd->data[i] << std::dec << " @ " << sc_time_stamp()
                                  << "\n";
                    }
                    // 这里总load word（地址向下对齐），在LSU中按照指令lw,lh,lb来选取需要的数据
                }
            }
            if (!m_enable_ramulator) {
                // 不启用DDR时序仿真，立即回调
                Ramulator::Request dummy_request(0, 0); // not used
                ramulator_callback(dummy_request);
            }
            return 0;
        } else { // memory controller busy, request not accepted, try again later
            cmd_ = std::move(req->cmd); // return the borrowed ownership
            m_pending_requests.erase(req);
            return 1;
        }
        // Finish the read request
    } else if (cmd_->opcode == L1D_OPCODE_WRITE) {
        // Deal with the write request
        if (!m_enable_ramulator
            || m_frontend->receive_external_requests(1, paddr_block, sm_id, nullptr)) {
            std::cout << "[Ramulator-WRITE] SM" << sm_id << " warp=" << cmd_->warp_id
                      << " ptroot=0x" << std::hex << cmd_->pagetable_root << std::dec << std::endl;

            for (int threadidx = 0; threadidx < hw_num_thread; threadidx++) {
                if (cmd_->mask[threadidx]) {
                    uint8_t wordOffset1H = cmd_->wordOffset1H->at(threadidx);
                    uint32_t paddr = paddr_block + (cmd_->blockOffset->at(threadidx) << 2);
                    std::cout << "  [WRITE-thread " << threadidx
                              << "] blockOffset=" << (int)cmd_->blockOffset->at(threadidx)
                              << " paddr=0x" << std::hex << paddr << " wordOffset1H=0b"
                              << std::bitset<4>(wordOffset1H) << std::dec
                              << " data=" << cmd_->data[threadidx] << std::endl;
                }
            }
            for (int threadidx = 0; threadidx < hw_num_thread; threadidx++) {
                if (cmd_->mask[threadidx]) {
                    sc_bv<4> wordOffset1H = cmd_->wordOffset1H->at(threadidx);
                    uint32_t paddr = paddr_block + (cmd_->blockOffset->at(threadidx) << 2);
                    const uint8_t* data = reinterpret_cast<const uint8_t*>(&cmd_->data[threadidx]);
                    for (int dataOffset = 0, addrOffset = 0; addrOffset < 4; addrOffset++) {
                        if (wordOffset1H[addrOffset]) {
                            m_mem->write(paddr + addrOffset, data + dataOffset, 1);
                            dataOffset++;
                        }
                    }
                }
            }
            if (callback) {
                // Ramulator的写操作只表明成功接受，不在执行完毕后回调
                // 暂且在Ramulator接受写操作后就回调，移除MSHR中的条目
                callback(std::move(cmd_));
            }
            return 0;
        }
        return 1; // 内存控制器忙，写请求未响应，需稍后再试
        // Finish the write request
    } else if (cmd_->opcode == L1D_OPCODE_CACHEOP) {
        // cache operation: invalidate or flush
        // TODO: 这是L1D的行为，等不再使用L1D的接口后移除
        cmd_.reset();
        return 0;
    } else if (cmd_->opcode == L1D_OPCODE_ATOMIC) {
        SPDLOG_CRITICAL("TODO: Atomic operation not implemented yet");
        assert(0);
        return -1;
    }
    SPDLOG_CRITICAL("RamulatorWrapper: Unsupported opcode: {}", cmd_->opcode);
    return -1; // Unsupported opcode
}

void RamulatorWrapper::tick() {
    while (true) {
        if (!m_enable_ramulator)
            return;
        wait(clk.posedge_event());
        if (m_tick_count % m_tick_frontend == 0) {
            m_frontend->tick();
        }
        if (m_tick_count % m_tick_memorysystem == 0) {
            m_memorysystem->tick();
        }
        m_tick_count++;
    }
}
