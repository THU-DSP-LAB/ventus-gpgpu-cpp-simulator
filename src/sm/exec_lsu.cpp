#include "BASE.h"
#include "subcore.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <fmt/ostream.h>
#include <functional>
#include <iterator>
#include <memory>
#include <queue>
#include <ranges>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

constexpr uint8_t LSU_EXTRA_DELAY = 1;
constexpr uint8_t SHARED_MEM_DELAY_WRITE = 2;
constexpr uint8_t SHARED_MEM_DELAY_READ = 2;

// clang-format off
static uint32_t byte_extract(uint32_t data, uint8_t wordOffset1H, bool is_unsigned) {
    if(is_unsigned) {
        switch(wordOffset1H) {
            case 0b1111: return data;
            case 0b1100: return data >> 16;
            case 0b0011: return data & 0xFFFF;
            case 0b1000: return (data >> 24) & 0xFF;
            case 0b0100: return (data >> 16) & 0xFF;
            case 0b0010: return (data >>  8) & 0xFF;
            case 0b0001: return (data >>  0) & 0xFF;
            default:     return data;
        }
    } else {
        int32_t data_ = static_cast<int32_t>(data);
        switch(wordOffset1H) {
            case 0b1111: return data_;
            case 0b1100: return data_ >> 16;
            case 0b0011: return (data_ << 16) >> 16;
            case 0b1000: return (data_ <<  0) >> 24;
            case 0b0100: return (data_ <<  8) >> 24;
            case 0b0010: return (data_ << 16) >> 24;
            case 0b0001: return (data_ << 24) >> 24;
            default:     return data_;
        }
    }
}
// clang-format on

// shared memory (LDS)
int BASE::sharedMem_request(const std::unique_ptr<lsu_mem_cmd_t>& cmd) {
    uint32_t data = 0;
    uint8_t* data_bytes = reinterpret_cast<uint8_t*>(&data);
    auto& mshr_item = m_lsu_mshr.at(cmd->instrId);
    if (!mshr_item.valid) {
        SPDLOG_LOGGER_ERROR(
            m_logger,
            "SM {} warp {} 0x{:x} {}: MSHR not valid: sharedMemory, instrId={}, addr={:x}", sm_id,
            cmd->warp_id, cmd->instr.currentpc, fmt::streamed(cmd->instr), cmd->instrId,
            fmt::join(*cmd->addr, " ")
        );
    }
    assert(mshr_item.valid);
    for (int threadidx = 0; threadidx < hw_num_thread; threadidx++) {
        if (!cmd->mask[threadidx]) {
            continue;
        }
        assert(!mshr_item.finished_mask[threadidx]);
        sc_bv<4> wordOffset1H = cmd->wordOffset1H->at(threadidx);
        uint32_t addr = (cmd->addr->at(threadidx) & ~0b11); // {tag,setIdx,blockOffset} in RTL
        data = cmd->data[threadidx];
        for (int i = 0; i < 4; i++) { // a word
            if (addr + i < ldsBaseAddr_core || addr + i >= ldsBaseAddr_core + hw_lds_size) {
                SPDLOG_LOGGER_ERROR(
                    m_logger, "SM {} warp {} 0x{:x} {}: LDS access out of range: addr=0x{:x}",
                    sm_id, cmd->warp_id, cmd->instr.currentpc, fmt::streamed(cmd->instr), addr + i
                );
                return -1;
            }
            if (cmd->opcode == L1D_OPCODE_READ) { // load
                data_bytes[i] = m_local_mem[addr - ldsBaseAddr_core + i];
            } else if (cmd->opcode == L1D_OPCODE_WRITE) { // store
                if (wordOffset1H[i]) {
                    m_local_mem[addr - ldsBaseAddr_core + i] = data_bytes[i];
                }
            } else { // unknown opcode
                SPDLOG_LOGGER_ERROR(
                    m_logger, "SM {} warp {} 0x{:x} {}: LDS unknown opcode: {}", sm_id,
                    cmd->warp_id, cmd->instr.currentpc, fmt::streamed(cmd->instr), cmd->opcode
                );
                assert(0);
            }
        }
        if (cmd->opcode == L1D_OPCODE_READ) { // load
            assert(mshr_item.data != nullptr);
            mshr_item.data->at(threadidx)
                = byte_extract(data, cmd->wordOffset1H->at(threadidx), cmd->instr.ddd.mem_unsigned);
        }
    }
    m_lsu_mshr[cmd->instrId].finished_mask |= cmd->mask;
    if (cmd->opcode == L1D_OPCODE_WRITE) {
        m_lsu_mshr[cmd->instrId].delay = SHARED_MEM_DELAY_WRITE;
    } else if (cmd->opcode == L1D_OPCODE_READ) {
        m_lsu_mshr[cmd->instrId].delay = SHARED_MEM_DELAY_READ;
    }
    return 0;
}

static uint8_t wordOffset1H_calc(uint32_t addr, const I_TYPE& instr) {
    if (instr.ddd.mem_whb == DecodeParams::MEM_W) {
        return 0b1111;
    } else if (instr.ddd.mem_whb == DecodeParams::MEM_H) {
        return (0b0011 << (addr & 0b10));
    } else if (instr.ddd.mem_whb == DecodeParams::MEM_B) {
        return (0b0001 << (addr & 0b11));
    }
    SPDLOG_ERROR(
        "Unknown mem_whb type: {} in instruction {}", static_cast<int>(instr.ddd.mem_whb),
        fmt::streamed(instr)
    );
    assert(0);
    return 0;
}

static uint32_t private_pds_addr(
    const BASE::lsu_subcore_req_t& req, const I_TYPE& instr, uint32_t private_byte_offset,
    uint32_t lane
) {
    const uint32_t word_base_offset = private_byte_offset & ~3u;
    const uint32_t byte_lane_offset =
        instr.ddd.mem_whb == DecodeParams::MEM_W ? 0u : (private_byte_offset & 3u);
    return req.pds_base
        + word_base_offset * req.csr_numw * req.csr_numt
        + (req.csr_tid + lane) * 4u
        + byte_lane_offset;
}

void BASE::lsu_new_req() {
    assert(m_lsu_subcore_req_queue.size() == 1 || m_lsu_subcore_req_queue.size() == 2);
    auto& req = m_lsu_subcore_req_queue.front();
    int warp_id = warpid_convert(req.subcore_id, req.subcore_warp_id);
    const I_TYPE& instr = req.instr;
    const auto& ddd = instr.ddd;
    const auto& isvec = ddd.isvec;
    const sc_bv<hw_num_thread> mask = isvec ? instr.mask : sc_bv<hw_num_thread> { 1 };
    const int num_thread = std::bit_width(mask.to_uint());
    auto& src1 = *req.src_data1;
    auto& src2 = *req.src_data2;
    auto& src3 = *req.src_data3;

    //
    // 1. 为向量各分量计算访存地址
    //
    auto addr_ptr = std::make_shared<std::array<uint32_t, hw_num_thread>>();
    auto& addr = *addr_ptr;
    auto addr_log_range
        = std::ranges::subrange(addr.begin(), addr.begin() + (isvec ? addr.size() : 1));
    bool is_shared_memory;
    bool type_conflict = false;
    for (int i = 0; i < num_thread; i++) {
        addr[i] = (instr.ddd.isvec && instr.ddd.disable_mask)
            ? (instr.ddd.is_vls12()
                   ? (src1[i] + src2[i])
                   : private_pds_addr(req, instr, src1[i] + src2[i], i))
            : (instr.ddd.isvec ? (src1[i] + (instr.ddd.mop == 0 ? i << 2 : i * src2[i]))
                               : (src1[0] + src2[0]));
        enum { UNKNOWN, GLOBAL, SHARED } addr_type = UNKNOWN, addr_type_;
        if (mask[i]) { // 是否是shared memory访存
            addr_type_
                = ((addr[i] >= ldsBaseAddr_core) && (addr[i] < ldsBaseAddr_core + hw_lds_size))
                ? SHARED
                : GLOBAL;
            if (addr_type == UNKNOWN) {
                addr_type = addr_type_;
                is_shared_memory = (addr_type == SHARED);
            } else if (addr_type != addr_type_) {
                type_conflict = true;
            }
        }
    }
    if (type_conflict) {
        SPDLOG_LOGGER_ERROR(
            m_logger,
            "SM {} warp {} 0x{:x} {} mask={:X}: both global and shared memory access in one "
            "instruction: MEMADDR {:x}",
            sm_id, warp_id, instr.currentpc, instr, instr.mask.to_uint(),
            fmt::join(addr_log_range, " ")
        );
        assert(0);
    }

#ifdef SPIKE_OUTPUT
    if (ddd.mem_cmd == DecodeParams::M_XWR) {
        std::vector<uint32_t> data(isvec ? num_thread : 1, 0);
        for (int i = 0; i < data.size(); i++) {
            data[i] = mask[i] ? src3[i] : 0u;
        }
        SPDLOG_LOGGER_TRACE(
            m_logger, "SM {} warp {} 0x{:x} {} mask={:X} ADDR {:x}, DATA {:x}", sm_id, warp_id,
            instr.currentpc, instr, instr.mask.to_uint(), fmt::join(addr_log_range, " "),
            fmt::join(data, " ")
        );
    } else {
        SPDLOG_LOGGER_TRACE(
            m_logger, "SM {} warp {} 0x{:x} {} mask={:X} MEMADDR {:x}", sm_id, warp_id,
            instr.currentpc, instr, instr.mask.to_uint(), fmt::join(addr_log_range, " ")
        );
    }
#endif

    // 为向量各分量计算L1Dcache的tag、setIdx、blockOffset
    std::array<uint32_t, hw_num_thread> cache_tag;
    std::array<uint32_t, hw_num_thread> cache_setIdx;
    auto blockOffset_ptr = std::make_shared<std::array<uint8_t, hw_num_thread>>();
    auto wordOffset1H_ptr = std::make_shared<std::array<uint8_t, hw_num_thread>>();
    auto& blockOffset = *blockOffset_ptr;
    auto& wordOffset1H = *wordOffset1H_ptr;
    for (int i = 0; i < num_thread; i++) {
        cache_tag[i] = (addr[i] >> (2 + log2Ceil(L1D_BLOCK_NUM_WORD) + log2Ceil(L1D_NUM_SET)));
        cache_setIdx[i]
            = (addr[i] >> (2 + log2Ceil(L1D_BLOCK_NUM_WORD))) & ((1 << log2Ceil(L1D_NUM_SET)) - 1);
        blockOffset[i] = (addr[i] >> 2) & ((1 << log2Ceil(L1D_BLOCK_NUM_WORD)) - 1);
        wordOffset1H[i] = mask[i] ? wordOffset1H_calc(addr[i], instr) : 0;
    }

    //
    // 2. 写入mshr
    //
    auto mshr_it = std::find_if(m_lsu_mshr.begin(), m_lsu_mshr.end(), [](const lsu_mshr_t& mshr) {
        return !mshr.valid;
    });
    const uint8_t mshr_idx = mshr_it - m_lsu_mshr.begin();
    if (mshr_idx >= m_lsu_mshr.size()) {
        SPDLOG_LOGGER_ERROR(
            m_logger, "SM {} warp {} 0x{:x} {} mask={:x}: LSU MSHR full, unacceptable lsu_req",
            sm_id, warp_id, instr.currentpc, instr, instr.mask.to_uint()
        );
        assert(0);
    }
    mshr_it->valid = true;
    mshr_it->warp_id = warp_id;
    mshr_it->instr = instr; // 包括写回、regidx、mask、unsigned等指令decode信息
    mshr_it->wordOffset1H = wordOffset1H_ptr;
    mshr_it->finished_mask = ~mask; // 非活跃⇔已完成，finish_mask全1时此MSHR项目可返回
    mshr_it->addr = addr_ptr;       // debug用的冗余信息
    mshr_it->delay = LSU_EXTRA_DELAY; // 额外的延迟周期
    mshr_it->data
        = (ddd.wxd || ddd.wvd) ? std::make_unique<std::array<reg_t, hw_num_thread>>() : nullptr;

    //
    // 3.1 原子release需要发送额外的flush
    // TODO: 可能还需要fence
    //
    if (instr.ddd.atomic && instr.ddd.rl) {
        std::unique_ptr<lsu_mem_cmd_t> cmd_flush = std::make_unique<lsu_mem_cmd_t>();
        cmd_flush->opcode = L1D_OPCODE_CACHEOP;
        cmd_flush->param = L1D_PARAM_FLUSH;
        m_lsu_mem_cmd_queue.push(std::move(cmd_flush));
    }

    //
    // 3.2 生成常规的访存命令
    //

    uint8_t _amo_param = (instr.ddd.alu_fn == DecodeParams::FN_AMOADD) ? L1D_PARAM_ATOMIC_ADD
        : (instr.ddd.alu_fn == DecodeParams::FN_XOR)                   ? L1D_PARAM_ATOMIC_XOR
        : (instr.ddd.alu_fn == DecodeParams::FN_OR)                    ? L1D_PARAM_ATOMIC_OR
        : (instr.ddd.alu_fn == DecodeParams::FN_AND)                   ? L1D_PARAM_ATOMIC_AND
        : (instr.ddd.alu_fn == DecodeParams::FN_MIN)                   ? L1D_PARAM_ATOMIC_MIN
        : (instr.ddd.alu_fn == DecodeParams::FN_MAX)                   ? L1D_PARAM_ATOMIC_MAX
        : (instr.ddd.alu_fn == DecodeParams::FN_MINU)                  ? L1D_PARAM_ATOMIC_MINU
        : (instr.ddd.alu_fn == DecodeParams::FN_MAXU)                  ? L1D_PARAM_ATOMIC_MAXU
        : (instr.ddd.alu_fn == DecodeParams::FN_SWAP)                  ? L1D_PARAM_ATOMIC_SWAP
                                                                       : L1D_PARAM_ATOMIC_XOR;
    uint8_t _cmd_opcode, _cmd_param;
    if (instr.ddd.atomic) {
        // Simplified from chisel code. What does this mean?
        _cmd_opcode = (ddd.aq || ddd.rl || ddd.alu_fn != DecodeParams::FN_ADD) ? L1D_OPCODE_ATOMIC
            : (instr.ddd.mem_cmd == DecodeParams::M_XWR)                       ? L1D_OPCODE_WRITE
                                                                               : L1D_OPCODE_READ;
        _cmd_param = _amo_param;
    } else if (instr.ddd.fence) {
        _cmd_opcode = L1D_OPCODE_CACHEOP;
        _cmd_param = L1D_PARAM_INVALIDATE;
        // clang-format off
    // TODO: support from-external flush/invalidate
    // } else if (is_flush) {
    //     _cmd_opcode = L1D_OPCODE_CACHEOP;
    //     _cmd_param = L1D_PARAM_INVALIDATE;
        // clang-format on
    } else { // regular load/store
        _cmd_opcode = (ddd.mem_cmd == DecodeParams::M_XWR) ? L1D_OPCODE_WRITE : L1D_OPCODE_READ;
        _cmd_param = L1D_PARAM_NORMAL;
    }

    // for vector load/store, access 1 cacheline each cycle
    sc_bv<hw_num_thread> active_mask = mask;
    while (active_mask.or_reduce()) {
        std::unique_ptr<lsu_mem_cmd_t> cmd = std::make_unique<lsu_mem_cmd_t>();
        cmd->instrId = mshr_idx;
        cmd->pagetable_root = req.pagetable_root;
        cmd->warp_id = warp_id;
        cmd->instr = instr;
        cmd->opcode = _cmd_opcode;
        cmd->param = _cmd_param;
        cmd->is_shared_memory = is_shared_memory;
        cmd->addr = addr_ptr;
        cmd->blockOffset = blockOffset_ptr;
        cmd->wordOffset1H = wordOffset1H_ptr;
        if (!instr.ddd.wxd && !instr.ddd.wvd) { // store instruction
            for (int i = 0; i < num_thread; i++) {
                cmd->data[i] = mask[i] ? src3[i] : 0u;
            }
        }

        int threadidx = std::bit_width(active_mask.to_uint()) - 1; // priority encoder
        sc_bv<hw_num_thread> addr_belonging_same_cacheline_mask = 0;
        for (int i = 0; i < hw_num_thread; i++) {
            if (active_mask[i] && cache_tag[i] == cache_tag[threadidx]
                && cache_setIdx[i] == cache_setIdx[threadidx]) {
                addr_belonging_same_cacheline_mask.set_bit(i, 1);
            }
        }
        cmd->cache_tag = cache_tag[threadidx];
        cmd->cache_setIdx = cache_setIdx[threadidx];
        cmd->mask = addr_belonging_same_cacheline_mask;
        m_lsu_mem_cmd_queue.push(std::move(cmd));
        active_mask &= ~addr_belonging_same_cacheline_mask;
    }

    //
    // 3.3 原子acquire需要发送额外的invalidate
    // TODO: 可能还需要fence
    //
    if (instr.ddd.atomic && instr.ddd.aq) {
        std::unique_ptr<lsu_mem_cmd_t> cmd_invalidate = std::make_unique<lsu_mem_cmd_t>();
        cmd_invalidate->opcode = L1D_OPCODE_CACHEOP;
        cmd_invalidate->param = L1D_PARAM_INVALIDATE;
        m_lsu_mem_cmd_queue.push(std::move(cmd_invalidate));
    }

    m_lsu_subcore_req_queue.pop();
}

void BASE::lsu_main() { // LSU sc_thread
    // L1读写操作完成后的回调函数(global memory)
    std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> read_callback
        = [this](std::unique_ptr<lsu_mem_cmd_t> cmd) { lsu_l1d_read_callback(std::move(cmd)); };
    std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> write_callback
        = [this](std::unique_ptr<lsu_mem_cmd_t> cmd) { lsu_l1d_write_callback(std::move(cmd)); };

    while (true) {
        wait(clk->posedge_event());

        //
        // 1. 处理core pipline lsu_req
        // 将其转化为(可能多个)访存命令压入m_lsu_mem_cmd_queue中、写入MSHR
        //
        if (std::any_of(std::begin(emito_lsu), std::end(emito_lsu), [](const sc_signal<bool>& sig) {
                return sig.read() == true;
            })) {
            assert(
                std::count_if(
                    std::begin(emito_lsu), std::end(emito_lsu),
                    [](const sc_signal<bool>& sig) { return sig.read() == true; }
                )
                == 1
            );
            lsu_new_req();
        }

        //
        // 2. 更新与OPC的握手信号
        //

        //
        // 3. 每周期从cmd queue中取出一个访存命令，发向L1D Cache
        //
        if (m_lsu_mem_cmd_queue.size() > 0) {
            auto& cmd = m_lsu_mem_cmd_queue.front();
            if (cmd->is_shared_memory) {
                // shared memory access
                if (sharedMem_request(cmd) != 0) { // 此函数会自行写入mshr
                    assert(0);
                } else { // shared_memory access ok
                    m_lsu_mem_cmd_queue.pop();
                }
            } else { // global memory access
                std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> callback = nullptr;
                callback = (cmd->opcode == L1D_OPCODE_READ) ? read_callback
                    : (cmd->opcode == L1D_OPCODE_WRITE)     ? write_callback
                                                            : callback;
                // TODO: ramulator以回调函数来返回，但L1D是以FIFO握手来返回，这里暂且不管
                int failed = l1d_request(cmd, callback);
                if (!failed) { // cmd accepted
                    m_lsu_mem_cmd_queue.pop();
                } else if (failed == -1) { // something wrong in the cmd
                    if (cmd->instr.ddd.isvec) {
                        SPDLOG_LOGGER_ERROR(
                            m_logger,
                            "SM {} warp {} 0x{:x} {}: LSU cmd error, MEMADDR {:x} mask={:x}", sm_id,
                            cmd->warp_id, cmd->instr.currentpc, cmd->instr,
                            fmt::join(*cmd->addr, " "), cmd->mask.to_uint()
                        );
                    } else {
                        SPDLOG_LOGGER_ERROR(
                            m_logger, "SM {} warp {} 0x{:x} {}: LSU cmd error, MEMADDR {:x}", sm_id,
                            cmd->warp_id, cmd->instr.currentpc, cmd->instr, cmd->addr->at(0)
                        );
                    }
                } // else: mem is busy, cmd needs to wait
            }
        }

        //
        // 4. 对于MSHR声明已完成的访存，按照登记值施加额外的延迟
        //
        for (auto& mshr_item : m_lsu_mshr) {
            // perf测试表明sc_bv的and_reduce方法耗时异常长，故等效替换掉
            const bool and_reduce = (mshr_item.finished_mask.to_uint() == hw_num_thread_mask);
            if (mshr_item.valid && and_reduce && mshr_item.delay > 0) {
                mshr_item.delay--; // 按照之前的登记值施加额外的延迟
            }
        }

        //
        // 5. 每周期从MSHR中取出一个完成的访存，发向pipeline的下一级
        //
        for (auto& mshr_item : m_lsu_mshr) {
            if (mshr_item.valid && mshr_item.finished_mask.to_uint() == hw_num_thread_mask) {
                // memory access done
#ifdef SPIKE_OUTPUT
                // SPDLOG_LOGGER_TRACE(
                //     m_logger, "SM {} warp {} 0x{:x} {} MEMDONE, MSHR item cleared", sm_id,
                //     mshr_item.warp_id, mshr_item.instr.currentpc, mshr_item.instr
                // );
#endif
                if (mshr_item.instr.ddd.wxd || mshr_item.instr.ddd.wvd) { // to writeback
                    const auto [subcore_idx, subcore_warp_idx] = warpid_convert(mshr_item.warp_id);
                    m_subcores[subcore_idx]->lsu_writeback(
                        mshr_item.instr, subcore_warp_idx, std::move(mshr_item.data)
                    );
                }
                mshr_item.valid = false;
                break;
            }
        }
        // 每个subcore每周期都需要激活此事件，即使并没有发出相应的writeback
        for (auto& subcore : m_subcores) {
            subcore->lsu_writeback_event();
        }
    }
}

// L1读操作完成的回调函数(global memory)
void BASE::lsu_l1d_read_callback(std::unique_ptr<lsu_mem_cmd_t> cmd) {
    assert(cmd && cmd->opcode == L1D_OPCODE_READ);
    assert(m_lsu_mshr.at(cmd->instrId).valid);
    auto& mshr_item = m_lsu_mshr[cmd->instrId];
    for (int i = 0; i < hw_num_thread; i++) {
        if (cmd->mask[i]) {
            assert(!mshr_item.finished_mask[i]);
            assert(mshr_item.data != nullptr);
            mshr_item.data->at(i) = byte_extract(
                cmd->data[i], mshr_item.wordOffset1H->at(i),
                m_lsu_mshr[cmd->instrId].instr.ddd.mem_unsigned
            );
        }
    }
    m_lsu_mshr[cmd->instrId].finished_mask |= cmd->mask;
};

// L1写操作完成的回调函数(global memory)
void BASE::lsu_l1d_write_callback(std::unique_ptr<lsu_mem_cmd_t> cmd) {
    assert(cmd && cmd->opcode == L1D_OPCODE_WRITE);
    assert(m_lsu_mshr.at(cmd->instrId).valid);
    auto& mshr_item = m_lsu_mshr[cmd->instrId];
    // TODO: 目前Ramulator的写操作只表明成功接受，不在执行完毕后回调
    // 此回调函数实质上在Ramulator接受写操作后就被回调
    for (int i = 0; i < hw_num_thread; i++) {
        if (cmd->mask[i]) {
            assert(mshr_item.finished_mask[i] == 0);
        }
    }
    mshr_item.finished_mask |= cmd->mask;
};
