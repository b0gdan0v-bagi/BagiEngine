#include "PoolStringChain.h"

#include <Math/NumberUtils.h>

namespace Core {

    size_t PoolStringChain::Size() const {
        size_t total = 0;
        for (const auto& fragment : _fragments) {
            eastl::visit(
                [&total](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;

                    if constexpr (std::is_same_v<T, PoolString>) {
                        total += arg.Length();
                    } else if constexpr (std::is_same_v<T, int>) {
                        total += Math::CountDigits(arg);
                    }
                },
                fragment);
        }
        return total;
    }

    eastl::string PoolStringChain::Materialize() const {
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

    PoolString PoolStringChain::MaterializeToPoolString() const {
        if (IsSingle()) {
            return eastl::get<PoolString>(_fragments[0]);
        }
        return PoolString::Intern(Materialize());
    }

    bool PoolStringChain::IsSingle() const {
        return _fragments.size() == 1 && eastl::holds_alternative<PoolString>(_fragments[0]);
    }

    bool PoolStringChain::Empty() const {
        if (IsSingle()) {
            return eastl::get<PoolString>(_fragments[0]).Empty();
        }
        return _fragments.empty();
    }

    uint64_t PoolStringChain::GetHash() const {
        if (IsSingle()) {
            return eastl::get<PoolString>(_fragments[0]).HashValue();
        }

        uint64_t hash = String::hashOffset;
        for (const auto& fragment : _fragments) {
            eastl::visit(
                [&hash](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;

                    if constexpr (std::is_same_v<T, PoolString>) {
                        Math::HashCombine(hash, arg.HashValue());
                    } else if constexpr (std::is_same_v<T, int>) {
                        Math::HashCombine(hash, arg);
                    }
                },
                fragment);
        }
        return hash;
    }

    bool PoolStringChain::operator==(const PoolStringChain& other) const {
        if (_fragments.size() != other._fragments.size()) {
            return false;
        }

        for (size_t i = 0; i < _fragments.size(); ++i) {
            const auto& frag1 = _fragments[i];
            const auto& frag2 = other._fragments[i];

            // Проверяем типы
            if (frag1.index() != frag2.index()) {
                return false;
            }

            // Типы равны, сравниваем значения
            if (eastl::holds_alternative<PoolString>(frag1)) {
                if (eastl::get<PoolString>(frag1) != eastl::get<PoolString>(frag2)) {
                    return false;
                }
            } else {
                if (eastl::get<int>(frag1) != eastl::get<int>(frag2)) {
                    return false;
                }
            }
        }

        return true;
    }

    bool PoolStringChain::operator!=(const PoolStringChain& other) const {
        return !(*this == other);
    }

    bool PoolStringChain::operator==(const PoolString& other) const {
        if (IsSingle()) {
            return eastl::get<PoolString>(_fragments[0]) == other;
        }
        return false;
    }

    bool PoolStringChain::operator!=(const PoolString& other) const {
        return !(*this == other);
    }

}  // namespace Core

