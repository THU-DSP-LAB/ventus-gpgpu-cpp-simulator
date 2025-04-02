#include "l2_tlm.hpp"

tlm::tlm_sync_enum L2_Cache::nb_transport_fw(tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
                                             sc_core::sc_time &delay)
{
    if (phase == tlm::BEGIN_REQ)
    {
        // 1) 把 transaction 存到队列
        req_queue.push_back(&trans);

        // 2) 返回 TLM_ACCEPTED or TLM_UPDATED
        //    这里简单返回 TLM_ACCEPTED
        return tlm::TLM_ACCEPTED;
    }
    else if (phase == tlm::END_RESP)
    {
        // Initiator 通知 Response 结束
        // 常规 4-phase 协议时会来这里
        return tlm::TLM_ACCEPTED;
    }
    // 其他 phase 忽略
    return tlm::TLM_ACCEPTED;
}

void L2_Cache::process_queue()
{
    while (true)
    {
        wait(PERIOD, sc_core::SC_NS); // every cycle

        if (!req_queue.empty())
        {
            tlm::tlm_generic_payload *trans = req_queue.front();
            req_queue.pop_front();

            // 1) 先解析请求 extension
            auto *reqExt = dynamic_cast<DCacheMemReqExtension *>(trans->get_extension<DCacheMemReqExtension>());
            if (!reqExt)
            {
                SC_REPORT_ERROR("L2_Cache", "No extension found in trans!");
                continue;
            }

            // ============ 多阶段模拟 =============
            // 2) 检查是否命中
            bool is_hit = false; // 简化，默认 miss
            if (is_hit)
            {
                wait(10, sc_core::SC_NS);
            }
            else
            {
                wait(50, sc_core::SC_NS);
            }

            // 3) 构造响应 extension
            auto *rspExt = new L2MemRspExtension;
            rspExt->rsp.d_source = reqExt ? reqExt->req.a_source : 0;
            std::array<bool, cache_building_block::LINEWORDS> return_mask{};

            if (reqExt->req.a_opcode == TL_UH_A_opcode::Get && reqExt->req.a_param == 0x0)
            {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAckData;
                // 现在虚拟地址编号设置为 0，将来能够传 asid 之后加上
                auto mem_stat = m_mem->readDataVirtual(0, reqExt->req.a_address, cache_building_block::LINESIZE,
                                                       rspExt->rsp.d_data.data());
                if (mem_stat != 0)
                {
                    SC_REPORT_ERROR("L2_Cache_TLM", "Failed to read data");
                }
                return_mask.fill(true);
            }
            else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutFullData)
            {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAck;

                auto mem_stat = m_mem->writeDataVirtual(0, reqExt->req.a_address, cache_building_block::LINESIZE,
                                                        reinterpret_cast<const uint8_t *>(reqExt->req.a_data.data()));
                if (mem_stat != 0)
                {
                    SC_REPORT_ERROR("L2_Cache_TLM", "Failed to write data");
                }
            }
            else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutPartialData && reqExt->req.a_param == 0x0)
            {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAck;

                const auto &mask = reqExt->req.a_mask;
                const auto &data = reqExt->req.a_data;
                size_t nwords = mask.size();

                // 遍历每个 word，尽量合并连续为 true 的 mask 位
                for (size_t i = 0; i < nwords;)
                {
                    if (!mask[i])
                    {
                        ++i;
                        continue;
                    }
                    // 找出从 i 开始连续为 true 的区间 [i, j)
                    size_t j = i;
                    while (j < nwords && mask[j])
                    {
                        ++j;
                    }
                    // 计算该连续区间对应的起始地址和字节数
                    uint64_t addr = reqExt->req.a_address + i * sizeof(uint32_t);
                    uint64_t size = (j - i) * sizeof(uint32_t);
                    auto mem_stat = m_mem->writeDataVirtual(
                        0, addr, size, reinterpret_cast<const uint8_t *>(data.data()) + i * sizeof(uint32_t));
                    if (mem_stat != 0)
                    {
                        SC_REPORT_ERROR("L2_Cache_TLM", "Failed to write data");
                    }
                    i = j;
                }
            }
            else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutPartialData && reqExt->req.a_param == 0x1)
            { // SC
                return_mask[0] = true;
                reqExt->req.a_data[0] = 0x0; // 0x0表示成功，0x1表示失败
            }

            else
            {
                SC_REPORT_ERROR("L2_Cache_TLM", "Unsupported a_opcode");
            }

            rspExt->rsp.d_mask = return_mask;

            // 4) attach 到 trans
            trans->set_extension(rspExt);

            // 5) 调用 nb_transport_bw() 返回
            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_time delay = SC_ZERO_TIME;
            auto status = target_socket->nb_transport_bw(*trans, phase, delay);
            // 如果返回 TLM_UPDATED，需要处理 phase
            // 这里简单忽略
        }
    }
}
