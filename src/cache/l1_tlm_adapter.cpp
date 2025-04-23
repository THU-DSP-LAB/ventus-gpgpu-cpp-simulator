#include "l1_tlm_adapter.hpp"

void L1_TLM_Adapter::send_req_thread()
{
    while (true)
    {
        wait(PERIOD, sc_core::SC_NS);
        // 1) 从 L1 读取请求
        dcache_2_L2_memReq req = memReq_in.read();

        // 2) 创建 TLM transaction
        tlm::tlm_generic_payload *trans = new tlm::tlm_generic_payload;

        // 给 transaction 绑定一个 extension 存储这个请求信息
        auto *ext = new DCacheMemReqExtension;
        ext->req = req; // 拷贝
        trans->set_extension(ext);

        // command: currently not include atomic, invalidate, flush, etc.
        trans->set_command(
            req.a_opcode == TL_UH_A_opcode::Get ? tlm::TLM_READ_COMMAND
            : (req.a_opcode == TL_UH_A_opcode::PutFullData || req.a_opcode == TL_UH_A_opcode::PutPartialData)
                ? tlm::TLM_WRITE_COMMAND
                : tlm::TLM_IGNORE_COMMAND);
        trans->set_address(req.a_address);
        // 以下为对写入错误的调试
        if (req.a_opcode == TL_UH_A_opcode::PutFullData
            || req.a_opcode == TL_UH_A_opcode::PutPartialData) {
            // 分配并复制数据
            auto* data_ptr = new uint8_t[req.a_data.size() * sizeof(uint32_t)];
            std::memcpy(data_ptr, req.a_data.data(), req.a_data.size() * sizeof(uint32_t));

            trans->set_data_ptr(data_ptr);
            trans->set_data_length(req.a_data.size() * sizeof(uint32_t));
            trans->set_streaming_width(req.a_data.size() * sizeof(uint32_t));
            trans->set_byte_enable_ptr(nullptr); // 暂不启用 byte mask
            trans->set_dmi_allowed(false);       // 禁用 DMI
            std::cout << "[Adapter] Sending ";
            if (req.a_opcode == TL_UH_A_opcode::PutFullData
                || req.a_opcode == TL_UH_A_opcode::PutPartialData) {
                std::cout << "STORE to addr 0x" << std::hex << req.a_address << " with data = ";
                for (auto val : req.a_data)
                    std::cout << "0x" << std::hex << val << " ";
                std::cout << std::endl;
            }
        }
        // 3) 记录到 inflight_map
        // 因为一般由 Initiator 端管理每个事务的状态
        inflight_map[req.a_source] = trans;

        // 4) 发起 nb_transport_fw
        tlm::tlm_phase phase = tlm::BEGIN_REQ;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        tlm::tlm_sync_enum status = initiator_socket->nb_transport_fw(*trans, phase, delay);
        switch (status) 
        {
        case tlm::TLM_ACCEPTED:
        case tlm::TLM_UPDATED:
            // 对方(Target)接受了请求
            break;
        case tlm::TLM_COMPLETED:
            // 目标端可能立即完成了 (罕见)
            break;
        default:
            break;
        }
    }
}

tlm::tlm_sync_enum L1_TLM_Adapter::nb_transport_bw(tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
    sc_core::sc_time &delay)
{
    if (phase == tlm::BEGIN_RESP)
    {
        // 1) 提取响应 extension
        auto *rspExt = trans.get_extension<L2MemRspExtension>();
        if (!rspExt)
        {
            SC_REPORT_ERROR("L1_TLM_Adapter", "No DCacheMemReqExtension in trans!");
        }

        // 2) 从 inflight_map 找到 source_id
        uint32_t key = rspExt->rsp.d_source;
        auto it = inflight_map.find(key);
        if (it == inflight_map.end())
         {
            SC_REPORT_ERROR("L1_TLM_Adapter", "Unknown transaction in inflight_map!");
        }

        // 3) 构造 L2_2_dcache_memRsp
        L2_2_dcache_memRsp final_rsp = rspExt->rsp;

        // 4) 写出到 FIFO，让 L1 收到
        memRsp_out.write(final_rsp);

        // 移除 in-flight
        inflight_map.erase(it);

        // 5) 回复 TLM_UPDATED 并将 phase 置为 END_RESP
        phase = tlm::END_RESP;
        return tlm::TLM_UPDATED;
    } 
    else if (phase == tlm::END_REQ)
     {
        // 表示 Target 端对 BEGIN_REQ 的确认
        return tlm::TLM_ACCEPTED;
    }
    // 其他情况
    return tlm::TLM_ACCEPTED;
}