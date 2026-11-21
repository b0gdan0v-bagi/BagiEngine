#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <bit>
#include <cstdint>

namespace BECore {

    /**
     * @brief A `Value<T>` that keeps the last result of its formula and recomputes only when something the formula reads has changed
     *
     * `Evaluate` returns what `Value::Evaluate` returns. A `Const` and a `Slot` are delegated (a read is cheaper than a key). A `Program`
     * is run once and its result kept while the levels it reads hold what they held then (`Context::Matches`: the same four parts as
     * `Context::VersionKey`, compared exactly): the owner of each object bumps the counter given to `Context::Set` when the object
     * changes, as for any other user of the key. A context rebuilt each frame over the same objects and counters matches, so it hits. A program
     * with no `Reads()` (a hand-assembled one; the compiler folds those to a `Const`) is run once and kept until `Invalidate` or `Reset`.
     *
     * The cache is valid only when every level the program reads has an object and a counter (`Context::IsVersioned`). With another
     * context the program runs on every call, `Computations()` rises each time and a debug build says so once per instance in the log.
     *
     * Not thread-safe: one owner per instance (the program pool stays thread-safe). Not a field type: no reflection, nothing is saved, a
     * content file holds a `Value<T>` and the code that evaluates it holds the `CachedValue`. A call that hits computes and allocates nothing.
     */
    template <ValueType T>
    class CachedValue {
    public:
        /// Takes `Reads()` of @p value once, so the program pool is not asked on every call
        explicit CachedValue(const Value<T>& value) {
            Reset(value);
        }

        /// Another formula: the cache is empty and the warning of an unversioned context may be given again
        void Reset(const Value<T>& value) {
            _value = value;
            _reads = value.Reads();
            _stamps.resize(static_cast<size_t>(std::popcount(_reads)));
            _warned = false;
            Invalidate();
        }

        /// The result now. Not `const`: it keeps the result
        [[nodiscard]] T Evaluate(const Context& context) {
            if (_value.Kind() != ValueKind::Program) {
                return _value.Evaluate(context);
            }
            if (_reads == 0) {
                return _valid ? _result : Compute(context);
            }
            // A kept result implies versioned levels: the stamp below is taken only after the check
            if (_valid && context.Matches(_reads, _stamps.data())) {
                return _result;
            }
            if (!context.IsVersioned(_reads)) {
                WarnUnversioned();
                _valid = false;
                return Compute(context);
            }
            const T result = Compute(context);
            context.Stamp(_reads, _stamps.data());
            return result;
        }

        /// Forgets the kept result: the next `Evaluate` runs the program
        void Invalidate() {
            _valid = false;
        }

        [[nodiscard]] const Value<T>& Source() const {
            return _value;
        }

        /// How many times the program ran (a `Const` and a `Slot` never do): what a test or a benchmark counts
        [[nodiscard]] uint32_t Computations() const {
            return _computations;
        }

    private:
        T Compute(const Context& context) {
            _result = _value.Evaluate(context);
            _valid = true;
            ++_computations;
            return _result;
        }

        /// Cold: the text is built on the first call only, and not at all where asserts are off (a release build)
        void WarnUnversioned() {
#if ENGINE_ENABLE_ASSERTS
            if (!_warned) {
                _warned = true;
                const String message = Format("CachedValue: the formula '{}' reads a level that has no object or no counter in this context, so it runs on every call", _value.Path().ToStringView());
                LOG_WARNING(message);
            }
#endif
        }

        Value<T> _value;
        T _result{};
        Vector<LevelStamp> _stamps;
        uint32_t _computations = 0;
        LevelMask _reads = 0;
        bool _valid = false;
        [[maybe_unused]] bool _warned = false;
    };

}  // namespace BECore
