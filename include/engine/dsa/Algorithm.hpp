#pragma once
#include <ranges>
#include <algorithm>

namespace egn {
    template <typename T>
    inline T hash_combine(T const &lhs, T const &rhs) noexcept {
        return lhs ^ (rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2));
    }
};