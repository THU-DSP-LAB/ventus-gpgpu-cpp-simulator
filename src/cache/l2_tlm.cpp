#include "l2_tlm.hpp"

tlm::tlm_sync_enum L2_Cache::nb_transport_fw(tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
    sc_core::sc_time &delay)
{
    if (phase == tlm::BEGIN_REQ)
    {
        // 1) 把 transaction 存到队列
        req_queue.push_back(&trans);
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

            char log_buf[128];
            snprintf(log_buf, 128, "Physical memory access addr=0x%x", reqExt->req.a_address);
            SC_REPORT_INFO("L2_Cache_TLM_Req", log_buf);

            if (reqExt->req.a_opcode == TL_UH_A_opcode::Get && reqExt->req.a_param == 0x0)
            {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAckData;
                SC_REPORT_INFO("L2_Cache_Dispatch", "进入分支：读取 (Get)");
                // 现在虚拟地址编号设置为 0，将来能够传 asid 之后加上
                auto mem_stat = m_mem->read(
                    reqExt->req.a_address, rspExt->rsp.d_data.data(), cache_building_block::LINESIZE
                );
                if (mem_stat != 0)
                {
                    SC_REPORT_ERROR("L2_Cache_TLM", "Failed to read data");
                }
                return_mask.fill(true);
            }
            else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutFullData)
            {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAck;
                SC_REPORT_INFO("L2_Cache_Dispatch", "进入分支：putfulldata");
                auto mem_stat = m_mem->write(
                    reqExt->req.a_address, reqExt->req.a_data.data(), cache_building_block::LINESIZE
                );

                if (mem_stat != 0)
                {
                    SC_REPORT_ERROR("L2_Cache_TLM", "Failed to write data");
                }
            }
            else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutPartialData && reqExt->req.a_param == 0x0)
            {
                rspExt->rsp.d_opcode = TL_UH_D_opcode::AccessAck;
                SC_REPORT_INFO("L2_Cache_Dispatch", "进入分支：PutPartialData");
                const auto &mask = reqExt->req.a_mask;
                const auto &data = reqExt->req.a_data;

                for (size_t i = 0; i < mask.size(); ++i) {
                    if (mask[i]) {
                        uint32_t addr = reqExt->req.a_address + i * sizeof(uint32_t);
                        SC_REPORT_INFO("L2_Cache_TLM_Debug", log_buf);
                        int mem_stat = m_mem->write(addr, &data[i], sizeof(uint32_t));
                        if (mem_stat != 0) {
                            SC_REPORT_ERROR("L2_Cache_TLM", "Failed to write partial word");
                        }
                    }
                }

                // 设置响应mask为实际写入的mask
                rspExt->rsp.d_mask = reqExt->req.a_mask;
            }
            else if (reqExt->req.a_opcode == TL_UH_A_opcode::PutPartialData && reqExt->req.a_param == 0x1)
            { // SC
                SC_REPORT_INFO("L2_Cache_Dispatch", "PutPartialData");
                return_mask[0] = true;
                reqExt->req.a_data[0] = 0x0; // 0x0表示成功，0x1表示失败
            } 
            
            else 
            {
                SC_REPORT_ERROR("L2_Cache_TLM_Rsp", "Unsupported a_opcode");
            }

            rspExt->rsp.d_mask = return_mask;

            // 4) attach 到 trans
            trans->set_extension(rspExt);
            //******************************************
            // 打印响应字段信息
            snprintf(
                log_buf, 128, "d_opcode=%d, d_source=%d", static_cast<int>(rspExt->rsp.d_opcode),
                rspExt->rsp.d_source
            );
            SC_REPORT_INFO("L2_Cache_TLM_Rsp", log_buf);

            // 打印返回 mask（d_mask）
            std::stringstream rsp_mask_ss;
            rsp_mask_ss << "d_mask = [ ";
            for (bool m : rspExt->rsp.d_mask)
                rsp_mask_ss << m << " ";
            rsp_mask_ss << "]";
            SC_REPORT_INFO("L2_Cache_TLM_Rsp", rsp_mask_ss.str().c_str());

            // 打印返回数据（d_data）
            std::stringstream rsp_data_ss;
            rsp_data_ss << "d_data = [ ";
            for (auto word : rspExt->rsp.d_data)
                rsp_data_ss << std::hex << "0x" << word << " ";
            rsp_data_ss << "]";
            SC_REPORT_INFO("L2_Cache_TLM_Rsp", rsp_data_ss.str().c_str());

            //*************************************
            // 5) 调用 nb_transport_bw() 返回
            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_time delay = SC_ZERO_TIME;
            auto status = target_socket->nb_transport_bw(*trans, phase, delay);
            // 如果返回 TLM_UPDATED，需要处理 phase
            // 这里简单忽略
        }
    }
}
