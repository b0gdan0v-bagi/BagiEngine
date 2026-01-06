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

        [[nodiscard]] size_t Size() const;
        [[nodiscard]] eastl::string Materialize() const;
        [[nodiscard]] PoolString MaterializeToPoolString() const;
        [[nodiscard]] bool IsSingle() const;

        [[nodiscard]] bool Empty() const;
        [[nodiscard]] uint64_t GetHash() const;

        [[nodiscard]] bool operator==(const PoolStringChain& other) const;
        [[nodiscard]] bool operator!=(const PoolStringChain& other) const;
        [[nodiscard]] bool operator==(const PoolString& other) const;
        [[nodiscard]] bool operator!=(const PoolString& other) const;

    private:
        eastl::fixed_vector<PoolStringElement, 3> _fragments;
    };

    // Обратные операторы сравнения: PoolString op PoolStringChain
    [[nodiscard]] inline bool operator==(const PoolString& lhs, const PoolStringChain& rhs) {
        return rhs == lhs;
    }

    [[nodiscard]] inline bool operator!=(const PoolString& lhs, const PoolStringChain& rhs) {
        return rhs != lhs;
    }

}  // namespace Core
