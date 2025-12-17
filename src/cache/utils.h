#ifndef UTILS_H
#define UTILS_H

#include <random>
#include <functional>

/*integer log2 floor(向下取整)
ref: GPGPU-Sim gpu-misc.h */
unsigned int LOGB2(unsigned int v);

/*integer 2's power
ref: https://stackoverflow.com/questions/101439/the-most-efficient-way-to-implement-an-integer-based-power-function-powint-int */
unsigned int POW2(unsigned int exp,unsigned int base=2);

/*随机 ture/false
ref: https://stackoverflow.com/questions/43329352/generating-random-boolean */
bool randomBool();

//ref: https://stackoverflow.com/questions/288739/generate-random-numbers-uniformly-over-an-entire-range
template<typename T>
T random(T range_from, T range_to) {
    std::random_device                  rand_dev;
    std::mt19937                        generator(rand_dev());
    std::uniform_int_distribution<T>    distr(range_from, range_to);
    return distr(generator);
}
#endif