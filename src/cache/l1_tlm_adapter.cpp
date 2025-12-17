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
            // std::cout << "[Adapter] Sending ";
            // if (req.a_opcode == TL_UH_A_opcode::PutFullData
            //     || req.a_opcode == TL_UH_A_opcode::PutPartialData) {
            //     std::cout << "STORE to addr 0x" << std::hex << req.a_address << " with data = ";
            //     for (auto val : req.a_data)
            //         std::cout << "0x" << std::hex << val << " ";
            //     std::cout << std::endl;
            // }
        }
        // 3) 记录到 inflight_map
        // 因为一般由 Initiator 端管理每个事务的状态
        // 生成唯一的 adapter 内部 request ID（替代 req.a_source）
        uint32_t adapter_req_id = unique_req_id_counter++;
        adapter_source_map[adapter_req_id] = req.a_source;

        // 更新 extension 中的 req 的 source 字段（用于响应匹配）
        ext->req.a_source = adapter_req_id;

        // 存入映射
        inflight_map[adapter_req_id] = trans;
        
        // Debug: Print when adding to inflight_map
        static int send_req_count = 0;
        send_req_count++;
        // std::cout << "[L1_TLM_Adapter::send_req_thread] #" << send_req_count 
        //           << " Added to inflight_map: adapter_req_id=0x" << std::hex << adapter_req_id << std::dec
        //           << ", original a_source=0x" << std::hex << req.a_source << std::dec
        //           << ", pc=0x" << std::hex << req.a_pc << std::dec
        //           << ", a_opcode=" << (req.a_opcode == TL_UH_A_opcode::Get ? "Get" : "Other")
        //           << ", inflight_map.size()=" << inflight_map.size()
        //           << " @ " << sc_time_stamp() << std::endl;

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
        
        // Debug: Print key and inflight_map size
        // static int debug_count = 0;
        // debug_count++;
        // std::cout << "[L1_TLM_Adapter::nb_transport_bw] #" << debug_count
        //           << " Looking for key=0x" << std::hex << key << std::dec
        //           << ", inflight_map.size()=" << inflight_map.size()
        //           << " @ " << sc_time_stamp() << std::endl;
        // if (!inflight_map.empty()) {
        //     std::cout << "  Available keys in inflight_map: ";
        //     for (const auto& pair : inflight_map) {
        //         std::cout << "0x" << std::hex << pair.first << std::dec << " ";
        //     }
        //     std::cout << std::endl;
        // }
        
        auto it = inflight_map.find(key);
        if (it == inflight_map.end())
         {
            // std::cout << "[L1_TLM_Adapter::nb_transport_bw] ERROR: key=0x" << std::hex << key << std::dec 
            //           << " not found in inflight_map (size=" << inflight_map.size() << ")" << std::endl;
            SC_REPORT_ERROR("L1_TLM_Adapter", "Unknown transaction in inflight_map!");
        }
        auto orig_source_it = adapter_source_map.find(key);
        uint32_t original_d_source = rspExt->rsp.d_source;  // 保存原始值用于调试
        if (orig_source_it != adapter_source_map.end()) {
            rspExt->rsp.d_source = orig_source_it->second;
            // std::cout << "[L1_TLM_Adapter::nb_transport_bw] Restored d_source: key=0x" << std::hex << key 
            //           << " -> original d_source=0x" << original_d_source << " -> restored d_source=0x" 
            //           << rspExt->rsp.d_source << std::dec << std::endl;
            // 注意：先不删除映射，等确认响应有效后再删除
        } else {
            // std::cout << "[L1_TLM_Adapter::nb_transport_bw] WARNING: No adapter_source_map entry for key=0x" 
            //           << std::hex << key << std::dec << ", d_source remains 0x" << std::hex 
            //           << rspExt->rsp.d_source << std::dec << std::endl;
        }
        // 3) 构造 L2_2_dcache_memRsp
        L2_2_dcache_memRsp final_rsp = rspExt->rsp;

        // 调试：记录 store 响应（AccessAck 表示 store）
        static int adapter_rsp_count = 0;
        bool is_store = (final_rsp.d_opcode == TL_UH_D_opcode::AccessAck);
        if (is_store && (++adapter_rsp_count <= 100 || (final_rsp.d_pc >= 0x80000088 && final_rsp.d_pc <= 0x800000c0))) {
            std::cout << "[L1_TLM_Adapter::nb_transport_bw] Store response: pc=0x" << std::hex << final_rsp.d_pc << std::dec
                      << " d_source=" << final_rsp.d_source
                      << " d_instrId=" << final_rsp.d_instrId
                      << " key=0x" << std::hex << key << std::dec
                      << " @ " << sc_time_stamp() << std::endl;
        }

        // 检查响应pc是否有效（避免处理未初始化的响应）
        // 0x5555 看起来像未初始化的内存值（0x55555555的低16位）
        if (final_rsp.d_pc == 0x5555 || final_rsp.d_pc == 0x0) {
            std::cout << "[L1_TLM_Adapter::nb_transport_bw] WARNING: Invalid pc=0x" << std::hex << final_rsp.d_pc 
                      << std::dec << ", ignoring this response. key=0x" << std::hex << key << std::dec << std::endl;
            // 不处理这个响应，保留inflight_map和adapter_source_map中的条目，以便后续正确响应能够使用
            return tlm::TLM_COMPLETED;
        }
        
        // 响应有效，现在可以安全地清理映射
        if (orig_source_it != adapter_source_map.end()) {
            adapter_source_map.erase(orig_source_it); // 清理映射
        }

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