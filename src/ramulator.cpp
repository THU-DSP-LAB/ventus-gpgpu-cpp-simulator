#include "ramulator.hpp"
#include "base/config.h"
#include "base/request.h"
#include "parameters.h"
#include "physical_mem.hpp"
#include "sysc/kernel/sc_module.h"
#include <ctime>
#include <memory>
#include <spdlog/spdlog.h>

RamulatorWrapper::RamulatorWrapper(
    const std::string& config_file, std::shared_ptr<spdlog::logger> logger
)
    // : sc_module(sc_core::sc_module_name("RamulatorWrapper")) {
    : sc_module("RamulatorWrapper")
    , m_logger(logger ? logger : spdlog::default_logger()) {

    YAML::Node config = Ramulator::Config::parse_config_file(config_file, {});

    m_frontend.reset(Ramulator::Factory::create_frontend(config));
    m_memorysystem.reset(Ramulator::Factory::create_memory_system(config));

    m_frontend->connect_memory_system(m_memorysystem.get());
    m_memorysystem->connect_frontend(m_frontend.get());

    m_tick_frontend = m_frontend->get_clock_ratio();
    m_tick_memorysystem = m_memorysystem->get_clock_ratio();

    m_mem = std::make_shared<PhysicalMemoryBasicSim>(1ull << 32);
    m_mmu = std::move(std::make_unique<SV39_basic>(m_mem));

    SC_HAS_PROCESS(RamulatorWrapper);
    SC_THREAD(tick);
}

int RamulatorWrapper::request(
    int sm_id, std::unique_ptr<lsu_mem_cmd_t>& cmd_,
    std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> callback
) {
    assert(cmd_);
    uint32_t vaddr_block = (((cmd_->cache_tag << log2Ceil(L1D_NUM_SET)) | cmd_->cache_setIdx)
                            << log2Ceil(L1D_BLOCK_NUM_WORD))
        << 2;
    uint32_t paddr_block = m_mmu->translate(cmd_->pagetable_root, vaddr_block);

    if (paddr_block == 0) {
        SPDLOG_ERROR(
            "MMU translate failed: SM{} warp{} ptroot=0x{:x} vaddr=0x{:x}", sm_id, cmd_->warp_id,
            cmd_->pagetable_root, vaddr_block
        );
        return -1;
    }

    if (cmd_->opcode == L1D_OPCODE_READ) {
        // Deal with the write request
        m_pending_requests.emplace_back();
        auto req = m_pending_requests.end();
        --req;
        req->sm_id = sm_id;
        req->cmd = std::move(cmd_);
        req->callback = callback;
        req->id = m_request_id++;
        uint64_t req_id = req->id;
        // 也可直接捕获req迭代器，而不是再加一个req_id字段，因为std::list只要不删除此元素其迭代器就一直有效
        // 但这样编译器会报warning
        auto ramulator_callback = [this, req_id](Ramulator::Request& _) {
            auto it = std::find_if(
                m_pending_requests.begin(), m_pending_requests.end(),
                [req_id](const request_t& r) { return r.id == req_id; }
            );
            assert(it != m_pending_requests.end());
            assert(it->cmd->opcode == L1D_OPCODE_READ);
            if (it->callback) {
                it->callback(std::move(it->cmd));
            }
            m_pending_requests.erase(it);
        };
        if (m_frontend->receive_external_requests(0, paddr_block, sm_id, ramulator_callback)) {
            for (int i = 0; i < hw_num_thread; i++) {
                if (req->cmd->mask[i]) {
                    uint32_t paddr = paddr_block + (req->cmd->blockOffset->at(i) << 2);
                    m_mem->read(paddr, &req->cmd->data[i], 4);
                    // 这里总load word（地址向下对齐），在LSU中按照指令lw,lh,lb来选取需要的数据
                }
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
        if (m_frontend->receive_external_requests(1, paddr_block, sm_id, nullptr)) {
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
