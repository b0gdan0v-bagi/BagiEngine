#pragma once

namespace Core {

    class PoolStringChain {
        using PoolStringElement = eastl::variant<PoolString, int>;

    public:
        PoolStringChain() = default;
        PoolStringChain(std::initializer_list<PoolStringElement> fragments) : _fragments(fragments) {}

        PoolStringChain& operator+=(PoolString ps) {
            _fragments.push_back(ps);
            return *this;
        }

        PoolStringChain& operator+=(int value) {
            _fragments.push_back(value);
            return *this;
        }

        template <Details::FixedString Str>
        PoolStringChain& operator+=(const StaticPoolString<Str>& value) {
            _fragments.push_back(value.ToPoolString());
            return *this;
        }

        [[nodiscard]] size_t Size() const {
            size_t total = 0;
            for (const auto& fragment : _fragments) {
                // Используем visit без лишних шаблонов для краткости
                eastl::visit(
                    [&total](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;

                        if constexpr (std::is_same_v<T, PoolString>) {
                            // Предполагаем, что у PoolString получение размера — это O(1)
                            total += arg.Length();
                        } else if constexpr (std::is_same_v<T, int>) {
                            total += CountDigits(arg);
                        }
                    },
                    fragment);
            }
            return total;
        }

        eastl::string Materialize() const {
            eastl::string result;
            result.reserve(Size());

            for (const auto& fragment : _fragments) {
                eastl::visit(
                    [&result](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;

                        if constexpr (std::is_same_v<T, PoolString>) {
                            auto view = arg.ToStringView();
                            result.append(view.data(), view.size());
                        } else if constexpr (std::is_same_v<T, int>) {
                            char buf[12];
                            int len = std::snprintf(buf, sizeof(buf), "%d", arg);

                            if (len > 0) {
                                result.append(buf, static_cast<size_t>(len));
                            }
                        }
                    },
                    fragment);
            }
            return result;
        }

    private:
        static constexpr size_t CountDigits(int v) {
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

        eastl::fixed_vector<PoolStringElement, 3> _fragments;
    };

}  // namespace Core
