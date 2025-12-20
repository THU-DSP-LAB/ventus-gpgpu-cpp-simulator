#include "icache.hpp"
#include "sysc/kernel/sc_simcontext.h"
#include "sysc/kernel/sc_time.h"
#include <algorithm>
#include <bit>
#include <spdlog/spdlog.h>
#include <systemc>

unsigned ICacheReplacementPolicy_random::choose_victim(
    unsigned set_idx, const std::vector<bool>& valids
) {
    // use empty way first
    for (int i = 0; i < valids.size(); i++) {
        if (!valids[i])
            return i;
    }
    // all ways are valid, choose one randomly
    return rand() % valids.size();
}

ICache::ICache(
    sc_core::sc_module_name name, const ICacheConfig& config,
    std::function<paddr_t(paddr_t ptroot, vaddr_t vaddr)> mmu_translate_,
    std::function<void(const ICacheRsp&)> core_response_callback,
    std::function<int(paddr_t block_addr, unsigned sourceId, std::function<int(unsigned)> callback)>
        l2_request_interface,
    std::shared_ptr<spdlog::logger> logger, const std::string& log_prefix
)
    : sc_core::sc_module(name)
    , m_cfg(config)
    , mmu_translate(mmu_translate_)
    , core_response(core_response_callback)
    , l2_request(l2_request_interface)
    , m_logger(logger ? logger : spdlog::default_logger())
    , m_log_prefix(log_prefix) {
    assert(std::popcount(m_cfg.numSets) == 1);        // must be power of 2
    assert(std::popcount(m_cfg.cachelineBytes) == 1); // must be power of 2
    assert(
        m_cfg.cachelineBytes % (4 * m_cfg.numFetch) == 0
        && "ICache cachelineBytes must be (numFetch * instruction)-aligned"
    );
    assert(mmu_translate && l2_request && core_response); // not null

    m_tags.resize(m_cfg.numSets);
    m_valids.resize(m_cfg.numSets);
    for (int i = 0; i < m_cfg.numSets; i++) {
        m_tags[i].resize(m_cfg.numWays);
        m_valids[i].resize(m_cfg.numWays, false);
    }
    m_mshr.resize(m_cfg.numMshrItems, mshr_t { 0, 0, false, false });

    if (m_cfg.replacementPolicy == "random") {
        m_replacer = std::make_unique<ICacheReplacementPolicy_random>();
    } else {
        SPDLOG_LOGGER_ERROR(
            m_logger, "{} Unknown ICache replacement policy: {}", m_log_prefix,
            m_cfg.replacementPolicy
        );
        throw std::runtime_error("Unknown ICache replacement policy");
    }

    SC_HAS_PROCESS(ICache);
    SC_THREAD(process_response);
    SC_THREAD(send_mshr_to_l2);
}

vaddr_t ICache::get_tag(vaddr_t addr) const {
    return addr >> (log2Ceil(m_cfg.cachelineBytes) + log2Ceil(m_cfg.numSets));
}

vaddr_t ICache::get_set_idx(vaddr_t addr) const {
    // numSet is power of 2
    return (addr >> log2Ceil(m_cfg.cachelineBytes)) & ((1 << log2Ceil(m_cfg.numSets)) - 1);
}

vaddr_t ICache::get_cacheline_base(vaddr_t addr) const {
    return addr - (addr % m_cfg.cachelineBytes);
}

bool ICache::check_hit(paddr_t ptroot, vaddr_t addr, unsigned& way_idx) const {
    vaddr_t tag = get_tag(addr);
    vaddr_t set_idx = get_set_idx(addr);
    for (int i = 0; i < m_cfg.numWays; i++) {
        if (m_valids[set_idx][i] && m_tags[set_idx][i] == vaddr_full_t { ptroot, tag }) {
            way_idx = i;
            return true;
        }
    }
    return false;
}
bool ICache::check_hit(paddr_t ptroot, vaddr_t addr) const {
    unsigned way_idx;
    return check_hit(ptroot, addr, way_idx);
}

void ICache::access(paddr_t ptroot, vaddr_t addr, int warpid) {
    assert(
        addr % (4 * m_cfg.numFetch) == 0
        && "ICache address must be (numFetch * instruction)-aligned"
    );

    unsigned way_idx;
    bool hit = check_hit(ptroot, addr, way_idx);

    // construct response no matter hit or miss
    auto rsp_latency = sc_core::sc_time(m_cfg.rspLatency * PERIOD, TIME_UNIT);
    auto rsp_time = rsp_latency + sc_core::sc_time_stamp();
    if (m_responses.empty() || m_responses.back().time != rsp_time) {
        assert(m_responses.empty() || m_responses.back().time < rsp_time);
        ev_rsp.notify(rsp_latency);
    }
    m_responses.emplace();
    auto& rsp = m_responses.back();
    rsp.hit = hit;
    rsp.warpid = warpid;
    rsp.addr = addr;
    rsp.time = rsp_time;

    // if miss: add to MSHR & request to lower memory
    auto block_addr_full = vaddr_full_t { ptroot, get_cacheline_base(addr) };
    auto& block_addr = block_addr_full.tag;
    if (!hit) {
        auto mshr_it
            = std::find_if(m_mshr.begin(), m_mshr.end(), [block_addr, ptroot](const mshr_t& mshr) {
                  return mshr.valid && mshr.vaddr == block_addr && mshr.pagetable_root == ptroot;
              });                      // already in MSHR?
        if (mshr_it == m_mshr.end()) { // new miss
            mshr_it = std::find_if(m_mshr.begin(), m_mshr.end(), [](const mshr_t& mshr) {
                return !mshr.valid;
            });                            // find free MSHR entry
            if (mshr_it == m_mshr.end()) { // MSHR full
                SPDLOG_LOGGER_WARN(
                    m_logger,
                    "{} ICACHE miss and MSHR full, dropping request for address 0x{:x}. "
                    "This will not cause functional errors as PC will be replayed.",
                    m_log_prefix, addr
                );
            } else { // new MSHR item, send to L2
                mshr_it->valid = true;
                mshr_it->pagetable_root = ptroot;
                mshr_it->vaddr = block_addr;
                mshr_it->sent = true; // default
                // insert mshr first.
                // When DDR timing is disabled, l2_request may call back immediately
                int failed
                    = l2_request(block_addr, mshr_it - m_mshr.begin(), [this](unsigned entryIdx) {
                          return memory_response_handler(entryIdx);
                      });
                mshr_it->sent = !failed;
                if (failed) {                                  // L2 request refused
                    notify(PERIOD, TIME_UNIT, ev_mshr_l2_req); // try again next cycle
                    SPDLOG_LOGGER_TRACE(
                        m_logger,
                        "{} ICACHE failed to send request to L2 for address 0x{:x}, will try again "
                        "later.",
                        m_log_prefix, addr
                    );
                }
            }
        } else {
            // request already in MSHR, do nothing
        }
    } else { // hit: update replacement policy
        m_replacer->touch(get_set_idx(addr), way_idx);
    }
}

void ICache::process_response() {
    while (1) {
        wait(ev_rsp.default_event());
        wait(SC_ZERO_TIME); // go into delta cycle 1 (clock edge)
        sc_core::sc_time curr_time = sc_core::sc_time_stamp();
        while (!m_responses.empty() && m_responses.front().time <= curr_time) {
            assert(m_responses.front().time == curr_time);
            core_response(m_responses.front());
            m_responses.pop();
        }
    }
}

// sc_thread for sending MSHR miss to L2
void ICache::send_mshr_to_l2() {
    while (true) {
        wait(ev_mshr_l2_req);
        for (auto& mshr_item : m_mshr) {
            if (mshr_item.valid && !mshr_item.sent) { // find pending unsent miss
                int failed = l2_request(
                    mshr_item.vaddr, &mshr_item - &m_mshr[0],
                    [this](unsigned entryIdx) { return memory_response_handler(entryIdx); }
                );
                if (!failed) {
                    mshr_item.sent = true;
                } else { // still failed: try again next cycle
                    notify(PERIOD, TIME_UNIT, ev_mshr_l2_req);
                }
            }
        }
    }
}

int ICache::memory_response_handler(unsigned entryIdx) {
    // find MSHR entry
    auto& mshr_item = m_mshr.at(entryIdx);
    if (!mshr_item.valid) {
        SPDLOG_LOGGER_WARN(
            m_logger,
            "{} ICACHE received memory response for invalid MSHR entry index={}. "
            "May be caused by previous ICache invalidate which clears MSHR? ",
            m_log_prefix, entryIdx
        );
        return -1;
    }

    // install cacheline: find a way to replace
    vaddr_t set_idx = get_set_idx(mshr_item.vaddr);
    // choose victim
    auto way_idx = m_replacer->choose_victim(set_idx, m_valids[set_idx]);
    // install
    m_tags[set_idx][way_idx] = tag_t { mshr_item.pagetable_root, get_tag(mshr_item.vaddr) };
    m_valids[set_idx][way_idx] = true;

    // remove MSHR entry
    mshr_item.valid = false;

    return 0;
}

void ICache::flushpipe(int warpid) {
    // remove inflight responses for this warpid
    auto count = m_responses.size();
    for (size_t i = 0; i < count; i++) {
        auto rsp = std::move(m_responses.front());
        if (rsp.warpid != warpid) {
            m_responses.push(std::move(rsp));
        }
        m_responses.pop();
    }
}

void ICache::invalidate() {
    // invalidate all cachelines
    for (int set = 0; set < m_cfg.numSets; set++) {
        std::fill(m_valids[set].begin(), m_valids[set].end(), false);
    }
    // clear MSHR
    m_mshr.clear(); // may cause memRsp failures? Is this right?
}
