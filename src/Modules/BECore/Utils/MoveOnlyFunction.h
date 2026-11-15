#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace BECore {

    /// The inline buffer of a MoveOnlyFunction that names none: the captures of tasks, control handlers and continuations are 8 to 16
    /// bytes, and 48 makes `sizeof(MoveOnlyFunction)` one cache line with its two pointers. A capture that holds a MoveOnlyFunction of
    /// the same type never fits (it is larger than the buffer): that is the heap path.
    inline constexpr size_t kDefaultFunctionInlineBytes = 48;

    namespace detail {

        /// The buffer is aligned like a pointer: a callable that needs more goes to the heap instead of padding every function.
        inline constexpr size_t kFunctionInlineAlign = alignof(void*);

        /// A callable of type @p TCallable lives in the buffer when it is small enough, aligned enough and cannot throw while moving.
        template <typename TCallable, size_t InlineBytes>
        inline constexpr bool kFitsInline = sizeof(TCallable) <= InlineBytes && alignof(TCallable) <= kFunctionInlineAlign && std::is_nothrow_move_constructible_v<TCallable>;

    }  // namespace detail

    /// True when a callable of the type @p TCallable stays in the buffer of @p InlineBytes of a MoveOnlyFunction; false means one heap allocation.
    template <typename TCallable, size_t InlineBytes = kDefaultFunctionInlineBytes>
    inline constexpr bool kFunctionFitsInline = detail::kFitsInline<std::decay_t<TCallable>, InlineBytes>;

    namespace detail {

        template <typename TSignature, size_t InlineBytes, bool AllowHeap>
        class FunctionImpl;

        /// The one implementation of MoveOnlyFunction (@p AllowHeap) and FixedFunction (no heap path: a callable that does not fit is not accepted)
        template <typename R, typename... Args, size_t InlineBytes, bool AllowHeap>
        class FunctionImpl<R(Args...), InlineBytes, AllowHeap> {
            static_assert(!AllowHeap || InlineBytes >= sizeof(void*), "the buffer holds the pointer of a heap callable");

            template <typename TCallable>
            static constexpr bool kIsOther = !std::is_same_v<std::decay_t<TCallable>, FunctionImpl>;

            /// What the constructor and the assignment take besides a function of this type (checked first, so the traits never see this class): a callable invocable with the signature, and for a
            /// function without a heap path only one that fits
            template <typename TCallable>
            static constexpr bool kAccepts = std::is_invocable_r_v<R, std::decay_t<TCallable>&, Args...> && std::is_constructible_v<std::decay_t<TCallable>, TCallable> &&
                                             (AllowHeap || kFitsInline<std::decay_t<TCallable>, InlineBytes>);

        public:
            FunctionImpl() noexcept = default;
            FunctionImpl(std::nullptr_t) noexcept {}

            template <typename TCallable>
            requires(kIsOther<TCallable>&& kAccepts<TCallable>) FunctionImpl(TCallable&& callable) {
                Emplace(std::forward<TCallable>(callable));
            }

            FunctionImpl(const FunctionImpl&) = delete;
            FunctionImpl& operator=(const FunctionImpl&) = delete;

            FunctionImpl(FunctionImpl&& other) noexcept {
                Take(other);
            }

            FunctionImpl& operator=(FunctionImpl&& other) noexcept {
                if (this != &other) {
                    Reset();
                    Take(other);
                }
                return *this;
            }

            FunctionImpl& operator=(std::nullptr_t) noexcept {
                Reset();
                return *this;
            }

            template <typename TCallable>
            requires(kIsOther<TCallable>&& kAccepts<TCallable>) FunctionImpl& operator=(TCallable&& callable) {
                Reset();
                Emplace(std::forward<TCallable>(callable));
                return *this;
            }

            ~FunctionImpl() {
                Reset();
            }

            /// Destroys the stored callable; the function is empty afterwards
            void Reset() noexcept {
                if (_ops != nullptr) {
                    _ops->destroy(_buffer);
                }
                _invoke = nullptr;
                _ops = nullptr;
            }

            explicit operator bool() const noexcept {
                return _invoke != nullptr;
            }

            friend bool operator==(const FunctionImpl& function, std::nullptr_t) noexcept {
                return function._invoke == nullptr;
            }

            R operator()(Args... args) const {
                ASSERT(_invoke != nullptr, "function called while empty");
                return _invoke(_buffer, std::forward<Args>(args)...);
            }

        private:
            using Invoke = R (*)(void* buffer, Args&&... args);

            /// What the type-erased callable needs besides the call; one static table per callable type
            struct Ops {
                void (*relocate)(void* from, void* to) noexcept;  ///< moves the callable into the empty @p to and leaves @p from without one
                void (*destroy)(void* buffer) noexcept;
            };

            template <typename TCallable>
            static TCallable* InlineCallable(void* buffer) noexcept {
                return std::launder(static_cast<TCallable*>(buffer));
            }

            template <typename TCallable>
            static TCallable*& HeapPointer(void* buffer) noexcept {
                return *std::launder(static_cast<TCallable**>(buffer));
            }

            template <typename TCallable>
            static R InvokeInline(void* buffer, Args&&... args) {
                return std::invoke_r<R>(*InlineCallable<TCallable>(buffer), std::forward<Args>(args)...);
            }

            template <typename TCallable>
            static R InvokeHeap(void* buffer, Args&&... args) {
                return std::invoke_r<R>(*HeapPointer<TCallable>(buffer), std::forward<Args>(args)...);
            }

            template <typename TCallable>
            static constexpr Ops kInlineOps{
                [](void* from, void* to) noexcept {
                    TCallable* source = InlineCallable<TCallable>(from);
                    ::new (to) TCallable(std::move(*source));
                    source->~TCallable();
                },
                [](void* buffer) noexcept { InlineCallable<TCallable>(buffer)->~TCallable(); },
            };

            template <typename TCallable>
            static constexpr Ops kHeapOps{
                [](void* from, void* to) noexcept { ::new (to) TCallable*(HeapPointer<TCallable>(from)); },
                [](void* buffer) noexcept { delete HeapPointer<TCallable>(buffer); },
            };

            /// True for what holds no callable: a null function or member pointer, or an empty function object that converts to bool
            template <typename TCallable>
            static bool IsEmptyCallable(const TCallable& callable) {
                if constexpr (std::is_pointer_v<TCallable> || std::is_member_pointer_v<TCallable>) {
                    return callable == nullptr;
                } else if constexpr (std::is_class_v<TCallable> && requires { static_cast<bool>(callable); }) {
                    return !static_cast<bool>(callable);
                } else {
                    return false;
                }
            }

            template <typename TCallable>
            void Emplace(TCallable&& callable) {
                using Stored = std::decay_t<TCallable>;
                if (IsEmptyCallable(callable)) {
                    return;
                }
                if constexpr (detail::kFitsInline<Stored, InlineBytes>) {
                    ::new (static_cast<void*>(_buffer)) Stored(std::forward<TCallable>(callable));
                    _invoke = &InvokeInline<Stored>;
                    _ops = &kInlineOps<Stored>;
                } else {
                    static_assert(AllowHeap);
                    // The one place that owns a raw allocation: the buffer holds only the pointer, kHeapOps deletes it.
                    ::new (static_cast<void*>(_buffer)) Stored*(new Stored(std::forward<TCallable>(callable)));
                    _invoke = &InvokeHeap<Stored>;
                    _ops = &kHeapOps<Stored>;
                }
            }

            /// Moves the callable of @p other in; this one is empty before and @p other is empty after
            void Take(FunctionImpl& other) noexcept {
                if (other._ops != nullptr) {
                    other._ops->relocate(other._buffer, _buffer);
                }
                _invoke = other._invoke;
                _ops = other._ops;
                other._invoke = nullptr;
                other._ops = nullptr;
            }

            alignas(detail::kFunctionInlineAlign) mutable unsigned char _buffer[InlineBytes];
            Invoke _invoke = nullptr;
            const Ops* _ops = nullptr;
        };

    }  // namespace detail

    /**
     * @brief An owning, move-only callable of the signature `R(Args...)`: a `std::function` that may hold a move-only capture.
     *
     * A callable that fits the buffer (`kFunctionFitsInline`) lives inside the object; any other is allocated once through the global
     * `operator new`, which `AllocTracker` counts, so the heap path is visible in a profile and never a compile error. Moving
     * leaves the source empty. `operator()` is const and calls the stored callable as non-const, like `std::function`: a `mutable`
     * lambda keeps its state over calls. Calling an empty function is an `ASSERT`ed contract violation.
     *
     * Not `std::move_only_function`: it promises no buffer size, so the allocation of a capture could not be reasoned about, and
     * libc++ has it only since LLVM 19 (the one of `external/emsdk` was not available to check the web build). Not
     * `eastl::function`: it copies its callable and so rejects a move-only capture.
     *
     * Constructed from an empty function pointer, `std::function`, `eastl::function` or `MoveOnlyFunction` the result is empty. A
     * `MoveOnlyFunction` of another buffer size is moved in as a callable: its storage is never shared.
     */
    template <typename TSignature, size_t InlineBytes = kDefaultFunctionInlineBytes>
    using MoveOnlyFunction = detail::FunctionImpl<TSignature, InlineBytes, true>;

}  // namespace BECore
