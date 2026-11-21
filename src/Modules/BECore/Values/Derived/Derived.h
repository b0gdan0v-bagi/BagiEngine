#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Derived/DerivedReads.h>
#include <concepts>
#include <cstdint>

namespace BECore {

    /**
     * @brief A value of any type that is built from a `Context` and rebuilt only when a declared input changed
     *
     * For what `CachedValue` cannot hold (a text, a list, a layout struct): the reads are declared once (`DerivedReads`) and the
     * builder is given to each `Get`, so the screen keeps no version key of its own. A call is a hit, with no allocation and no
     * indirect call, when the counters of the declared sources and `Context::VersionKey` of the declared levels are what they were at
     * the last build; the first call, a changed key, a moved counter and `Invalidate` build. A context rebuilt each frame over the same
     * objects and counters hits.
     *
     * The builder fills the result in place: it gets the last result, so a `String` or a `Vector` it clears keeps its capacity and a
     * rebuild that fits allocates nothing. Where asserts are on it also gets `Context::Restricted` to the declared levels, so a read of
     * an undeclared level fails an `ASSERT` instead of leaving a stale screen; a read through a captured pointer is not seen. The key
     * and the counters are taken after the build, so a builder that bumps a counter it declared does not rebuild forever (and its own
     * bump is not seen until another one moves).
     *
     * A declared level with no object or no counter (`Context::IsVersioned`) cannot say whether it changed: the value is rebuilt on
     * every call and a debug build logs it once per instance. Not thread-safe; movable, not copyable (a copy would carry a key
     * that belongs to the original's inputs).
     */
    template <typename T>
    requires std::default_initializable<T>&& std::movable<T> class Derived {
    public:
        explicit Derived(DerivedReads reads) : _reads(std::move(reads)) {}

        Derived(const Derived&) = delete;
        Derived& operator=(const Derived&) = delete;
        Derived(Derived&&) noexcept = default;
        Derived& operator=(Derived&&) noexcept = default;

        /// Another declaration: the result is stale, the warning of an unversioned context may be given again
        void Reset(DerivedReads reads) {
            _reads = std::move(reads);
            _warned = false;
            Invalidate();
        }

        /// The result now, rebuilt with @p build(result, context) when an input moved. @p build runs inside this call only
        [[nodiscard]] const T& Get(const Context& context, FunctionRef<void(T&, const Context&)> build) {
            if (_reads.IsEmpty()) {
                if (!_valid) {
                    Rebuild(context, build);
                }
                return _result;
            }
            const LevelMask mask = _reads.Mask();
            if (mask != 0 && !context.IsVersioned(mask)) {
                WarnUnversioned();
                Rebuild(context, build);
                // The key of a context that cannot be keyed says nothing about a later one
                _valid = false;
                return _result;
            }
            if (_valid && SourcesUnchanged() && (mask == 0 || context.VersionKey(mask) == _key)) {
                return _result;
            }
            Rebuild(context, build);
            return _result;
        }

        /// Forgets the key: the next `Get` builds
        void Invalidate() {
            _valid = false;
        }

        /// How many times the builder ran: what a test or a benchmark counts
        [[nodiscard]] uint32_t Rebuilds() const {
            return _rebuilds;
        }

        [[nodiscard]] const DerivedReads& Reads() const {
            return _reads;
        }

    private:
        [[nodiscard]] bool SourcesUnchanged() const {
            const Span<const uint64_t* const> sources = _reads.Sources();
            for (size_t i = 0; i < sources.size(); ++i) {
                if (*sources[i] != _sourceValues[i]) {
                    return false;
                }
            }
            return true;
        }

        void Rebuild(const Context& context, FunctionRef<void(T&, const Context&)> build) {
            const LevelMask mask = _reads.Mask();
#if ENGINE_ENABLE_ASSERTS
            const Context view = context.Restricted(mask);
            build(_result, view);
#else
            build(_result, context);
#endif
            _key = mask != 0 ? context.VersionKey(mask) : 0;
            const Span<const uint64_t* const> sources = _reads.Sources();
            for (size_t i = 0; i < sources.size(); ++i) {
                _sourceValues[i] = *sources[i];
            }
            _valid = true;
            ++_rebuilds;
        }

        /// Cold: the text is built on the first call only, and not at all where asserts are off
        void WarnUnversioned() {
#if ENGINE_ENABLE_ASSERTS
            if (!_warned) {
                _warned = true;
                const String message =
                    Format("Derived: a declared level (mask {}) has no object or no counter in this context, so the value is rebuilt on every call", static_cast<uint32_t>(_reads.Mask()));
                LOG_WARNING(message);
            }
#endif
        }

        DerivedReads _reads;
        T _result{};
        uint64_t _key = 0;
        Array<uint64_t, kMaxVersionSources> _sourceValues{};
        uint32_t _rebuilds = 0;
        bool _valid = false;
        [[maybe_unused]] bool _warned = false;
    };

}  // namespace BECore
