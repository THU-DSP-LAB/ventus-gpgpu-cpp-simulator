#include "write_status_holding_reg.h"
#include <cassert>

// Constructor: Initialize valid bitset
wshr::wshr() { m_valid.reset(); }

// Check if WSHR is empty
bool wshr::empty() { return m_valid.none(); }

// Check if WSHR is full
bool wshr::is_full() { return m_valid.all(); }

// Check if there is a conflict with the given block address
bool wshr::has_conflict(block_addr_t block_idx) {
    for (int i = 0; i < N_WSHR_ENTRY; ++i) {
        if (m_valid[i] && (m_entry[i] == block_idx)) {
            return true;
        }
    }
    return false;
}

// Push a new entry into WSHR
void wshr::push(block_addr_t block_idx, wshr_idx_t& idx) {
    assert(!has_conflict(block_idx));
    for (int i = 0; i < N_WSHR_ENTRY; ++i) {
        if (m_valid[i] == false) {
            m_valid[i] = true;
            m_entry[i] = block_idx;
            idx = i;
            break;
        }
    }
}

// Pop an entry from WSHR
void wshr::pop(wshr_idx_t idx) {
    assert(m_valid[idx] == true);
    m_valid[idx] = false;
}
