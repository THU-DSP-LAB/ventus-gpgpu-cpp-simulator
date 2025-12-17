#include "L1D_Cache_System.hpp"
#include "../parameters.h"  // 用于 L1D_NUM_SET 和 L1D_BLOCK_NUM_WORD
#define NUM_L1 4

static sc_core::sc_trace_file *tf = nullptr;
L1D_Cache_System::L1D_Cache_System(
    sc_core::sc_module_name name,
    int l1_id,
    L2_Cache& l2_ref,
    std::shared_ptr<PhysicalMemoryInterface> pmem_ptr
)
    : sc_core::sc_module(name),clk("clk"), l2(l2_ref)
{
    // 创建子模块
    l1 = new SC_L1_CACHE((std::string("l1_") + std::to_string(l1_id)).c_str());
    adapter = new L1_TLM_Adapter((std::string("adapter_") + std::to_string(l1_id)).c_str());

    // 绑定内部 FIFO 与 TLM 适配器
    l1->dcache_2_L2_memReq_port(fifo_l1_to_adapter);
    l1->L2_2_dcache_memRsp_port(fifo_adapter_to_l1);
    l1->LSU_2_dcache_coreReq_port(internal_fifo_lsu_2_l1d);
    l1->dcache_2_LSU_coreRsp_port(internal_fifo_l1d_2_lsu);
    l1->clk(clk);

    adapter->memReq_in(fifo_l1_to_adapter);
    adapter->memRsp_out(fifo_adapter_to_l1);
    adapter->initiator_socket.bind(l2.target_socket);

    SC_HAS_PROCESS(L1D_Cache_System);
    // 请求由 accept() 直接写入 internal_fifo_lsu_2_l1d，这里只需要响应线程
    SC_THREAD(forward_rsp);
    if (tf == nullptr) {
        tf = sc_create_vcd_trace_file("l1d_cache_system");
        tf->set_time_unit(1, sc_core::SC_NS);

        // 例子：trace 一些关键信号
        sc_trace(tf, clk, "clk");
        sc_trace(tf, fifo_l1_to_adapter.num_available(), "fifo_l1_to_adapter_avail");
        sc_trace(tf, fifo_adapter_to_l1.num_available(), "fifo_adapter_to_l1_avail");
        sc_trace(tf, internal_fifo_lsu_2_l1d.num_available(), "fifo_lsu_to_l1d_avail");
        sc_trace(tf, internal_fifo_l1d_2_lsu.num_available(), "fifo_l1d_to_lsu_avail");
    }
    // std::cout << "[L1D_Cache_System] forward_rsp 线程注册完成" << std::endl;
}

int L1D_Cache_System::get_l1_hit_count() const {
    return l1->get_hit_count();
}

int L1D_Cache_System::get_l1_miss_count() const {
    return l1->get_miss_count();
}

int L1D_Cache_System::accept(
    std::unique_ptr<lsu_mem_cmd_t>& cmd,
    std::function<void(std::unique_ptr<lsu_mem_cmd_t>)> callback
) {
    LSU_2_dcache_coreReq req;

    // 设置请求类型：读或写
    req.m_opcode = (cmd->opcode == L1D_OPCODE_READ)
        ? LSU_cache_coreReq_opcode::Read
        : LSU_cache_coreReq_opcode::Write;

    // 计算 block 索引（假设所有 lane 地址位于同一 block）
    // 使用系统参数而不是硬编码值，确保与 exec_lsu.cpp 和 l2_tlm.cpp 一致
    uint32_t addr_rebuild = (cmd->cache_tag << (2 + log2Ceil(L1D_BLOCK_NUM_WORD) + log2Ceil(L1D_NUM_SET))) |
                        (cmd->cache_setIdx << (2 + log2Ceil(L1D_BLOCK_NUM_WORD))) |
                        (cmd->blockOffset->at(0) << 2);
    req.m_block_idx = addr_rebuild >> (2 + log2Ceil(L1D_BLOCK_NUM_WORD));

    // 页表根地址
    req.m_pagetable_root = cmd->pagetable_root;
    // std::cout << "[L1D_Cache_System::accept] cmd->pagetable_root=0x" << std::hex << cmd->pagetable_root 
    //           << " -> req.m_pagetable_root=0x" << req.m_pagetable_root << std::dec << std::endl;

    // 将各 lane 的数据填入请求
    for (int l = 0; l < NLANE; ++l) {
        req.m_block_offset[l] = cmd->blockOffset->at(l);
        req.m_word_offset[l]  = cmd->wordOffset1H->at(l);
        req.m_data[l] = cmd->data[l];
        req.m_mask[l] = cmd->mask.get_bit(l);
    }

    // 其他元信息
    req.m_l1id = 0;
    req.m_type = 0;
    req.m_wid = cmd->warp_id;
    req.m_reg_idxw = cmd->instr.d;
    req.m_instrId = cmd->instrId;
    req.m_pc = cmd->instr.currentpc;

    // 调试输出
    // std::cout << "[accept] warp=" << req.m_wid << ", rd=" << req.m_reg_idxw
    //           << ", block_idx=" << req.m_block_idx << ", mask = ";
    // for (int i = 0; i < NLANE; ++i) {
    //     std::cout << cmd->mask.get_bit(i);
    // }
    // std::cout << std::endl;
    // for (int i = 0; i < NLANE; ++i) {
    //     if (req.m_mask[i]) {
    //         int bo = static_cast<int>(req.m_block_offset[i]);
    //         uint8_t wo = static_cast<uint8_t>(req.m_word_offset[i]); // 0..15 expected
    //         std::cout << "  [lane " << i << "] blockOffset=" << bo
    //                   << " wordOffset1H=" << std::bitset<4>(wo)  // prints e.g. 0101
    //                   << " data=0x" << std::hex << req.m_data[i] << std::dec
    //                   << std::endl;
    //     } else {
    //         std::cout << "  [lane " << i << "] masked-off" << std::endl;
    //     }
    // }

    // 写入FIFO
    internal_fifo_lsu_2_l1d.write(req);
    // 储存回调表，用于返回阶段
    int reg_idxw = cmd->instr.d;
    int warp_id = cmd->warp_id;
    uint32_t pc = cmd->instr.currentpc;
    std::pair<int, int> key(warp_id, reg_idxw);
    
    // 检查是否已存在相同的key（可能表示有多个LW指令写回同一寄存器）
    if (callback_table.find(key) != callback_table.end()) {
        // std::cout << "[L1D_Cache_System::accept] ⚠️ WARNING: callback_table key already exists: "
        //           << "warp=" << warp_id << ", rd=" << reg_idxw 
        //           << ", pc=0x" << std::hex << pc << std::dec
        //           << ". Previous entry will be overwritten!" << std::endl;
    }
    
    callback_table[key] = std::make_pair(std::move(callback), std::move(cmd));
    // std::cout << "[L1D_Cache_System::accept] ✅ Stored callback: warp=" << warp_id 
    //           << ", rd=" << reg_idxw << ", pc=0x" << std::hex << pc 
    //           << ", callback_table.size()=" << callback_table.size() << std::dec << std::endl;

    return 0; // 表示成功接收请求
}

void L1D_Cache_System::forward_rsp() {
    // std::cout << "[forward_rsp] 启动 SC_THREAD" << std::endl;
    while (true) {
        // 检查仿真状态，如果已停止则退出
        if (sc_core::sc_get_status() != sc_core::SC_RUNNING) {
            // std::cout << "[forward_rsp] 检测到仿真停止（状态=" 
            //           << static_cast<int>(sc_core::sc_get_status()) << "），退出线程" << std::endl;
            if (!callback_table.empty()) {
                // std::cerr << "[forward_rsp] ⚠️ WARNING: 仿真停止时仍有 " 
                //           << callback_table.size() << " 个未完成的请求" << std::endl;
            }
            return; // 直接返回，退出线程
        }
        
        // 使用wait()等待FIFO有数据，这样可以响应sc_stop()
        // 如果FIFO为空，等待时钟事件或数据写入事件，这样可以定期检查仿真状态
        if (internal_fifo_l1d_2_lsu.num_available() == 0) {
            // FIFO为空，等待数据到达或时钟事件（用于定期检查仿真状态）
            sc_core::sc_event_or_list events;
            events |= internal_fifo_l1d_2_lsu.data_written_event();
            events |= clk.posedge_event(); // 使用时钟事件，确保能响应sc_stop()
            wait(events);
            // 检查是否因为仿真停止而退出
            if (sc_core::sc_get_status() != sc_core::SC_RUNNING) {
                // std::cout << "[forward_rsp] 检测到仿真停止（状态=" 
                //           << static_cast<int>(sc_core::sc_get_status()) << "），退出线程" << std::endl;
                if (!callback_table.empty()) {
                    // std::cerr << "[forward_rsp] ⚠️ WARNING: 仿真停止时仍有 " 
                    //           << callback_table.size() << " 个未完成的请求" << std::endl;
                }
                return; // 直接返回，退出线程
            }
            // 如果FIFO仍然为空（可能是时钟事件触发），继续等待
            if (internal_fifo_l1d_2_lsu.num_available() == 0) {
                continue;
            }
        }
        
        auto rsp = internal_fifo_l1d_2_lsu.read();

        // std::cout << "[forward_rsp] ✅ 收到响应: warp=" << rsp.m_wid
        //           << ", rd=" << rsp.m_reg_idxw
        //           << ", data[0]=" << rsp.m_data[0]
        //           << ", pc=0x" << std::hex << rsp.m_pc << std::dec
        //           << ", instrId=" << rsp.m_instrId
        //           << std::endl;

        // ==== 1. store 情况：rd=99 表示 dummy，但需要回调并清理callback_table ====
        if (rsp.m_reg_idxw == 99) {
            // Store操作的响应reg_idxw=99，但请求时rd=0，所以需要用pc来匹配
            // 注意：store响应的wid可能为0，所以主要使用pc来匹配
            // 遍历callback_table，找到匹配的store操作（rd=0且pc匹配）
            bool found = false;
            for (auto it = callback_table.begin(); it != callback_table.end(); ) {
                const auto& [key, value] = *it;
                const auto& [callback, orig_cmd] = value;
                // 匹配条件：rd=0（store操作），pc相同
                // 如果wid不为0，也检查wid是否匹配
                if (key.second == 0 && orig_cmd->instr.currentpc == rsp.m_pc &&
                    (rsp.m_wid == 0 || key.first == rsp.m_wid)) {
                    // std::cout << "[forward_rsp] ✅ Found store callback: warp=" << key.first
                    //           << " (response wid=" << rsp.m_wid << ")"
                    //           << ", pc=0x" << std::hex << rsp.m_pc << std::dec
                    //           << ", callback_table.size()=" << callback_table.size() << std::endl;
                    
                    // 构造返回的cmd（store操作不需要数据）
                    auto ret = std::make_unique<lsu_mem_cmd_t>();
                    ret->warp_id = key.first; // 使用callback_table中的warp_id
                    ret->instr.d = 0; // store操作rd=0
                    ret->pagetable_root = orig_cmd->pagetable_root;
                    ret->instrId = orig_cmd->instrId;
                    ret->instr.currentpc = orig_cmd->instr.currentpc;
                    ret->mask = orig_cmd->mask;
                    ret->opcode = L1D_OPCODE_WRITE;
                    
                    callback(std::move(ret));
                    it = callback_table.erase(it);
                    found = true;
                    break;
                } else {
                    ++it;
                }
            }
            if (!found) {
                // std::cout << "[forward_rsp] ⚠️ WARNING: store响应未找到匹配的callback: warp="
                //           << rsp.m_wid << ", pc=0x" << std::hex << rsp.m_pc << std::dec
                //           << ", callback_table.size()=" << callback_table.size() << std::endl;
                // 输出callback_table中的所有条目用于调试
                // if (!callback_table.empty()) {
                //     std::cerr << "[forward_rsp] Current callback_table entries: ";
                //     for (const auto& [k, v] : callback_table) {
                //         const auto& [cb, cmd] = v;
                //         std::cerr << "(warp=" << k.first << ",rd=" << k.second 
                //                   << ",pc=0x" << std::hex << cmd->instr.currentpc << std::dec << ") ";
                //     }
                //     std::cerr << std::endl;
                // }
            }
            continue;
        }

        // ==== 2. load 情况：去查 callback_table ====
        std::pair<int, int> key(rsp.m_wid, rsp.m_reg_idxw);
        auto it = callback_table.find(key);
        if (it != callback_table.end()) {
            auto& [callback, orig_cmd] = it->second;

            auto ret = std::make_unique<lsu_mem_cmd_t>();
            ret->warp_id = rsp.m_wid;
            ret->instr.d = rsp.m_reg_idxw;
            ret->pagetable_root = orig_cmd->pagetable_root;
            ret->instrId = orig_cmd->instrId;
            ret->instr.currentpc = orig_cmd->instr.currentpc;
            ret->mask = orig_cmd->mask;

            for (int i = 0; i < NLANE; ++i) {
                ret->data[i] = rsp.m_data[i];
            }

            // std::cout << "[forward_rsp] ✅ Found callback: warp=" << rsp.m_wid 
            //           << ", rd=" << rsp.m_reg_idxw << ", pc=0x" << std::hex 
            //           << orig_cmd->instr.currentpc << std::dec
            //           << ", data[0]=0x" << std::hex << rsp.m_data[0] << std::dec
            //           << ", callback_table.size()=" << callback_table.size() << std::endl;

            callback(std::move(ret));
            callback_table.erase(it);
        } else if (rsp.m_reg_idxw == 0) {
            // ==== 3. store write hit 情况：rd=0 但不在callback_table中（可能是write hit） ====
            // Write hit的响应reg_idxw=0，但可能不在callback_table中（因为write hit不需要等待L2响应）
            // 这种情况下，我们不需要回调，因为write hit已经完成
            // std::cout << "[forward_rsp] ℹ️ store write hit响应（rd=0），无需回调: warp="
            //           << rsp.m_wid << ", pc=0x" << std::hex << rsp.m_pc << std::dec << std::endl;
        } else {
            // std::cerr << "[forward_rsp] ❌ 未找到对应 callback，warp="
            //           << rsp.m_wid << ", rd=" << rsp.m_reg_idxw 
            //           << ", pc=0x" << std::hex << rsp.m_pc << std::dec
            //           << ", data[0]=0x" << std::hex << rsp.m_data[0] << std::dec
            //           << ", callback_table.size()=" << callback_table.size() << std::endl;
            // 输出callback_table中的所有key用于调试
            // if (!callback_table.empty()) {
            //     std::cerr << "[forward_rsp] Current callback_table keys: ";
            //     for (const auto& [k, v] : callback_table) {
            //         const auto& [cb, cmd] = v;
            //         std::cerr << "(warp=" << k.first << ",rd=" << k.second 
            //                   << ",pc=0x" << std::hex << cmd->instr.currentpc << std::dec << ") ";
            //     }
            //     std::cerr << std::endl;
            // } else {
                std::cerr << "[forward_rsp] callback_table is empty! This response may be orphaned." << std::endl;
            }
            // 注意：即使找不到callback，也要继续处理下一个响应，避免死锁
            // 可能是store操作的响应（rd=99应该已经被过滤），或者callback已经被处理过了
        }
    }

L1D_Cache_System::~L1D_Cache_System() {
    // 检查是否有未完成的请求
    if (!callback_table.empty()) {
        std::cerr << "[L1D_Cache_System::~L1D_Cache_System] ⚠️ WARNING: "
                  << callback_table.size() << " pending requests in callback_table:" << std::endl;
        for (const auto& [key, value] : callback_table) {
            const auto& [callback, orig_cmd] = value;
            std::cerr << "  - warp=" << key.first << ", rd=" << key.second 
                      << ", pc=0x" << std::hex << orig_cmd->instr.currentpc << std::dec << std::endl;
        }
    }
    
    if (tf != nullptr) {
        sc_close_vcd_trace_file(tf);
        tf = nullptr;
    }
}