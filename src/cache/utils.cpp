#include "utils.h"

// 全局PC追踪配置：用于调试和追踪特定指令在cache中的执行路径
std::vector<uint64_t> trace_pcs = {0x800001ac, 0x8000019c};

unsigned int LOGB2(unsigned int v) {
    unsigned int shift = 0;
    unsigned int r = 0;

    shift = ((v & 0xFFFF0000) != 0) << 4;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xFF00) != 0) << 3;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xF0) != 0) << 2;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xC) != 0) << 1;
    v >>= shift;
    r |= shift;
    shift = ((v & 0x2) != 0) << 0;
    v >>= shift;
    r |= shift;

    return r;
}

unsigned int POW2(unsigned int exp, unsigned int base) {
    int result = 1;
    for (;;) {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        if (!exp)
            break;
        base *= base;
    }

    return result;
}

bool randomBool() {
    static auto gen
        = std::bind(std::uniform_int_distribution<>(0, 1), std::default_random_engine());
    return gen();
}