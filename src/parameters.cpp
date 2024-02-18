#include "parameters.h"

uint32_t extractBits32(uint32_t number, int start, int end)
{
    return (number >> end) & ((1 << (start - end + 1)) - 1);
}
