#pragma once

#include "PoolString.h"
#include "StaticPoolString.h"
#include <Math/NumberUtils.h>
#include <EASTL/string_view.h>
#include <EASTL/array.h>
#include <EASTL/variant.h>
#include <EASTL/string.h>
#include <initializer_list>
#include <cstdint>

namespace Core {

    class PoolStringChain {
        friend class PoolString;

        struct TaggedVal {
            uintptr_t data; // bit 0: 0=PoolString, 1=Int

            static TaggedVal FromPoolString(PoolString ps) {
                return {reinterpret_cast<uintptr_t>(ps._entry)};
            }
            static constexpr TaggedVal FromInt(int i) {
                return {(static_cast<uintptr_t>(i) << 1) | 1};
            }
            constexpr bool IsInt() const {
                return data & 1;
            }
            constexpr int AsInt() const {
                return static_cast<int>(data >> 1);
            }
            PoolString AsPoolString() const {
                return PoolString(reinterpret_cast<const Details::PoolStringEntry*>(data));
            }
        };

        enum class Mode : uint8_t { Empty, Atom, Literal, Compound };

        union Storage {
            PoolString single;
            struct {
                const char* p;
                size_t s;
            } view;
            eastl::array<TaggedVal, 3> compound;

            constexpr Storage() : compound{TaggedVal{0}, TaggedVal{0}, TaggedVal{0}} {}
        };

    public:
        using PoolStringElement = eastl::variant<PoolString, int>;

        constexpr PoolStringChain() : _mode(Mode::Empty), _count(0) {}

        constexpr PoolStringChain(eastl::string_view sv) : _mode(Mode::Literal), _count(0) {
            _storage.view.p = sv.data();
            _storage.view.s = sv.size();
        }

        constexpr PoolStringChain(PoolString ps) : _mode(Mode::Atom), _count(0) {
            _storage.single = ps;
        }

        PoolStringChain(std::initializer_list<PoolStringElement> fragments);

        PoolStringChain& operator+=(PoolString ps);
        PoolStringChain& operator+=(int value);

        template <Details::FixedString Str>
        PoolStringChain& operator+=(const StaticPoolString<Str>& value) {
            return *this += value.ToPoolString();
        }

        static PoolStringChain Concat(PoolString a, int i) {
            PoolStringChain s;
            s._mode = Mode::Compound;
            s._storage.compound[0] = TaggedVal::FromPoolString(a);
            s._storage.compound[1] = TaggedVal::FromInt(i);
            s._count = 2;
            return s;
        }

        constexpr eastl::string_view View() const {
            if (_mode == Mode::Literal)
                return {_storage.view.p, _storage.view.s};
            if (_mode == Mode::Atom)
                return _storage.single.ToStringView();
            return "";
        }

        constexpr size_t Size() const {
            switch (_mode) {
                case Mode::Empty:
                    return 0;
                case Mode::Atom:
                    return _storage.single.Length();
                case Mode::Literal:
                    return _storage.view.s;
                case Mode::Compound: {
                    size_t total = 0;
                    for (uint8_t i = 0; i < _count; ++i) {
                        const auto& v = _storage.compound[i];
                        if (v.IsInt()) {
                            total += Math::CountDigits(v.AsInt());
                        } else {
                            total += v.AsPoolString().Length();
                        }
                    }
                    return total;
                }
            }
            return 0;
        }

        [[nodiscard]] eastl::string Materialize() const;
        [[nodiscard]] PoolString MaterializeToPoolString() const;

        [[nodiscard]] constexpr bool IsSingle() const {
            return _mode == Mode::Atom;
        }

        [[nodiscard]] constexpr bool Empty() const {
            if (_mode == Mode::Empty)
                return true;
            if (_mode == Mode::Atom)
                return _storage.single.Empty();
            if (_mode == Mode::Literal)
                return _storage.view.s == 0;
            if (_mode == Mode::Compound)
                return _count == 0;
            return false;
        }

        [[nodiscard]] uint64_t GetHash() const;

        [[nodiscard]] bool operator==(const PoolStringChain& other) const;
        [[nodiscard]] bool operator!=(const PoolStringChain& other) const;
        [[nodiscard]] bool operator==(const PoolString& other) const;
        [[nodiscard]] bool operator!=(const PoolString& other) const;

        operator PoolString() const {
            return MaterializeToPoolString();
        }

    private:
        Storage _storage;
        Mode _mode;
        uint8_t _count; // Для Compound: кол-во элементов (2 или 3)
    };

    static_assert(sizeof(PoolStringChain) <= 32);

    // Обратные операторы сравнения: PoolString op PoolStringChain
    [[nodiscard]] inline bool operator==(const PoolString& lhs, const PoolStringChain& rhs) {
        return rhs == lhs;
    }

    [[nodiscard]] inline bool operator!=(const PoolString& lhs, const PoolStringChain& rhs) {
        return rhs != lhs;
    }

}  // namespace Core
