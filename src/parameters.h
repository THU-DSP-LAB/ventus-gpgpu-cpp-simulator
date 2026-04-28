#ifndef _PARAMETERS_H
#define _PARAMETERS_H
#include "magic_enum.hpp"
#include "ventus_cyclesim.h"
#include <array>
#include <bitset>
#include <fmt/ostream.h>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <memory>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept> // For std::out_of_range
#include <unordered_map>

#include "hardware_config.hpp"
#include "utils_print.hpp"
#define SC_INCLUDE_DYNAMIC_PROCESSES
#define SPIKE_OUTPUT
#include <sysc/kernel/sc_time.h>
#include <systemc.h>

// #include <format>  // gcc13支持std::format

inline constexpr int xLen = 32;
inline constexpr long unsigned int hw_num_thread = 32; // 每个warp支持的最大thread数目
inline constexpr uint32_t hw_num_thread_mask = 0xFFFFFFFF;
inline constexpr int ireg_bitsize = 10;
inline constexpr int ireg_size = 1 << ireg_bitsize;
inline constexpr int INS_LENGTH = 32; // the length of per instruction
inline constexpr double PERIOD = 10;
inline constexpr auto TIME_UNIT = SC_NS;
inline constexpr int IFIFO_SIZE = 10;
inline constexpr int OPCFIFO_SIZE = SUBCORE_WARP_NUM;
inline constexpr int BANK_NUM = 4;
inline constexpr int num_register_per_warp = 256; // 每个warp寄存器数目
inline constexpr int NUM_MAX_KERNEL = 8;
inline constexpr unsigned max_concurrent_kernel = 4; // 正在运行的kernel的最大数量
inline constexpr unsigned hw_lds_size = 0x10000000;  // core的总localmem大小
inline constexpr unsigned MAX_RUNNING_CTA_PER_KERNEL = 32;
inline constexpr unsigned ldsBaseAddr_core = 0x70000000;
inline constexpr unsigned LSU_MSHR_SIZE = hw_num_warp;
inline constexpr unsigned L1D_NUM_SET = 256;       // L1 D-cache的组数
inline constexpr unsigned L1D_NUM_WAY = 2;         // L1 D-cache的组相联度
inline constexpr unsigned L1D_BLOCK_NUM_WORD = 32; // 每个cache block包含多少个32bit
using vaddr_t = uint32_t;
using paddr_t = uint32_t;

// 核心流水线与L1D之间的接口 opcode & param
inline constexpr uint8_t L1D_OPCODE_READ = 0x0;
inline constexpr uint8_t L1D_OPCODE_WRITE = 0x1;
inline constexpr uint8_t L1D_OPCODE_ATOMIC = 0x2;
inline constexpr uint8_t L1D_OPCODE_CACHEOP = 0x3;
inline constexpr uint8_t L1D_PARAM_NORMAL = 0x0;     // 常规读写
inline constexpr uint8_t L1D_PARAM_PREFETCH = 0x1;   // 预留性读出
inline constexpr uint8_t L1D_PARAM_CONDWRITE = 0x1;  // 条件性写入
inline constexpr uint8_t L1D_PARAM_NONCACHE = 0x2;   // 不缓存读出/写入
inline constexpr uint8_t L1D_PARAM_INVALIDATE = 0x0; // 全局无效化
inline constexpr uint8_t L1D_PARAM_FLUSH = 0x1;      // 全局冲刷
inline constexpr uint8_t L1D_PARAM_FENCE = 0x2;      // 等待MSHR清空

inline constexpr uint8_t L1D_PARAM_ATOMIC_SWAP = 16;
inline constexpr uint8_t L1D_PARAM_ATOMIC_ADD = 0;
inline constexpr uint8_t L1D_PARAM_ATOMIC_XOR = 1;
inline constexpr uint8_t L1D_PARAM_ATOMIC_OR = 2;
inline constexpr uint8_t L1D_PARAM_ATOMIC_AND = 3;
inline constexpr uint8_t L1D_PARAM_ATOMIC_MIN = 4;
inline constexpr uint8_t L1D_PARAM_ATOMIC_MAX = 5;
inline constexpr uint8_t L1D_PARAM_ATOMIC_MINU = 6;
inline constexpr uint8_t L1D_PARAM_ATOMIC_MAXU = 7;

// 编译期计算整数的二进制对数向上取整
constexpr unsigned log2Ceil(unsigned n) {
    unsigned log = 0;
    n--;
    while (n > 0) {
        log++;
        n >>= 1;
    }
    return log;
}
inline constexpr int depth_thread = log2Ceil(hw_num_thread);

union i32_u32_f32_t {
    int32_t i32;
    uint32_t u32;
    float f32;
};
using iuf32_t = i32_u32_f32_t;
using reg_t = uint32_t;

using v_regfile_t = std::array<reg_t, hw_num_thread>;
struct vector_t : std::array<reg_t, hw_num_thread> {
    friend std::ostream& operator<<(std::ostream& os, const v_regfile_t& arr) {
        os << "{" << std::hex;
        for (const auto& item : arr) {
            os << item << ",";
        }
        os << std::dec << "}";
        return os;
    }
    bool operator==(const std::array<reg_t, hw_num_thread>& other) const {
        for (int i = 0; i < hw_num_thread; ++i)
            if ((*this)[i] != other[i])
                return false;
        return true;
    }
    v_regfile_t& operator=(const std::array<reg_t, hw_num_thread>& other) {
        for (int i = 0; i < hw_num_thread; ++i) {
            (*this)[i] = other[i];
        }
        return *this;
    }
};

enum OP_TYPE {
    INVALID_,
    CUSTOM_PRINT_, // only supported in this simulator for debug
#include "sm/all_instructions.txt"
};
struct instable_t {
    std::bitset<32> mask;
    std::unordered_map<std::bitset<32>, OP_TYPE> itable;
};

namespace DecodeParams {
enum branch_t {
    B_N,
    B_B,
    B_J,
    B_R,
};
enum csr_t {
    CSR_N,
    CSR_W,
    CSR_S,
    CSR_C,
};
enum sel_alu3_t {
    A3_X, // 不需要源操作数
    A3_FRS3,
    A3_VRS3,
    A3_SD,
    A3_PC,
};
enum sel_alu2_t {
    A2_X,
    A2_RS2,
    A2_IMM,
    A2_VRS2,
    A2_SIZE, // 指令字节数(=4)(in rvc it's 2)
};
enum sel_alu1_t {
    A1_X,
    A1_RS1,
    A1_VRS1,
    A1_IMM,
    A1_PC,
};
enum sel_imm_t {
    IMM_B,
    IMM_J,
    IMM_I,
    IMM_U,
    IMM_X,
    IMM_Z,
    IMM_S,
    IMM_2,
    IMM_V,
    IMM_L11,
    IMM_S11,
};
enum mem_whb_t {
    MEM_X,
    MEM_W,
    MEM_B,
    MEM_H,
};
enum alu_fn_t {
    FN_X,
    FN_ADD,
    FN_SL,
    FN_SEQ,
    FN_SNE,
    FN_XOR,
    FN_SR,
    FN_OR,
    FN_AND,
    FN_SUB,
    FN_SRA,
    FN_SLT,
    FN_SGE,
    FN_SLTU,
    FN_SGEU,
    FN_MAX,
    FN_MIN,
    FN_MAXU,
    FN_MINU,
    FN_A1ZERO,
    FN_MUL,
    FN_MULH,
    FN_MULHU,
    FN_MULHSU,
    FN_MACC,
    FN_NMSAC,
    FN_MADD,
    FN_NMSUB,
    FN_VMNOR,
    FN_VMNAND,
    FN_VMXNOR,
    FN_VMORNOT,
    FN_VMANDNOT,
    FN_VID,
    FN_VMERGE,
    FN_FADD,
    FN_FSUB,
    FN_FMUL,
    FN_FMADD,
    FN_FMSUB,
    FN_FNMSUB,
    FN_FNMADD,
    FN_VFMADD,
    FN_VFMSUB,
    FN_VFNMSUB,
    FN_VFNMADD,
    FN_FMIN,
    FN_FMAX,
    FN_FLE,
    FN_FLT,
    FN_FEQ,
    FN_FNE,
    FN_FCLASS,
    FN_FSGNJ,
    FN_FSGNJN,
    FN_FSGNJX,
    FN_F2IU,
    FN_F2I,
    FN_IU2F,
    FN_I2F,
    FN_DIV,
    FN_REM,
    FN_DIVU,
    FN_REMU,
    FN_FDIV,
    FN_FSQRT,
    FN_EXP,
    FN_TTF,
    FN_TTH,
    FN_TTB,
    FN_A2ZERO,
    FN_SWAP,
    FN_AMOADD,
    FN_VLS12,
};
enum mem_t {
    M_X,
    M_XRD,
    M_XWR,
};

// 自己添加的decode信号：
enum sel_execunit_t {
    INVALID_EXECUNIT = 0,
    SALU,
    MUL,
    VALU,
    VFPU,
    LSU,
    SFU,
    CSR,
    SIMTSTK,
    TC, // tensor core
    WPSCHEDLER,
};
}

class decodedat {
public:
    bool isvec = 0; // float指令标量向量共用向量寄存器，标量指令的写回不能被向量指令的mask影响
    bool fp;
    bool barrier;
    DecodeParams::branch_t branch;
    bool simtSTK;
    bool simtop;
    DecodeParams::csr_t csr;
    bool reverse; // swap src1,src2, such as vdiv|div
    DecodeParams::sel_alu3_t sel_alu3;
    DecodeParams::sel_alu2_t sel_alu2;
    DecodeParams::sel_alu1_t sel_alu1;
    DecodeParams::sel_imm_t sel_imm;
    DecodeParams::mem_whb_t mem_whb;
    DecodeParams::alu_fn_t alu_fn;
    bool mul;
    DecodeParams::mem_t mem_cmd;
    bool mem_unsigned;
    bool fence;
    bool sfu;
    bool wvd; // write vector register file
    bool readmask;
    bool writemask;
    bool wxd; // write scalar register file
    bool tc;
    bool disable_mask;
    bool custom_signal_0;
    bool atomic;

    // 自己加的decode信号
    DecodeParams::sel_execunit_t sel_execunit;

    decodedat& operator=(const decodedat& rhs) = default;

    // 以下为chisel的其他decode信号
    bool force_rm_rtz;
    bool write_mask = false;
    bool aq;
    bool rl;

    uint32_t mop;
    bool mem() const {
        int value = static_cast<int>(mem_cmd);
        return (value & 1) | ((value >> 1) & 1);
    }

    bool is_vls12() const { return alu_fn == DecodeParams::alu_fn_t::FN_VLS12; }
    void decode_ext(uint32_t instr) {
        mop = readmask ? 3 : ((instr >> 26) & 0b11);
        // clang-format off
        // BitPat instr: VFCVT_RTZ_X_F_V || VFCVT_RTZ_XU_F_V
        force_rm_rtz = ((instr & 0b111111'000000'11110111'00000'1111111)
                              == 0b010010'000000'00110001'00000'1010111);
        // clang-format on
        aq = atomic && ((instr >> 26) & 0b1);
        rl = atomic && ((instr >> 25) & 0b1);
    }
};

template <typename T>
void sc_trace(sc_core::sc_trace_file*& tf, const std::shared_ptr<T>& v, const std::string& name) {
    sc_trace(tf, v == nullptr, name + ".is_null");
    if (v) {
        sc_trace(tf, *v, name);
    }
}

class I_TYPE // type of per instruction
{
public:
    uint32_t origin32bit; // 原始32位指令
    bool is_extended = false;
    uint32_t dispatch_id = 0;
    int op;               // sc_trace不支持enum，只能定义op为int型
    int d = -1;           // beq指令为imm
    int s1 = -1;          // load指令为寄存器addr
    int s2 = -1;          // load指令为offset, addi、auipc指令为imm
    int s3 = -1;          // fmadd等指令使用
    int imm = -1;
    decodedat ddd;
    // int jump_addr = -1; // 分支指令才有用
    uint32_t currentpc; // 每条指令当前pc，取指后赋予
    sc_bv<hw_num_thread> mask;

    I_TYPE() {};
    I_TYPE(uint32_t origin)
        : origin32bit(origin) {};
    I_TYPE(OP_TYPE _op, int _d, int _s1, int _s2)
        : op(_op)
        , d(_d)
        , s1(_s1)
        , s2(_s2) {};
    I_TYPE(I_TYPE _ins, int _currentpc)
        : origin32bit(_ins.origin32bit)
        , is_extended(_ins.is_extended)
        , dispatch_id(_ins.dispatch_id)
        , op(_ins.op)
        , d(_ins.d)
        , s1(_ins.s1)
        , s2(_ins.s2)
        , s3(_ins.s3)
        , currentpc(_currentpc) {};
    bool operator==(const I_TYPE& rhs) const {
        // return rhs.origin32bit == origin32bit && rhs.op == op && rhs.s1 == s1 && rhs.s2 == s2 &&
        // rhs.s3 == s3 && rhs.d == d && rhs.currentpc == currentpc && rhs.mask == mask;
        return rhs.origin32bit == origin32bit && rhs.currentpc == currentpc && rhs.mask == mask;
    }
    I_TYPE& operator=(const I_TYPE& rhs) {
        currentpc = rhs.currentpc;
        origin32bit = rhs.origin32bit;
        is_extended = rhs.is_extended;
        dispatch_id = rhs.dispatch_id;
        op = rhs.op;
        d = rhs.d;
        s1 = rhs.s1;
        s2 = rhs.s2;
        s3 = rhs.s3;
        imm = rhs.imm;
        ddd = rhs.ddd;
        mask = rhs.mask;
        return *this;
    }
    friend ostream& operator<<(ostream& os, I_TYPE const& v) {
        os << std::setw(18) << (magic_enum::enum_name((OP_TYPE)v.op)) << "0x" << std::setfill('0')
           << std::setw(8) << std::hex << v.origin32bit << std::dec << std::setfill(' ')
           << std::setw(0);
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const I_TYPE& v, const std::string& NAME) {
        sc_trace(tf, v.origin32bit, NAME + ".ins_bit");
        sc_trace(tf, v.is_extended, NAME + ".is_extended");
        sc_trace(tf, v.dispatch_id, NAME + ".dispatch_id");
        sc_trace(tf, v.op, NAME + ".op");
        sc_trace(tf, v.s1, NAME + ".s1");
        sc_trace(tf, v.s2, NAME + ".s2");
        sc_trace(tf, v.s2, NAME + ".s3");
        sc_trace(tf, v.d, NAME + ".d");
        sc_trace(tf, v.imm, NAME + ".imm");
        // sc_trace(tf, v.jump_addr, NAME + ".jump_addr");
        sc_trace(tf, v.mask, NAME + ".mask");
        sc_trace(tf, v.currentpc, NAME + ".currentpc");
    }

private:
    static std::string fixedLengthString(const std::string& str, size_t length) {
        std::ostringstream oss;
        oss << std::setw(length) << std::left << str;
        return oss.str();
    }
};
template <> struct fmt::formatter<I_TYPE> : fmt::ostream_formatter { };

typedef struct lsu_mem_cmd_t {
    bool is_shared_memory; // 访问的是shared_memory(LDS)还是global memory
    uint8_t instrId;       // mshr index
    uint8_t opcode;        // tilelink opcode
    uint8_t param;         // tilelink param
    sc_bv<hw_num_thread> mask;
    paddr_t pagetable_root; // pagetable root physical address for mmu
    vaddr_t cache_tag;
    vaddr_t cache_setIdx;
    std::shared_ptr<const std::array<uint8_t, hw_num_thread>> blockOffset;
    std::shared_ptr<const std::array<uint8_t, hw_num_thread>> wordOffset1H;
    std::shared_ptr<const std::array<uint32_t, hw_num_thread>> addr; // for debug
    std::array<uint32_t, hw_num_thread> data;
    uint8_t warp_id; // only for debug
    I_TYPE instr;    // only for debug
} lsu_mem_cmd_t;

class event_if : virtual public sc_interface // "if" means interface
{
public:
    virtual const sc_event& obtain_event() const = 0;
    virtual void notify() = 0;
    virtual void notify(const double time_) = 0;
};
class event : public sc_module, public event_if {
public:
    event(sc_module_name _name)
        : sc_module(_name) { }
    const sc_event& obtain_event() const { return self_event; }
    void notify() { self_event.notify(); }
    void notify(const double time_) { self_event.notify(time_, SC_NS); }

private:
    sc_event self_event;
};

enum REG_TYPE {
    s = 1,
    v,
    csr,
};
class SCORE_TYPE // every score in scoreboard
{
public:
    enum REG_TYPE regtype; // record to write scalar reg or vector reg
    int addr;
    bool operator<(const SCORE_TYPE& t_) const {
        if (regtype == t_.regtype)
            return addr < t_.addr;
        else
            return regtype < t_.regtype;
    }
    friend ostream& operator<<(ostream& os, SCORE_TYPE const& v) {
        os << "(regtype:" << v.regtype << ",addr:" << v.addr << ")";
        return os;
    }
    SCORE_TYPE(REG_TYPE regtype_, int addr_)
        : regtype(regtype_)
        , addr(addr_) {};
};
struct bank_t {
    int bank_id;
    int addr;
    friend std::ostream& operator<<(std::ostream& os, const bank_t& arr) {
        os << "bank" << arr.bank_id << "-" << arr.addr;
        return os;
    }
};
struct warpaddr_t {
    int warp_id;
    int addr;
};
struct opcfifo_t {
    // 进入opcfifo时，若要取操作数，令valid=1，等待regfile返回ready=1
    // 若是立即数，不用取操作数，令valid=0且直接令ready=1
    // 只要ready=1，就可以发射
    I_TYPE ins;
    int warp_id;
    std::array<bool, 3> ready = { 0 };
    std::array<bool, 3> valid = { 0 };
    std::array<bank_t, 3> srcaddr;
    std::array<bool, 3> banktype = { 0 };
    // int mask;
    std::array<std::array<reg_t, hw_num_thread>, 3> data;
    bool all_ready() const { return ready[0] && ready[1] && ready[2]; }
    opcfifo_t() {};
    opcfifo_t(I_TYPE ins_)
        : ins(ins_) {};
    opcfifo_t(
        I_TYPE ins_, int warp_id_, const std::array<bool, 3>& ready_arr,
        const std::array<bool, 3>& valid_arr, const std::array<bank_t, 3>& srcaddr_arr,
        const std::array<bool, 3>& banktype_arr
    )
        : ins(ins_)
        , warp_id(warp_id_)
        , ready(ready_arr)
        , valid(valid_arr)
        , srcaddr(srcaddr_arr)
        , banktype(banktype_arr) {};
};

template <typename T, size_t N> class StaticEntry { // for OPC entry
private:
    std::array<T, N> data_;
    std::array<bool, N> tag_; // 标志位置是否有效
    size_t size_;

public:
    StaticEntry()
        : size_(0) {
        tag_.fill(false);
    }
    void clear() { tag_.fill(false); }
    void push(const T& value) {
        if (size_ < N) {
            for (size_t i = 0; i < N; ++i) {
                if (tag_[i] == false) {
                    data_[i] = value;
                    tag_[i] = true;
                    ++size_;
                    break;
                }
            }
        } else {
            throw std::out_of_range("StaticEntry full but push data");
        }
    }

    void pop(size_t index) {
        if (index >= N || !tag_[index]) {
            throw std::out_of_range("Invalid index");
            return;
        }

        tag_[index] = false;
        --size_;
    }

    T& operator[](size_t index) { return data_[index]; }

    const T& operator[](size_t index) const { return data_[index]; }
    bool tag_valid(size_t index) const { return tag_[index]; }
    size_t get_size() const { return size_; }

    // 以下函数是为了使用范围-based for循环
    T* begin() { return data_.begin(); }
    T* end() { return data_.end(); }
    const T* begin() const { return data_.begin(); }
    const T* end() const { return data_.end(); }
};

template <typename T, std::size_t capacity> class StaticQueue {
private:
    std::array<T, capacity> data;
    std::size_t size;
    std::size_t front_index;
    // 由于建模的特殊性，有时可能需要临时溢出一个数据，此时暂时放入m_staged中
    // 在RTL中，同一周期内可以并行地从一个已满的FIFO中push + pop（需要组合逻辑传递下游ready到上游）
    // 但用软件建模这种行为时push & pop时无法“同时”的
    // 如果在同一周期内先push再pop，会导致临时溢出一个数据，但在硬件上这时可行的
    struct {
        bool valid = false;
        sc_core::sc_time time_stamp; // 追踪数据何时被放入暂存区，应当在同一周期内pop
        T data;
    } m_staged;

    void push_staged(const T& value) {
        assert(!m_staged.valid && "Already has staged data");
        m_staged.valid = true;
        m_staged.time_stamp = sc_core::sc_time_stamp();
        m_staged.data = value;
    }
    void pop_staged() {
        assert(m_staged.valid && "No staged data to pop");
        assert(
            m_staged.time_stamp == sc_core::sc_time_stamp()
            && "Staged data can only be popped in the same cycle it was pushed"
        );
        m_staged.valid = false;
        data[(front_index + size) % capacity] = std::move(m_staged.data);
        ++size;
    }

public:
    StaticQueue()
        : size(0)
        , front_index(0) { }
    void push(const T& value) {
        if (size == capacity) {
            if (m_staged.valid) {
                throw std::out_of_range("StaticQueue is full");
            } else { // 允许临时溢出，但要求同周期内pop
                push_staged(value);
                return;
            }
        }
        data[(front_index + size) % capacity] = value;
        ++size;
    }
    void pop() {
        if (size == 0) {
            throw std::out_of_range("StaticQueue is empty");
        }
        front_index = (front_index + 1) % capacity;
        --size;
        if (m_staged.valid) {
            pop_staged();
        }
    }
    void clear() {
        front_index = 0;
        size = 0;
        m_staged.valid = false;
    }
    T get() { // return front and pop
        if (size == 0) {
            throw std::out_of_range("StaticQueue is empty");
        }
        T re = data[front_index];
        front_index = (front_index + 1) % capacity;
        --size;
        if (m_staged.valid) {
            pop_staged();
        }
        return re;
    }
    T& front() {
        if (size == 0) {
            throw std::out_of_range("StaticQueue is empty");
        }
        return data[front_index];
    }
    const T& front() const {
        if (size == 0) {
            throw std::out_of_range("StaticQueue is empty");
        }
        return data[front_index];
    }
    T& operator[](std::size_t index) { // front对应索引0
        return data[(front_index + index) % capacity];
    }
    const T& operator[](std::size_t index) const { return data[(front_index + index) % capacity]; }
    T& at(size_t index) // 与[]不同，at()包含边界检查
    {
        if (index >= size) {
            throw std::out_of_range("Index out of range");
        }
        return data[(front_index + index) % capacity];
    }
    const T& at(size_t index) const {
        if (index >= size) {
            throw std::out_of_range("Index out of range");
        }
        return data[(front_index + index) % capacity];
    }
    bool isempty() const { return size == 0; }
    bool isfull() const { return size == capacity; }
    size_t used() const { return size; }
    size_t get_capacity() const { return capacity; }
};

struct salu_in_t {
    I_TYPE ins;
    int warp_id;
    reg_t rss1_data;
    reg_t rss2_data;
    reg_t rss3_data;
};
struct salu_out_t {
    I_TYPE ins;
    int warp_id;
    reg_t data; // 计算出的数据
    bool operator==(const salu_out_t& rhs) const { return rhs.ins == ins && rhs.data == data; }
    salu_out_t& operator=(const salu_out_t& rhs) {
        ins = rhs.ins;
        data = rhs.data;
        warp_id = rhs.warp_id;
        return *this;
    }
    friend ostream& operator<<(ostream& os, salu_out_t const& v) {
        os << "(" << v.ins << "," << v.data << ")";
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const salu_out_t& v, const std::string& NAME) {
        sc_trace(tf, v.ins, NAME + ".ins");
        sc_trace(tf, v.warp_id, NAME + ".warp_id");
        sc_trace(tf, v.data, NAME + ".data");
    }
};
struct valu_in_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rsv1_data, rsv2_data, rsv3_data;
};
struct valu_out_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rdv1_data;
    bool operator==(const valu_out_t& rhs) const {
        return rhs.ins == ins && rhs.rdv1_data == rdv1_data;
    }
    valu_out_t& operator=(const valu_out_t& rhs) {
        ins = rhs.ins;
        rdv1_data = rhs.rdv1_data;
        warp_id = rhs.warp_id;
        return *this;
    }
    friend ostream& operator<<(ostream& os, valu_out_t const& v) {
        os << "{" << v.ins << ";";
        auto it = v.rdv1_data.begin();
        while (it != v.rdv1_data.end()) {
            os << *it << " ";
            it = std::next(it);
        }
        os << "}";
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const valu_out_t& v, const std::string& NAME) {
        sc_trace(tf, v.ins, NAME + ".ins");
        for (int i = 0; i < hw_num_thread; i++)
            sc_trace(tf, v.rdv1_data[i], NAME + ".rdv1_data(" + std::to_string(i) + ")");

        sc_trace(tf, v.warp_id, NAME + ".warp_id");
    }
};

struct vfpu_in_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> vfpuSdata1, vfpuSdata2, vfpuSdata3;
};
struct vfpu_out_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rdf1_data;
    reg_t rds1_data; // FCVT_W_S等指令使用
    bool operator==(const vfpu_out_t& rhs) const {
        return rhs.ins == ins && rhs.rdf1_data == rdf1_data;
    }
    vfpu_out_t& operator=(const vfpu_out_t& rhs) {
        ins = rhs.ins;
        rdf1_data = rhs.rdf1_data;
        warp_id = rhs.warp_id;
        return *this;
    }
    friend ostream& operator<<(ostream& os, vfpu_out_t const& v) {
        os << "{" << v.ins << ";";
        auto it = v.rdf1_data.begin();
        while (it != v.rdf1_data.end()) {
            os << *it << " ";
            it = std::next(it);
        }
        os << "}";
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const vfpu_out_t& v, const std::string& NAME) {
        sc_trace(tf, v.ins, NAME + ".ins");
        for (int i = 0; i < hw_num_thread; i++)
            sc_trace(tf, v.rdf1_data[i], NAME + ".rdf1_data(" + std::to_string(i) + ")");
        sc_trace(tf, v.warp_id, NAME + ".warp_id");
    }
};

struct lsu_in_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rsv1_data, rsv2_data, rsv3_data;
};
struct lsu_out_t {
    I_TYPE ins;
    int warp_id;
    std::unique_ptr<std::array<reg_t, hw_num_thread>> rdv1_data;
    bool operator==(const lsu_out_t& rhs) const = default;
};

class simtstack_t {
    // 对于SIMT-stack来说，else分支（对于elsemask等数据）指的是分支指令判断跳转的path，
    // 无论是beq还是bne，也不用管编程模型定义的if和else。
public:
    uint32_t rpc;
    uint32_t nextpc;
    sc_bv<hw_num_thread> nextmask; // 汇合点mask

    friend ostream& operator<<(ostream& os, simtstack_t const& v) {
        os << "(rpc:" << std::hex << v.rpc << "|nextpc:" << v.nextpc << std::dec
           << "|nextmask:" << v.nextmask << ")";
        return os;
    }
};

struct csr_in_t {
    I_TYPE ins;
    int warp_id;
    reg_t csrSdata1;
    reg_t csrSdata2;
    // do not need vector csrSdata currently
};
struct csr_out_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> data;
    bool operator==(const csr_out_t& rhs) const { return rhs.ins == ins && rhs.data == data; }
    csr_out_t& operator=(const csr_out_t& rhs) {
        ins = rhs.ins;
        data = rhs.data;
        warp_id = rhs.warp_id;
        return *this;
    }
    friend ostream& operator<<(ostream& os, csr_out_t const& v) {
        // os << "(" << v.ins << "," << v.data << ")";
        assert(0); // todo
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const csr_out_t& v, const std::string& NAME) {
        sc_trace(tf, v.ins, NAME + ".ins");
        sc_trace(tf, v.warp_id, NAME + ".warp_id");
        for (int i = 0; i < v.data.size(); i++) {
            sc_trace(tf, v.data[i], NAME + ".data(" + std::to_string(i) + ")");
        }
    }
};

struct mul_in_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rsv1_data, rsv2_data, rsv3_data;
    reg_t rss1_data;
};
struct mul_out_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rdv1_data;
    bool operator==(const mul_out_t& rhs) const {
        return rhs.ins == ins && rhs.rdv1_data == rdv1_data;
    }
    mul_out_t& operator=(const mul_out_t& rhs) {
        ins = rhs.ins;
        rdv1_data = rhs.rdv1_data;
        warp_id = rhs.warp_id;
        return *this;
    }
    friend ostream& operator<<(ostream& os, mul_out_t const& v) {
        os << "{" << v.ins << ";";
        auto it = v.rdv1_data.begin();
        while (it != v.rdv1_data.end()) {
            os << *it << " ";
            it = std::next(it);
        }
        os << "}";
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const mul_out_t& v, const std::string& NAME) {
        sc_trace(tf, v.ins, NAME + ".ins");
        for (int i = 0; i < hw_num_thread; i++)
            sc_trace(tf, v.rdv1_data[i], NAME + ".rdv1_data(" + std::to_string(i) + ")");
        sc_trace(tf, v.warp_id, NAME + ".warp_id");
    }
};

struct sfu_in_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rsv1_data, rsv2_data;
};
struct sfu_out_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rdv1_data;
    bool operator==(const sfu_out_t& rhs) const {
        return rhs.ins == ins && rhs.rdv1_data == rdv1_data;
    }
    sfu_out_t& operator=(const sfu_out_t& rhs) {
        ins = rhs.ins;
        rdv1_data = rhs.rdv1_data;
        warp_id = rhs.warp_id;
        return *this;
    }
    friend ostream& operator<<(ostream& os, sfu_out_t const& v) {
        os << "{" << v.ins << ";";
        auto it = v.rdv1_data.begin();
        while (it != v.rdv1_data.end()) {
            os << *it << " ";
            it = std::next(it);
        }
        os << "}";
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const sfu_out_t& v, const std::string& NAME) {
        sc_trace(tf, v.ins, NAME + ".ins");
        for (int i = 0; i < hw_num_thread; i++)
            sc_trace(tf, v.rdv1_data[i], NAME + ".rdv1_data(" + std::to_string(i) + ")");
        sc_trace(tf, v.warp_id, NAME + ".warp_id");
    }
};

struct tc_in_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> tcSdata1, tcSdata2, tcSdata3;
};
struct tc_out_t {
    I_TYPE ins;
    int warp_id;
    std::array<reg_t, hw_num_thread> rdv1_data;
    bool operator==(const tc_out_t& rhs) const {
        return rhs.ins == ins && rhs.rdv1_data == rdv1_data;
    }
    tc_out_t& operator=(const tc_out_t& rhs) {
        ins = rhs.ins;
        rdv1_data = rhs.rdv1_data;
        warp_id = rhs.warp_id;
        return *this;
    }
    friend ostream& operator<<(ostream& os, tc_out_t const& v) {
        os << "{" << v.ins << ";";
        auto it = v.rdv1_data.begin();
        while (it != v.rdv1_data.end()) {
            os << *it << " ";
            it = std::next(it);
        }
        os << "}";
        return os;
    }
    friend void sc_trace(sc_trace_file* tf, const tc_out_t& v, const std::string& NAME) {
        sc_trace(tf, v.ins, NAME + ".ins");
        for (int i = 0; i < hw_num_thread; i++)
            sc_trace(tf, v.rdv1_data[i], NAME + ".rdv1_data(" + std::to_string(i) + ")");
        sc_trace(tf, v.warp_id, NAME + ".warp_id");
    }
};

class WARP_BONE {
public:
    const int warp_id; // hardware warp slot id
    // sc_event ev_kernel_ret; // 当前warp已经执行完kernel
    int blk_slot_idx;
    int warp_idx_in_blk;
    std::function<void(int, int)> finish_callback; // 当前warp执行完毕后回调通知CTA Scheduler
    paddr_t pagetable;                             // 页表基址
    int num_thread;                                // warp内线程数
    uint32_t dispatch_id = 0;

    explicit WARP_BONE(int warp_id)
        : warp_id(warp_id)
        , is_warp_activated(("is_warp_activated_Warp" + std::to_string(warp_id)).c_str())
        , pc_valid(("pc_valid_Warp" + std::to_string(warp_id)).c_str())
        , jump(("jump_Warp" + std::to_string(warp_id)).c_str())
        , branch_sig(("branch_sig_Warp" + std::to_string(warp_id)).c_str())
        , vbran_sig(("vbran_sig_Warp" + std::to_string(warp_id)).c_str())
        , jump_addr(("jump_addr_Warp" + std::to_string(warp_id)).c_str())
        , pc(("pc_Warp" + std::to_string(warp_id)).c_str())
        , decode_ins(("decode_ins_Warp" + std::to_string(warp_id)).c_str())
        , ibuf_empty(("ibuf_empty_Warp" + std::to_string(warp_id)).c_str())
        , ibuf_full(("ibuf_full_Warp" + std::to_string(warp_id)).c_str())
        , ibuftop_ins(("ibuftop_ins_Warp" + std::to_string(warp_id)).c_str())
        , ififo_elem_num(("ififo_elem_num_Warp" + std::to_string(warp_id)).c_str())
        , dispatch_warp_valid(("dispatch_warp_valid_Warp" + std::to_string(warp_id)).c_str())
        , current_mask(("current_mask_Warp" + std::to_string(warp_id)).c_str())
        , simtstk_jumpaddr(("simtstk_jumpaddr_Warp" + std::to_string(warp_id)).c_str())
        , simtstk_jump(("simtstk_jump_Warp" + std::to_string(warp_id)).c_str()) {
        current_mask.write(~sc_bv<hw_num_thread>()); // default: all true
        finish_callback = nullptr;
        will_warp_activate = false;
    }

    void export_vcd_trace(sc_core::sc_trace_file* tf, const std::string& prefix) const;

    void initwarp() {
        ififo.clear();
        can_dispatch = false;
        score.clear();
        s_regfile.fill(0);
        for (auto& subarray : v_regfile)
            subarray.fill(0);
        CSR_reg.clear();
        dispatch_id = 0;
        std::stack<simtstack_t>().swap(IPDOM_stack);

        endprg_flush_pipe.write(true);
    }

    sc_signal<bool, SC_MANY_WRITERS> is_warp_activated;
    bool will_warp_activate;

    // fetch
    struct regext_t {
        bool valid;
        int ext1, ext2, ext3, extd, extimm;
    } regext; // decode stage regext prefix-instruction info

    sc_signal<bool, SC_MANY_WRITERS> pc_valid; // PC to fetch
    sc_signal<bool, SC_MANY_WRITERS> jump, branch_sig,
        vbran_sig; // 无论是否jump，只要发生了分支判断，将branch_sig置为1。其中branch_sig是标量分支，vbran_sig是向量分支
    sc_signal<vaddr_t> jump_addr;
    sc_signal<vaddr_t, SC_MANY_WRITERS> pc;
    I_TYPE fetch_ins;
    sc_signal<I_TYPE> decode_ins;
    // ibuffer
    sc_event ev_ibuf_updated;
    sc_signal<bool> ibuf_empty, ibuf_full;
    sc_signal<std::shared_ptr<I_TYPE>> ibuftop_ins;
    StaticQueue<std::shared_ptr<I_TYPE>, IFIFO_SIZE> ififo;
    sc_signal<int> ififo_elem_num;
    // scoreboard
    sc_event ev_judge_dispatch;
    bool can_dispatch;
    sc_signal<bool> dispatch_warp_valid;
    I_TYPE _scoretmpins;
    std::set<SCORE_TYPE> score; // record regfile addr that's to be written
    bool wait_bran; // 应该使用C++类型；dispatch了分支指令，则要暂停dispatch等待分支指令被执行
    // warp scheduling
    sc_event ev_warp_dispatch;
    // regfile
    std::array<reg_t, num_register_per_warp> s_regfile;
    std::array<v_regfile_t, num_register_per_warp> v_regfile;
    std::unordered_map<int, reg_t> CSR_reg; // 标量CSR
    std::unordered_map<int, std::array<reg_t, hw_num_thread>> CSR_vreg; // 向量CSR
    // simt-stack
    std::stack<simtstack_t> IPDOM_stack;
    sc_signal<sc_bv<hw_num_thread>, SC_MANY_WRITERS> current_mask; // 在dispatch时随指令存入OPC
    sc_signal<int> simtstk_jumpaddr;                               // out_pc
    sc_signal<bool> simtstk_jump;                                  // fetch跳转的控制信号

    sc_signal<bool> endprg_flush_pipe;
};

// union FloatAndInt
// {
//     float f;
//     int i;
// };
// inline std::ostream &operator<<(std::ostream &os, const FloatAndInt &val)
// {
//     os << val.f;
//     return os;
// };
// inline bool operator==(const FloatAndInt &left, const FloatAndInt &right)
// {
//     return left.i == right.i;
// };

uint32_t extractBits32(uint32_t number, int start, int end);

template <typename T, std::size_t N, std::size_t... Is>
void printArrayHelper(const std::array<T, N>& arr, std::index_sequence<Is...>) {
    std::cout << '(';
    ((std::cout << arr[Is] << (Is != N - 1 ? "," : "")), ...);
    std::cout << ")\n";
}
template <typename T, std::size_t N> void printArray(const std::array<T, N>& arr) {
    printArrayHelper(arr, std::make_index_sequence<N> {});
}

template <typename T, std::size_t N, std::size_t... Is>
void coutArrayHelper(std::ostringstream& oss, const std::array<T, N>& arr, std::index_sequence<Is...>) {
    ((oss << (Is != 0 ? "," : "") << arr[Is]), ...);
}
template <typename T, std::size_t N> std::string coutArray(const std::array<T, N>& arr) {
    std::ostringstream oss;
    oss << '(';
    coutArrayHelper(oss, arr, std::make_index_sequence<N> {});
    oss << ')';
    return oss.str();
}

template <size_t Size> class BoolArray { // for wait_barrier
public:
    BoolArray()
        : array {} { } // 默认初始化所有值为false

    // 重载下标操作符（非常量版本）
    bool& operator[](size_t index) { return array[index]; }

    // 重载下标操作符（常量版本）
    const bool& operator[](size_t index) const { return array[index]; }

    void set(size_t index, bool value) {
        if (index < Size) {
            array[index] = value;
        } else {
            throw std::out_of_range("Index out of bounds");
        }
    }

    // 检查前n项是否都满足指定的布尔值
    bool areFirstNValue(size_t n, bool value) const {
        if (n > Size) {
            throw std::out_of_range("Number of items to check exceeds array size");
        }

        for (size_t i = 0; i < n; ++i) {
            if (array[i] != value) {
                return false;
            }
        }
        return true;
    }

    void fill(bool value) { array.fill(value); }

private:
    std::array<bool, Size> array;
};

template <typename T, size_t N> class SafeArray {
public:
    T& operator[](size_t index) {
        checkIndex(index);
        return array[index];
    }

    const T& operator[](size_t index) const {
        checkIndex(index);
        return array[index];
    }
    void fill(const T& value) { array.fill(value); }
    auto begin() { return array.begin(); }
    auto end() { return array.end(); }
    auto begin() const { return array.begin(); }
    auto end() const { return array.end(); }

private:
    std::array<T, N> array;
    void checkIndex(size_t index) const {
        if (index >= N) {
            throw std::out_of_range("Array index out of bounds");
        }
    }
};

#endif
