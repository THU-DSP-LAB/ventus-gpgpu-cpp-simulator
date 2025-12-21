#include "l2_tlm.hpp"
#include "../ramulator.hpp"
#include "interfaces.h"
#include <cstdio>
#include <sstream>
tlm::tlm_sync_enum L2_Cache::nb_transport_fw(
    int l1id, tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_core::sc_time& delay
) {
    if (phase == tlm::BEGIN_REQ) {
        // 1) 把 transaction 存到队列
        req_queue.emplace_back(l1id, &trans);
        auto* reqExt
            = dynamic_cast<DCacheMemReqExtension*>(trans.get_extension<DCacheMemReqExtension>());
        if (reqExt) {
            std::cout << std::setw(5) << (sc_time_stamp().to_default_time_units() / 10 + 1)
                      << " | memReq |";
            std::cout << "   |" << std::setw(3) << reqExt->req.a_source;
            std::cout << "| a_op=" << static_cast<int>(reqExt->req.a_opcode) << std::endl;
        } else {
            std::cout << "Error: No extension found in transaction!" << std::endl;
        }
        // 2) 返回 TLM_ACCEPTED or TLM_UPDATED
        //    这里简单返回 TLM_ACCEPTED
        return tlm::TLM_ACCEPTED;
    } else if (phase == tlm::END_RESP) {
        // Initiator 通知 Response 结束
        // 常规 4-phase 协议时会来这里
        return tlm::TLM_ACCEPTED;
    }
    // 其他 phase 忽略
    return tlm::TLM_ACCEPTED;
}

void L2_Cache::process_queue() {
    while (true) {
        wait(PERIOD, sc_core::SC_NS); // every cycle

        if (!req_queue.empty()) {
            auto [l1id, trans] = req_queue.front();
            req_queue.pop_front();

            // 1) 先解析请求 extension
            auto* reqExt
                = dynamic_cast<DCacheMemReqExtension*>(trans->get_extension<DCacheMemReqExtension>()
                );
            if (!reqExt) {
                SC_REPORT_ERROR("L2_Cache", "No extension found in trans!");
                continue;
            }

            // ============ 多阶段模拟 =============

            //  构造响应 extension
            auto* rspExt = new L2MemRspExtension;
            rspExt->rsp.d_source = reqExt ? reqExt->req.a_source : 0;
            std::array<bool, cache_building_block::LINEWORDS> return_mask {};
            bool response_sent = false; // 标记响应是否已发送
            // char log_buf[128];
            // snprintf(log_buf, 128, "Physical memory access addr=0x%x", reqExt->req.a_address);
            // SC_REPORT_INFO("L2_Cache_TLM_Req", log_buf);

            if (reqExt->req.a_opcode == TL_UH_A_opcode::Get && reqExt->req.a_param == 0x0) {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAckData;
                SC_REPORT_INFO("L2_Cache_Dispatch", "进入分支：读取 (Get)");

                // L2 层做虚拟地址到物理地址的翻译
                uint32_t vaddr_block = reqExt->req.a_address;
                uint32_t paddr_block = m_mmu->translate(reqExt->req.a_pagetable_root, vaddr_block);

                // Debug: Check translation for LW instruction at 0x800002c0
                if (reqExt->req.a_pc == 0x800002c0) {
                    std::cout << "[L2_Cache::process_queue] LW @ pc=0x800002c0: ptroot=0x"
                              << std::hex << reqExt->req.a_pagetable_root << ", vaddr_block=0x"
                              << vaddr_block << ", paddr_block=0x" << paddr_block << std::dec
                              << std::endl;
                }

                if (paddr_block == 0) {
                    char msg[256];
                    std::sprintf(
                        msg, "MMU translate failed: ptroot=0x%x vaddr=0x%x",
                        reqExt->req.a_pagetable_root, vaddr_block
                    );
                    SC_REPORT_ERROR("L2_Cache", msg);
                    continue; // 跳过这个请求
                }

                // 创建 cmd 对象
                auto cmd = std::make_unique<lsu_mem_cmd_t>();
                for (int i = 0; i < hw_num_thread; ++i)

                    cmd->opcode = L1D_OPCODE_READ;
                cmd->pagetable_root = reqExt->req.a_pagetable_root;
                cmd->warp_id = reqExt->req.a_source; // 来源 L1
                cmd->instrId = reqExt->req.a_instrId;
                cmd->instr.currentpc = reqExt->req.a_pc;
                // 从物理地址计算 cache_tag 和 cache_setIdx
                cmd->cache_tag
                    = (paddr_block >> (log2Ceil(L1D_BLOCK_NUM_WORD * 4) + log2Ceil(L1D_NUM_SET)));
                cmd->cache_setIdx = (paddr_block >> log2Ceil(L1D_BLOCK_NUM_WORD * 4))
                    & ((1 << log2Ceil(L1D_NUM_SET)) - 1);
                auto bo = std::make_shared<std::array<uint8_t, hw_num_thread>>();
                auto wo = std::make_shared<std::array<uint8_t, hw_num_thread>>();
                // L2 cache需要读取整个cache line，所以让前LINEWORDS个thread读取不同的word
                // 这样ramulator会读取所有word，然后我们可以构建完整的cache line响应
                for (int i = 0; i < hw_num_thread; ++i) {
                    if (i < cache_building_block::LINEWORDS) {
                        (*bo)[i] = i; // 每个thread读取不同的word（0, 1, 2, 3...）
                        (*wo)[i] = 1; // wordOffset1H=1表示读取整个word
                        cmd->mask[i] = true;
                    } else {
                        (*bo)[i] = 0;         // 超出LINEWORDS的thread设为0
                        (*wo)[i] = 0;         // 不读取
                        cmd->mask[i] = false; // 不读取
                    }
                }
                cmd->blockOffset = bo;
                cmd->wordOffset1H = wo;

                // 将 trans 指针和 l1id 缓存到本地变量
                auto trans_copy = trans;
                auto l1id_copy = l1id;

                // 发送到 ramulator
                //  保存 reqExt 的值到局部变量，避免 lambda 捕获指针时的问题
                uint32_t saved_source = reqExt->req.a_source;
                uint32_t saved_instrId = reqExt->req.a_instrId;
                uint32_t saved_pc = reqExt->req.a_pc;

                std::cout << "[L2_Cache::process_queue] Sending request to Ramulator: saved_pc=0x"
                          << std::hex << saved_pc << std::dec << ", saved_source=0x" << std::hex
                          << saved_source << std::dec << ", l1id=" << l1id_copy << " @ "
                          << sc_time_stamp() << std::endl;
                int ret = ramulator->request(
                    saved_source, cmd,
                    [=, this](std::unique_ptr<lsu_mem_cmd_t> ret_cmd) mutable {
                        std::cout << "[L2_Cache::ramulator_callback] CALLED: saved_pc=0x"
                                  << std::hex << saved_pc << std::dec << ", saved_source=0x"
                                  << std::hex << saved_source << std::dec << ", l1id=" << l1id_copy
                                  << " @ " << sc_time_stamp() << std::endl;
                        auto* rspExt = new L2MemRspExtension;
                        rspExt->rsp.d_source = saved_source;
                        rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAckData;
                        rspExt->rsp.d_instrId = saved_instrId;
                        rspExt->rsp.d_pc = saved_pc;
                        // Initialize d_data to 0
                        rspExt->rsp.d_data.fill(0);
                        // Copy data from ramulator response to L2 response
                        // ret_cmd->data[i] is indexed by thread, but we need to map it to word
                        // index based on blockOffset
                        for (int i = 0; i < hw_num_thread; ++i) {
                            if (ret_cmd->mask[i]) {
                                uint8_t blockOffset = ret_cmd->blockOffset->at(i);
                                rspExt->rsp.d_data[blockOffset] = ret_cmd->data[i];
                                std::cout << "[L2_Cache::ramulator_callback] thread=" << i
                                          << " blockOffset=" << static_cast<int>(blockOffset)
                                          << " data=0x" << std::hex << ret_cmd->data[i] << std::dec
                                          << " -> d_data[" << static_cast<int>(blockOffset)
                                          << "]=0x" << std::hex << rspExt->rsp.d_data[blockOffset]
                                          << std::dec << std::endl;
                            }
                        }
                        rspExt->rsp.d_mask.fill(true);
                        trans_copy->set_extension(rspExt);

                        std::cout << "[L2_Cache::ramulator_callback] About to call "
                                     "nb_transport_bw: d_source=0x"
                                  << std::hex << rspExt->rsp.d_source << std::dec << ", d_pc=0x"
                                  << std::hex << rspExt->rsp.d_pc << std::dec
                                  << ", l1id=" << l1id_copy << " @ " << sc_time_stamp()
                                  << std::endl;
                        // 调用 nb_transport_bw 将响应发送回 L1
                        tlm::tlm_phase phase = tlm::BEGIN_RESP;
                        sc_time delay = SC_ZERO_TIME;
                        auto status
                            = target_socket[l1id_copy]->nb_transport_bw(*trans_copy, phase, delay);
                        trans_copy->release_extension(rspExt);
                        // 如果返回 TLM_UPDATED，需要处理 phase（这里简单忽略）
                    }
                );

                return_mask.fill(true); // 原来的代码
                continue;               // 等待下级回调，在回调函数内部响应L1
            } else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutFullData) {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAck;
                // 正确设置响应字段
                rspExt->rsp.d_source = reqExt->req.a_source; // wshr_idx
                rspExt->rsp.d_pc = reqExt->req.a_pc;
                rspExt->rsp.d_instrId = reqExt->req.a_instrId;
                SC_REPORT_INFO("L2_Cache_Dispatch", "进入分支：putfulldata");

                // L2 层做虚拟地址到物理地址的翻译
                uint32_t vaddr_block = reqExt->req.a_address;
                uint32_t paddr_block = m_mmu->translate(reqExt->req.a_pagetable_root, vaddr_block);

                if (paddr_block == 0) {
                    char msg[256];
                    std::sprintf(
                        msg, "MMU translate failed: ptroot=0x%x vaddr=0x%x",
                        reqExt->req.a_pagetable_root, vaddr_block
                    );
                    SC_REPORT_ERROR("L2_Cache", msg);
                    continue; // 跳过这个请求
                }

                auto cmd = std::make_unique<lsu_mem_cmd_t>();
                cmd->opcode = L1D_OPCODE_WRITE;
                cmd->pagetable_root = reqExt->req.a_pagetable_root;
                cmd->warp_id = reqExt->req.a_source;
                cmd->instr.d = 0;
                cmd->instrId = reqExt->req.a_instrId;
                cmd->instr.currentpc = reqExt->req.a_pc;
                // 从物理地址计算 cache_tag 和 cache_setIdx
                cmd->cache_tag
                    = (paddr_block >> (log2Ceil(L1D_BLOCK_NUM_WORD * 4) + log2Ceil(L1D_NUM_SET)));
                cmd->cache_setIdx = (paddr_block >> log2Ceil(L1D_BLOCK_NUM_WORD * 4))
                    & ((1 << log2Ceil(L1D_NUM_SET)) - 1);

                // 构造 blockOffset、wordOffset1H、mask
                auto bo = std::make_shared<std::array<uint8_t, hw_num_thread>>();
                auto wo = std::make_shared<std::array<uint8_t, hw_num_thread>>();
                sc_dt::sc_bv<hw_num_thread> bv;

                for (int i = 0; i < hw_num_thread; ++i) {
                    (*bo)[i] = 0;
                    (*wo)[i] = 1; // 全写
                    cmd->data[i] = reqExt->req.a_data[i];
                    if (i == 0)
                        cmd->mask[i] = true;
                    else
                        cmd->mask[i] = false;
                }

                cmd->blockOffset
                    = std::static_pointer_cast<const std::array<uint8_t, hw_num_thread>>(bo);
                cmd->wordOffset1H
                    = std::static_pointer_cast<const std::array<uint8_t, hw_num_thread>>(wo);

                // 写操作：不需要回调，但可选回调可以用于通知释放资源
                // int ret = ramulator->request(reqExt->req.a_source, cmd, nullptr);
                rspExt->rsp.d_mask = reqExt->req.a_mask;

                // 立即发送响应（写操作不需要等待ramulator完成）
                trans->set_extension(rspExt);
                tlm::tlm_phase phase = tlm::BEGIN_RESP;
                sc_time delay = SC_ZERO_TIME;
                auto status = target_socket[l1id]->nb_transport_bw(*trans, phase, delay);
                trans->release_extension(rspExt);
                response_sent = true;
                continue; // 响应已发送，不再执行后续代码
            } else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutPartialData && reqExt->req.a_param == 0x0) {
                SC_REPORT_INFO("L2_Cache_Dispatch", "进入分支：putpartialdata");

                // L2 层做虚拟地址到物理地址的翻译
                uint32_t vaddr_block = reqExt->req.a_address;
                uint32_t paddr_block = m_mmu->translate(reqExt->req.a_pagetable_root, vaddr_block);

                if (paddr_block == 0) {
                    char msg[256];
                    std::sprintf(
                        msg, "MMU translate failed: ptroot=0x%x vaddr=0x%x",
                        reqExt->req.a_pagetable_root, vaddr_block
                    );
                    SC_REPORT_ERROR("L2_Cache", msg);
                    continue; // 跳过这个请求
                }

                // === 创建 lsu_mem_cmd_t 写请求 ===
                auto cmd = std::make_unique<lsu_mem_cmd_t>();

                cmd->opcode = L1D_OPCODE_WRITE;
                cmd->pagetable_root = reqExt->req.a_pagetable_root;
                cmd->warp_id = reqExt->req.a_source;
                cmd->instrId = reqExt->req.a_instrId;
                cmd->instr.currentpc = reqExt->req.a_pc;
                // 从物理地址计算 cache_tag 和 setIdx
                cmd->cache_tag
                    = (paddr_block >> (log2Ceil(L1D_BLOCK_NUM_WORD * 4) + log2Ceil(L1D_NUM_SET)));
                cmd->cache_setIdx = (paddr_block >> log2Ceil(L1D_BLOCK_NUM_WORD * 4))
                    & ((1 << log2Ceil(L1D_NUM_SET)) - 1);
                // 设置数据
                const auto& data = reqExt->req.a_data;
                const auto& mask = reqExt->req.a_mask;
                for (int i = 0; i < hw_num_thread; ++i) {
                    cmd->data[i] = data[i];
                    cmd->mask[i] = mask[i];
                    // sc_bv<> 可按位赋值
                }
                // blockOffset 和wordOffset1H
                auto bo = std::make_shared<std::array<uint8_t, hw_num_thread>>();
                auto wo = std::make_shared<std::array<uint8_t, hw_num_thread>>();
                for (int i = 0; i < hw_num_thread; ++i) {
                    (*bo)[i] = 0;
                    // 默认从 offset=0 写
                    (*wo)[i] = 0xF;
                    // 全写（00001111b）
                }
                cmd->blockOffset
                    = std::static_pointer_cast<const std::array<uint8_t, hw_num_thread>>(bo);
                cmd->wordOffset1H
                    = std::static_pointer_cast<const std::array<uint8_t, hw_num_thread>>(wo);
                // 发给 Ramulator，写不需要回调
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAck;
                rspExt->rsp.d_source = reqExt->req.a_source; // wshr_idx
                rspExt->rsp.d_pc = reqExt->req.a_pc;
                rspExt->rsp.d_instrId = reqExt->req.a_instrId;
                rspExt->rsp.d_mask = reqExt->req.a_mask;

                // 调试：记录 PutPartialData 响应发送
                static int putpartial_rsp_count = 0;
                if (++putpartial_rsp_count <= 100
                    || (reqExt->req.a_pc >= 0x80000088 && reqExt->req.a_pc <= 0x800000c0)) {
                    std::cout << "[L2_PutPartialData_RSP] Sending response: pc=0x" << std::hex
                              << reqExt->req.a_pc << std::dec
                              << " d_source=" << rspExt->rsp.d_source << " d_pc=0x" << std::hex
                              << rspExt->rsp.d_pc << std::dec
                              << " d_instrId=" << rspExt->rsp.d_instrId << " l1id=" << l1id << " @ "
                              << sc_time_stamp() << std::endl;
                }

                // 立即发送响应（写操作不需要等待ramulator完成）
                trans->set_extension(rspExt);
                tlm::tlm_phase phase = tlm::BEGIN_RESP;
                sc_time delay = SC_ZERO_TIME;
                auto status = target_socket[l1id]->nb_transport_bw(*trans, phase, delay);
                trans->release_extension(rspExt);
                response_sent = true;
                continue; // 响应已发送，不再执行后续代码
            } else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutPartialData && reqExt->req.a_param == 0x1) { // SC
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAckData;
                SC_REPORT_INFO("L2_Cache_Dispatch", "SC:PutPartialData");
                return_mask[0] = true;
                reqExt->req.a_data[0] = 0x0; // 0x0表示成功，0x1表示失败
            } else if (reqExt->req.a_opcode == TL_UH_A_opcode::Get && reqExt->req.a_param == 0x1) {
                // LR
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAckData;
                SC_REPORT_INFO("L2_Cache_Dispatch", "LR");
                // 虚拟地址到物理地址翻译
                uint32_t vaddr = reqExt->req.a_address;
                uint32_t paddr = m_mmu->translate(reqExt->req.a_pagetable_root, vaddr);
                if (paddr == 0) {
                    char msg[256];
                    std::sprintf(
                        msg, "MMU translate failed: ptroot=0x%x vaddr=0x%x",
                        reqExt->req.a_pagetable_root, vaddr
                    );
                    SC_REPORT_ERROR("L2_Cache", msg);
                    continue;
                }
                lr_reservation = std::make_pair(paddr, l1id);
                auto mem_stat
                    = m_mem->read(paddr, rspExt->rsp.d_data.data(), cache_building_block::LINESIZE);
                if (mem_stat != 0) {
                    SC_REPORT_ERROR("L2_Cache_TLM", "Failed to read data");
                }
                return_mask.fill(true);
            } else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutPartialData && reqExt->req.a_param >= 0x2) {
                // AMO
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAckData;
                // 虚拟地址到物理地址翻译
                uint32_t vaddr = reqExt->req.a_address;
                uint32_t paddr = m_mmu->translate(reqExt->req.a_pagetable_root, vaddr);
                if (paddr == 0) {
                    char msg[256];
                    std::sprintf(
                        msg, "MMU translate failed: ptroot=0x%x vaddr=0x%x",
                        reqExt->req.a_pagetable_root, vaddr
                    );
                    SC_REPORT_ERROR("L2_Cache", msg);
                    continue;
                }
                uint32_t old_data = 0;
                m_mem->read(paddr, &old_data, sizeof(uint32_t));
                rspExt->rsp.d_data.fill(old_data); // 返回旧值作为 AMO 的返回

                uint32_t result = old_data;
                uint32_t operand = reqExt->req.a_data[0];

                switch (reqExt->req.a_param) {
                case 0x2:
                    result = old_data + operand;
                    break; // AMOADD
                case 0x3:
                    result = old_data ^ operand;
                    break; // AMOXOR
                case 0x4:
                    result = std::min(old_data, operand);
                    break; // AMOMIN
                case 0x5:
                    result = std::max(old_data, operand);
                    break; // AMOMAX
                default:
                    SC_REPORT_WARNING("L2_Cache_TLM", "Unknown AMO param, fallback to AMOADD");
                    result = old_data + operand;
                    break;
                }

                m_mem->write(paddr, &result, sizeof(uint32_t));
                return_mask.fill(true);
            } else {
                SC_REPORT_ERROR("L2_Cache_TLM_Rsp", "Unsupported a_opcode");
            }

            // 如果响应还未发送，则发送响应（用于其他类型的请求，如SC、LR、AMO等）
            if (!response_sent) {
                rspExt->rsp.d_mask = return_mask;

                // 4) attach 到 trans
                trans->set_extension(rspExt);

                // 5) 调用 nb_transport_bw() 返回
                tlm::tlm_phase phase = tlm::BEGIN_RESP;
                sc_time delay = SC_ZERO_TIME;
                auto status = target_socket[l1id]->nb_transport_bw(*trans, phase, delay);
                trans->release_extension(rspExt);
                // 如果返回 TLM_UPDATED，需要处理 phase
                // 这里简单忽略
            }
        }
    }
}