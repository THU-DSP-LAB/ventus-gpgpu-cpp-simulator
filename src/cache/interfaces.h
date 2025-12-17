#ifndef INTERFACES_H
#define INTERFACES_H

#include <deque>
#include "parameter.h"
#include "../parameters.h"
#include <ostream>
#include <cassert>
#include <iomanip>

enum LSU_cache_coreReq_opcode {
    Read,
    Write,
    Amo,
    InvOrFlu
};

enum LSU_cache_coreReq_type_amo {
    notamo = 0xFF,
    amoadd = 0x00,
    amoxor = 0x01,
    amoand = 0x03,
    amoor = 0x02,
    amomin = 0x04,
    amomax = 0x05,
    amominu = 0x06,
    amomaxu = 0x07,
    amoswap = 0x10
};

enum TL_UH_A_PARAM_AMOARITH {
    MIN = 0x00,
    MAX = 0x01,
    MINU = 0x02,
    MAXU = 0x03,
    ADD = 0x04
};

enum TL_UH_A_PARAM_AMOLOGIC{
    XOR = 0x00,
    OR = 0x01,
    AND = 0x02,
    SWAP = 0x03
};

enum TL_UH_A_opcode {
    Get=4,
    PutFullData=0,
    PutPartialData=1,
    ArithmeticData=2,
    LogicalData=3
    //Intent=5
};

enum TL_UH_D_opcode {
    AccessAck=0,
    AccessAckData=1,
};

class pipe_reg_base {
    public:
    pipe_reg_base(){
        m_valid = false;
    }

    bool is_valid(){
        return m_valid;
    }

    void invalidate(){
        m_valid = false;
    }

    void set_valid(){
        m_valid = true;
    }

    private:
    bool m_valid;
};

struct dcache_2_L2_memReq : cache_building_block {
    public:
    dcache_2_L2_memReq(){}

    dcache_2_L2_memReq(enum TL_UH_A_opcode opcode, uint32_t param, 
    uint32_t source_id,uint8_t instrId,uint32_t pc,uint32_t block_idx, std::array<uint32_t, hw_num_thread> data, std::array<bool,LINEWORDS> mask) 
    :a_opcode(opcode), a_param(param), a_source(source_id),a_l1id(a_l1id),a_instrId(instrId),a_pc(pc),a_data(data), a_mask(mask){
        // block_idx 是从 L1D_Cache_System 传递过来的，它是地址右移 (2 + log2Ceil(L1D_BLOCK_NUM_WORD)) 位后的值
        // 要重建虚拟地址，需要左移相同的位数
        a_address = block_idx << (2 + log2Ceil(L1D_BLOCK_NUM_WORD));
    }

    enum TL_UH_A_opcode a_opcode;
    uint32_t a_param;
    //int a_size;
    uint32_t a_source; //TODO
    uint32_t a_l1id;
    uint32_t a_address;
    uint32_t a_pagetable_root;
    uint8_t  a_instrId;
    uint32_t a_pc;
    std::array<bool,LINEWORDS> a_mask;
    //bool a_data;//only to indicate whether there is a data transaction
    std::array<uint32_t, hw_num_thread> a_data;
};

//memReq_Q include W from cReq, dirty replace, or flush et.al
//only cReq trigger coreRsp
struct memReq_Q_ele : public dcache_2_L2_memReq {
    public:
    memReq_Q_ele(enum TL_UH_A_opcode opcode, uint32_t param, 
    uint32_t source_id,uint32_t l1id,uint32_t pagetable_root,uint8_t instrId,uint32_t pc,uint32_t block_idx, std::array<uint32_t, hw_num_thread> data, std::array<bool,LINEWORDS> mask){
        a_opcode=opcode;
        a_param=param;
        a_source=source_id;
        a_l1id=l1id;
        a_data=data;
        a_mask=mask;
        a_pagetable_root=pagetable_root;
        a_instrId=instrId;
        a_pc=pc;
        // block_idx 是从 L1D_Cache_System 传递过来的，它是地址右移 (2 + log2Ceil(L1D_BLOCK_NUM_WORD)) 位后的值
        // 要重建虚拟地址，需要左移相同的位数
        a_address = block_idx << (2 + log2Ceil(L1D_BLOCK_NUM_WORD));
    }

    void set_coreRsp(){
        need_coreRsp = true;
    }

    bool have_to_coreRsp(){
        return need_coreRsp;
    }

    bool need_coreRsp=false;
};

class memReq_Q : cache_building_block{
public:
    bool is_full(){
        assert(m_Q.size() <= MEM_REQ_Q_DEPTH);
        return m_Q.size() == MEM_REQ_Q_DEPTH;
    }

    bool is_empty(){
        return m_Q.size() == 0;
    }

    std::deque<memReq_Q_ele> m_Q;
};

struct L2_2_dcache_memRsp : cache_building_block {
    L2_2_dcache_memRsp(){}
    L2_2_dcache_memRsp(enum TL_UH_D_opcode opcode, uint32_t req_id, std::array<bool,LINEWORDS> mask , std::array<uint32_t, hw_num_thread> data,uint8_t instrId,uint32_t pc)
        :d_opcode(opcode),d_source(req_id),d_mask(mask),d_data(data),d_instrId(instrId),d_pc(pc){}
    enum TL_UH_D_opcode d_opcode;
    uint32_t d_source;
    //bool m_with_data;
    std::array<bool,LINEWORDS> d_mask;
    std::array<uint32_t, hw_num_thread> d_data;
    uint8_t d_instrId;
    uint32_t d_pc;
};

class memRsp_Q : cache_building_block{
public:
    bool is_full(){
        assert(m_Q.size() <= MEM_RSP_Q_DEPTH);
        return m_Q.size() == MEM_RSP_Q_DEPTH;
    }

    bool is_empty(){
        return m_Q.size() == 0;
    }

    std::deque<L2_2_dcache_memRsp> m_Q;
};

struct dcache_2_LSU_coreRsp : cache_building_block {
public:
    dcache_2_LSU_coreRsp(){}

    dcache_2_LSU_coreRsp(uint32_t reg_idxw, vec_nlane_t data, uint32_t l1id,uint32_t pagetable_root,uint8_t instrId,uint32_t pc,
        uint32_t wid, std::array<bool,NLANE> mask):m_wid(wid),
        m_reg_idxw(reg_idxw),m_l1id(l1id),m_pagetable_root(pagetable_root),m_instrId(instrId),m_pc(pc),m_mask(mask),m_data(data){
        m_wxd = true;//IsScalar?
        for (int i = 1;i<NLANE;++i){
            if (m_mask[i]==true){
                m_wxd = false;
                break;
            }
        }
    }

    uint32_t m_wid;
    uint32_t m_reg_idxw;
    uint32_t m_l1id;
    uint8_t  m_instrId;
    uint32_t m_pc;
    uint32_t m_pagetable_root;
    std::array<bool,NLANE> m_mask;
    bool m_wxd;//indicate whether its a scalar instruction
    vec_nlane_t m_data;
};

class coreRsp_Q : cache_building_block {
public:
    bool is_full(){
        assert(m_Q.size() <= CORE_RSP_Q_DEPTH);
        return m_Q.size() == CORE_RSP_Q_DEPTH;
    }

    bool is_empty(){
        return m_Q.size() == 0;
    }

    void DEBUG_print(cycle_t time){
        auto& tar = m_Q.front();
        std::cout << std::dec << std::setw(5) << time << " | coreRsp|";//<< DEBUG_print_number;
        std::cout << std::setw(3) << tar.m_wid <<"|   |";
        std::cout << " r_idx" << tar.m_reg_idxw;
        if (tar.m_wxd)
            std::cout << ", is scalar ";
        else
            std::cout << ", is vector ";
        std::cout << std::endl;
        //++DEBUG_print_number;
    }

    std::deque<dcache_2_LSU_coreRsp> m_Q;
    
private:
    //int DEBUG_print_number=0;
};

struct LSU_2_dcache_coreReq : cache_building_block {
public:
    LSU_2_dcache_coreReq(){}

    LSU_2_dcache_coreReq(enum LSU_cache_coreReq_opcode opcode, uint32_t type, 
        uint32_t wid, uint32_t req_id, uint32_t block_idx,uint32_t l1id,uint32_t pagetable_root,uint8_t instrId,uint32_t pc,
        vec_nlane_t perLane_addr, std::array<bool,NLANE> mask, vec_nlane_t data, enum LSU_cache_coreReq_type_amo amo_type=notamo):
        m_opcode(opcode), m_type(type), m_wid(wid), m_reg_idxw(req_id), m_block_idx(block_idx),m_l1id(l1id),m_pagetable_root(pagetable_root),
        m_instrId(instrId),m_pc(pc),m_mask(mask), m_block_offset(perLane_addr), m_amo_type(amo_type), m_data(data){
    }

    enum LSU_cache_coreReq_opcode m_opcode;
    uint32_t m_type;
    enum LSU_cache_coreReq_type_amo m_amo_type;//在硬件中，这个变量和type合用一个信号
    uint32_t m_wid;//used for coreRsp
    uint32_t m_reg_idxw;//used for coreRsp
    uint32_t m_block_idx;
    uint32_t m_l1id;
    uint8_t  m_instrId;
    uint32_t m_pc;
    uint32_t m_pagetable_root;
    std::array<bool,NLANE> m_mask;
    vec_nlane_t m_block_offset;
    vec_nlane_t m_word_offset;
    vec_nlane_t m_data;
};

class coreReq_pipe_reg : public LSU_2_dcache_coreReq, public pipe_reg_base{
    public:
    coreReq_pipe_reg(){}

    void update_with(LSU_2_dcache_coreReq coreReq){
        m_opcode=coreReq.m_opcode;
        m_type=coreReq.m_type;
        m_wid=coreReq.m_wid;
        m_l1id=coreReq.m_l1id;
        m_instrId=coreReq.m_instrId;
        m_pc=coreReq.m_pc;
        m_reg_idxw=coreReq.m_reg_idxw;
        m_block_idx=coreReq.m_block_idx;
        m_mask=coreReq.m_mask;
        m_block_offset=coreReq.m_block_offset;
        m_word_offset=coreReq.m_word_offset;
        m_amo_type=coreReq.m_amo_type;
        m_data=coreReq.m_data;
        m_pagetable_root=coreReq.m_pagetable_root;
        set_valid();
    }

};

class coreReq_pipe1_reg : public LSU_2_dcache_coreReq, public pipe_reg_base{
    public:
    coreReq_pipe1_reg(){}

    void set_dirty_for_invORFlu(){
        m_tag_has_dirty = true;
    }

    void unset_dirty_for_invORFlu(){
        m_tag_has_dirty = false;
    }

    bool invORFlu_has_dirty(){
        return m_tag_has_dirty;
    }

    void set_dirty_for_LRSCAMO(){
        m_chosen_tag_is_dirty = true;
    }

    void unset_dirty_for_LRSCAMO(){
        m_chosen_tag_is_dirty = false;
    }

    bool LRSCAMO_is_dirty(){
        return m_chosen_tag_is_dirty;
    }
    void update_with(LSU_2_dcache_coreReq coreReq){
        m_opcode=coreReq.m_opcode;
        m_type=coreReq.m_type;
        m_wid=coreReq.m_wid;
        m_l1id=coreReq.m_l1id;
        m_pagetable_root=coreReq.m_pagetable_root;
        m_instrId=coreReq.m_instrId;
        m_pc=coreReq.m_pc;
        m_reg_idxw=coreReq.m_reg_idxw;
        m_block_idx=coreReq.m_block_idx;
        m_mask=coreReq.m_mask;
        m_block_offset=coreReq.m_block_offset;
        m_word_offset=coreReq.m_word_offset;
        m_amo_type=coreReq.m_amo_type;
        m_data=coreReq.m_data;
        set_valid();
    }

    //TODO用pipe reg本身的block idx来完成
    block_addr_t m_block_addr_evict_for_invORFlu;
    private:
    bool m_tag_has_dirty=false;
    bool m_chosen_tag_is_dirty=false;
};

class coreRsp_pipe_reg : public dcache_2_LSU_coreRsp, public pipe_reg_base{
    public:
    coreRsp_pipe_reg(){}

    void update_with(dcache_2_LSU_coreRsp coreRsp){
        m_wid = coreRsp.m_wid;
        m_reg_idxw = coreRsp.m_reg_idxw;
        m_l1id=coreRsp.m_l1id;
        m_pagetable_root=coreRsp.m_pagetable_root;
        m_instrId=coreRsp.m_instrId;
        m_pc=coreRsp.m_pc;
        m_mask = coreRsp.m_mask;
        m_wxd = coreRsp.m_wxd;
        m_data = coreRsp.m_data;
        set_valid();
    }
    /* coreRsp_pipe_reg(uint32_t reg_idxw, bool data, uint32_t wid, 
        std::array<bool,NLANE> mask):dcache_2_LSU_coreRsp(reg_idxw,data,wid,mask){
        set_valid();
    } */
};

class memReq_pipe_reg : public dcache_2_L2_memReq, public pipe_reg_base{
    public:
    memReq_pipe_reg(){}

    void update_with(dcache_2_L2_memReq memReq){
        a_opcode = memReq.a_opcode;
        a_param = memReq.a_param;
        a_source = memReq.a_source;
        a_l1id = memReq.a_l1id;
        a_pagetable_root = memReq.a_pagetable_root;
        a_instrId=memReq.a_instrId;
        a_pc=memReq.a_pc;
        a_address = memReq.a_address;
        a_mask = memReq.a_mask;
        a_data = memReq.a_data;
        set_valid();
    }
};

// 重载运算符的内联函数, 四个均有添加
inline std::ostream& operator<<(std::ostream& os, const dcache_2_LSU_coreRsp& rsp) {
    os << "dcache_2_LSU_coreRsp{"
       << "wid=" << rsp.m_wid
       << ", reg_idxw=" << rsp.m_reg_idxw
       << ", m_l1id=" << rsp.m_l1id
       << ",m_pagetable_root="<< rsp.m_pagetable_root
       << ", mask=[";
    for (bool m : rsp.m_mask) os << m << " ";
    os << "], wxd=" << rsp.m_wxd
       << ", data=[";
    for (auto d : rsp.m_data) os << "0x" << std::hex << d << " ";
    os << "]}";
    return os;
}
inline std::ostream& operator<<(std::ostream& os, const LSU_2_dcache_coreReq& req) {
    os << "LSU_2_dcache_coreReq{"
       << "opcode=" << static_cast<int>(req.m_opcode)
       << ", type=" << req.m_type
       << ", wid=" << req.m_wid
       << ",m_l1id="<< req.m_l1id
       << ",m_pagetable_root="<< req.m_pagetable_root
       << ", reg_idxw=" << req.m_reg_idxw
       << ", block_idx=" << req.m_block_idx
       << ", mask=[";
    for (bool m : req.m_mask) os << m << " ";
    os << "], block_offset=[";
    for (auto b : req.m_block_offset) os << "0x" << std::hex << b << " ";
    os << "], word_offset=[";
    for (auto w : req.m_word_offset) os << "0x" << std::hex << w << " ";
    os << "], data=[";
    for (auto d : req.m_data) os << "0x" << std::hex << d << " ";
    os << "]}";
    return os;
}
inline std::ostream& operator<<(std::ostream& os, const L2_2_dcache_memRsp& rsp) {
    os << "L2_2_dcache_memRsp{"
       << "opcode=" << static_cast<int>(rsp.d_opcode)
       << ", source=" << rsp.d_source
       << ", mask=[";
    for (bool m : rsp.d_mask) os << m << " ";
    os << "], data=[";
    for (auto d : rsp.d_data) os << "0x" << std::hex << d << " ";
    os << "]}";
    return os;
}
inline std::ostream& operator<<(std::ostream& os, const dcache_2_L2_memReq& req) {
    os << "dcache_2_L2_memReq{"
       << "opcode=" << static_cast<int>(req.a_opcode)
       << ", param=" << req.a_param
       << ", source=" << req.a_source
       << ", l1id=" << req.a_l1id
       << ", address=0x" << std::hex << req.a_address
       << ", mask=[";
    for (bool m : req.a_mask) os << m << " ";
    os << "], data=[";
    for (auto d : req.a_data) os << "0x" << std::hex << d << " ";
    os << "]}";
    return os;
}

#endif