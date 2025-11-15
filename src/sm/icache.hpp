#pragma once

#include "../parameters.h"
#include "sysc/kernel/sc_time.h"
#include <memory>
#include <spdlog/logger.h>
#include <systemc>

//
// data structures
//

struct ICacheConfig {
    unsigned cachelineBytes;
    unsigned numSets;
    unsigned numWays;
    unsigned numMshrItems;
    std::string replacementPolicy;

    unsigned numFetch; // fetch multiple instructions in one access

    unsigned rspLatency; // core response delay cycles (hit or miss)
};

struct ICacheRsp {
    bool hit;
    int warpid;
    vaddr_t addr;
    sc_core::sc_time time; // for debug
    // no real instructions returned here, pure timing model
    // get it from functional model (SV_basic)
};

//
// ICache Replacement Policies
//

class ICacheReplacementPolicy { // abstract class
public:
    virtual void touch(unsigned set_idx, unsigned way_idx) = 0;
    virtual unsigned choose_victim(unsigned set_idx, const std::vector<bool>& valids) = 0;
    virtual ~ICacheReplacementPolicy() = default;
};

class ICacheReplacementPolicy_random : public ICacheReplacementPolicy {
public:
    void touch(unsigned set_idx, unsigned way_idx) override { }
    unsigned choose_victim(unsigned set_idx, const std::vector<bool>& valids) override;
};

//
// ICache module
//

class ICache : public sc_core::sc_module {
private:
    const ICacheConfig m_cfg;

    struct tag_t {
        paddr_t pagetable_root;
        vaddr_t tag;
        bool operator==(const tag_t&) const = default;
    };
    using vaddr_full_t = tag_t;
    std::vector<std::vector<tag_t>> m_tags;  // [set][way]
    std::vector<std::vector<bool>> m_valids; // [set][way]

    // RTL中MSHR会保存同一address的多个miss到subentry，但后续并未使用subentry信息
    // 这里简化为只保存address，多个同address的miss只保存一次
    struct mshr_t {
        paddr_t pagetable_root;
        vaddr_t vaddr;
        bool valid;
        bool sent;
        bool operator==(const mshr_t& other) const = default;
    };
    std::vector<mshr_t> m_mshr;

    // 即将返回的response，无论是hit还是miss
    // miss的response将会使PC replay
    std::queue<ICacheRsp> m_responses;

    // icache replacement policy
    std::unique_ptr<ICacheReplacementPolicy> m_replacer;

    // 此icache建模为纯时序模型，不包括功能模型，因此不真实存储数据
    // 这将导致不能正确仿真invalidate操作的功能效果
    // 设想：可以采用页的version记录，每次物理页更新时，version++，
    //      在icache中保存cacheline对应的version，
    //      访问时对比cache & memory version，若不同则报warning

public:
    ICache(
        sc_core::sc_module_name name, const ICacheConfig& config,
        std::function<paddr_t(paddr_t ptroot, vaddr_t vaddr)> mmu_translate,
        std::function<void(const ICacheRsp&)> core_response_callback,
        std::function<
            int(paddr_t block_addr, unsigned sourceId, std::function<int(unsigned)> callback)>
            l2_request_interface,
        std::shared_ptr<spdlog::logger> logger = nullptr,
        const std::string& log_prefix = "SM ? ICache"
    );

    void access(paddr_t pagetable_root, vaddr_t addr, int warpid);

    // icache将成为核心流水线的一部分，核心流水线flush时icache需要一起flush（例如跳转）
    void flushpipe(int warpid);
    // 清空icache
    void invalidate();

private:
    sc_core::sc_event_queue ev_rsp; // response to core event
    sc_core::sc_event ev_mshr_l2_req; // MSHR miss need to send to L2 later

    void process_response(); // sc_thread for response to SM
    void send_mshr_to_l2();  // sc_thread for sending MSHR miss to L2
    int memory_response_handler(unsigned sourceId);

    // Helper functions

    vaddr_t get_tag(vaddr_t addr) const;
    vaddr_t get_set_idx(vaddr_t addr) const;
    vaddr_t get_cacheline_base(vaddr_t addr) const; // block base address
    bool check_hit(paddr_t pagetable_root, vaddr_t addr, unsigned& way_idx) const;
    bool check_hit(paddr_t pagetable_root, vaddr_t addr) const;

    // Interfaces

    // MMU translation
    std::function<paddr_t(paddr_t ptroot, vaddr_t vaddr)> mmu_translate;

    // response callback to core(SM)
    std::function<void(const ICacheRsp&)> core_response;

    // L2 cache request interface
    // do not need MSHR entry ID in this ICache model
    std::function<int(paddr_t block_addr, unsigned sourceId, std::function<int(unsigned)> callback)>
        l2_request; // lower memory request

    // Logging

    std::shared_ptr<spdlog::logger> m_logger;
    const std::string m_log_prefix = "SM ? ICache";
};
