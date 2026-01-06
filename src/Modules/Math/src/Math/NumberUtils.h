#pragma once

namespace Math {

    // Подсчитывает количество цифр в числе (включая знак минус для отрицательных чисел)
    // Использует каскад сравнений для оптимизации (быстрее, чем цикл или log10)
    [[nodiscard]] constexpr size_t CountDigits(int v) {
        size_t count = (v < 0) ? 1 : 0;  // Место под минус
        unsigned int n = (v < 0) ? static_cast<unsigned int>(-static_cast<long long>(v)) : static_cast<unsigned int>(v);

        // Каскад сравнений (быстрее, чем цикл или log10)
        if (n < 10)
            return count + 1;
        if (n < 100)
            return count + 2;
        if (n < 1000)
            return count + 3;
        if (n < 10000)
            return count + 4;
        if (n < 100000)
            return count + 5;
        if (n < 1000000)
            return count + 6;
        if (n < 10000000)
            return count + 7;
        if (n < 100000000)
            return count + 8;
        if (n < 1000000000)
            return count + 9;
        return count + 10;
    }

    template <typename T>
    void HashCombine(size_t& seed, const T& v) {
        seed ^= eastl::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

}  // namespace Math

