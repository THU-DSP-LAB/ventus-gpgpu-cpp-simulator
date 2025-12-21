#ifndef WRITE_STATUS_HOLDING_REG_H
#define WRITE_STATUS_HOLDING_REG_H

#include "parameter.h"
#include "utils.h"
#include <bitset>
// #include "interfaces.h"

class wshr : public cache_building_block {
public:
    wshr();

    bool empty();

    bool is_full();

    bool has_conflict(block_addr_t block_idx);

    void push(block_addr_t block_idx, wshr_idx_t& idx);

    void pop(wshr_idx_t idx);

private:
    std::bitset<N_WSHR_ENTRY> m_valid;
    std::array<block_addr_t, N_WSHR_ENTRY> m_entry;
};

#endif