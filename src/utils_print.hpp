#pragma once
#include <array>
#include <iostream>

namespace sc_core {

template <typename T, std::size_t N>
std::ostream& operator<<(std::ostream& os, const std::array<T, N>& arr) {
    os << "{";
    for (std::size_t i = 0; i < N; ++i) {
        os << arr[i];
        if (i < N - 1) {
            os << ", "; // 在元素之间添加分隔符
        }
    }
    os << "}";
    return os; // 返回流对象以支持链式调用
}

} // namespace sc_core
