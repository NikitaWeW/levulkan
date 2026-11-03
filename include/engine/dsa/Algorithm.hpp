#pragma once
#include <glm/glm.hpp>
#include <ranges>
#include <algorithm>

namespace egn {
    template <typename T>
    inline T hash_combine(T const &lhs, T const &rhs) noexcept {
        return lhs ^ (rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2));
    }

    template<typename T, glm::length_t L, glm::qualifier Q, std::size_t... Idx>
    inline bool vecEquals(glm::vec<L, T, Q> const &a, glm::vec<L, T> const &b, T epsilon, std::index_sequence<Idx...>) {
        return ((glm::abs(a[Idx] - b[Idx]) <= epsilon) && ...);
    }
    template<typename T, glm::length_t L, glm::qualifier Q>
    inline bool vecEquals(glm::vec<L, T, Q> const &a, glm::vec<L, T> const &b, T epsilon = T(1e-6)) {
        return vecEquals(a, b, epsilon, std::make_index_sequence<L>());
    }
};