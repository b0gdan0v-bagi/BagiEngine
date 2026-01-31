#include "PoolStringChain.h"

#include <BECore/Math/NumberUtils.h>

namespace BECore {

    PoolStringChain::PoolStringChain(std::initializer_list<PoolStringElement> fragments) : _mode(Mode::Empty), _count(0) {
        for (const auto& fragment : fragments) {
            eastl::visit(
                [this](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, PoolString>) {
                        *this += arg;
                    } else if constexpr (std::is_same_v<T, int>) {
                        *this += arg;
                    }
                },
                fragment);
        }
    }

    PoolStringChain& PoolStringChain::operator+=(PoolString ps) {
        if (_mode == Mode::Empty) {
            _mode = Mode::Atom;
            _storage.single = ps;
        } else if (_mode == Mode::Atom) {
            PoolString first = _storage.single;
            _mode = Mode::Compound;
            _storage.compound[0] = TaggedVal::FromPoolString(first);
            _storage.compound[1] = TaggedVal::FromPoolString(ps);
            _count = 2;
        } else if (_mode == Mode::Literal) {
            PoolString first = PoolString::Intern({_storage.view.p, _storage.view.s});
            _mode = Mode::Compound;
            _storage.compound[0] = TaggedVal::FromPoolString(first);
            _storage.compound[1] = TaggedVal::FromPoolString(ps);
            _count = 2;
        } else if (_mode == Mode::Compound) {
            if (_count < 3) {
                _storage.compound[_count++] = TaggedVal::FromPoolString(ps);
            } else {
                PoolString materialized = MaterializeToPoolString();
                _mode = Mode::Compound;
                _storage.compound[0] = TaggedVal::FromPoolString(materialized);
                _storage.compound[1] = TaggedVal::FromPoolString(ps);
                _count = 2;
            }
        }
        return *this;
    }

    PoolStringChain& PoolStringChain::operator+=(int value) {
        if (_mode == Mode::Empty) {
            _mode = Mode::Compound;
            _storage.compound[0] = TaggedVal::FromInt(value);
            _count = 1;
        } else if (_mode == Mode::Atom) {
            PoolString first = _storage.single;
            _mode = Mode::Compound;
            _storage.compound[0] = TaggedVal::FromPoolString(first);
            _storage.compound[1] = TaggedVal::FromInt(value);
            _count = 2;
        } else if (_mode == Mode::Literal) {
            PoolString first = PoolString::Intern({_storage.view.p, _storage.view.s});
            _mode = Mode::Compound;
            _storage.compound[0] = TaggedVal::FromPoolString(first);
            _storage.compound[1] = TaggedVal::FromInt(value);
            _count = 2;
        } else if (_mode == Mode::Compound) {
            if (_count < 3) {
                _storage.compound[_count++] = TaggedVal::FromInt(value);
            } else {
                PoolString materialized = MaterializeToPoolString();
                _mode = Mode::Compound;
                _storage.compound[0] = TaggedVal::FromPoolString(materialized);
                _storage.compound[1] = TaggedVal::FromInt(value);
                _count = 2;
            }
        }
        return *this;
    }

    eastl::string PoolStringChain::Materialize() const {
        eastl::string result;
        result.reserve(Size());

        switch (_mode) {
            case Mode::Empty: break;
            case Mode::Atom: {
                auto view = _storage.single.ToStringView();
                result.append(view.data(), view.size());
                break;
            }
            case Mode::Literal: {
                result.append(_storage.view.p, _storage.view.s);
                break;
            }
            case Mode::Compound: {
                for (uint8_t i = 0; i < _count; ++i) {
                    const auto& v = _storage.compound[i];
                    if (v.IsInt()) {
                        char buf[12];
                        int len = std::snprintf(buf, sizeof(buf), "%d", v.AsInt());
                        if (len > 0) {
                            result.append(buf, static_cast<size_t>(len));
                        }
                    } else {
                        auto view = v.AsPoolString().ToStringView();
                        result.append(view.data(), view.size());
                    }
                }
                break;
            }
        }
        return result;
    }

    PoolString PoolStringChain::MaterializeToPoolString() const {
        if (_mode == Mode::Atom) {
            return _storage.single;
        }
        if (_mode == Mode::Empty) {
            return PoolString();
        }
        return PoolString::Intern(Materialize());
    }

    uint64_t PoolStringChain::GetHash() const {
        if (_mode == Mode::Atom) {
            return _storage.single.HashValue();
        }
        if (_mode == Mode::Literal) {
            return String::GetHash(eastl::string_view(_storage.view.p, _storage.view.s));
        }
        if (_mode == Mode::Empty) {
            return String::GetEmptyHash();
        }

        uint64_t hash = String::hashOffset;
        for (uint8_t i = 0; i < _count; ++i) {
            const auto& v = _storage.compound[i];
            if (v.IsInt()) {
                BECore::HashCombine(hash, v.AsInt());
            } else {
                BECore::HashCombine(hash, v.AsPoolString().HashValue());
            }
        }
        return hash;
    }

    bool PoolStringChain::operator==(const PoolStringChain& other) const {
        if (_mode != other._mode) return false;
        if (_mode == Mode::Empty) return true;
        if (_mode == Mode::Atom) return _storage.single == other._storage.single;
        if (_mode == Mode::Literal) return eastl::string_view(_storage.view.p, _storage.view.s) == eastl::string_view(other._storage.view.p, other._storage.view.s);
        if (_mode == Mode::Compound) {
            if (_count != other._count) return false;
            for (uint8_t i = 0; i < _count; ++i) {
                if (_storage.compound[i].data != other._storage.compound[i].data) return false;
            }
            return true;
        }
        return false;
    }

    bool PoolStringChain::operator!=(const PoolStringChain& other) const {
        return !(*this == other);
    }

    bool PoolStringChain::operator==(const PoolString& other) const {
        if (_mode == Mode::Atom) {
            return _storage.single == other;
        }
        return false;
    }

    bool PoolStringChain::operator!=(const PoolString& other) const {
        return !(*this == other);
    }

}  // namespace BECore
