#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace BECore {

    template <typename TSignature>
    class FunctionRef;

    /**
     * @brief A non-owning view of a callable of the signature `R(Args...)`: two pointers, trivially copyable, never allocates.
     *
     * For a parameter that is called only during the call (`ForEach(n, [&](int index) {...})`): a temporary lambda lives to the end
     * of the full expression that calls, so it is enough. **Never store one** (a member, a captured value, a returned one): it
     * dangles as soon as the callable it points at is gone. A callable object is referenced, so it must outlive every call; a function
     * or a function pointer is copied into the view and has no such limit. `operator()` is const and calls the referenced object as
     * non-const, so a `mutable` lambda lvalue works. A `FunctionRef` built from a `FunctionRef` is a plain copy, not a view of a view.
     */
    template <typename R, typename... Args>
    class FunctionRef<R(Args...)> {
        template <typename TCallable>
        static constexpr bool kIsFunctionPointer = std::is_pointer_v<TCallable> && std::is_function_v<std::remove_pointer_t<TCallable>>;

        // The first of the two constraints of a constructor: the traits of the second must never see this class
        template <typename TCallable>
        static constexpr bool kIsOther = !std::is_same_v<std::remove_cvref_t<TCallable>, FunctionRef>;

        template <typename TCallable>
        static constexpr bool kIsObject = std::is_class_v<std::remove_cvref_t<TCallable>> && std::is_invocable_r_v<R, std::remove_reference_t<TCallable>&, Args...>;

        template <typename TCallable>
        static constexpr bool kIsFunction = kIsFunctionPointer<std::decay_t<TCallable>> && std::is_invocable_r_v<R, std::decay_t<TCallable>, Args...>;

    public:
        FunctionRef() = delete;

        /// A callable object (lambda, functor, a `std::function`): referenced
        template <typename TCallable>
        requires(kIsOther<TCallable>&& kIsObject<TCallable>) FunctionRef(TCallable&& callable) noexcept : _call(&CallObject<std::remove_reference_t<TCallable>>) {
            _target.object = const_cast<void*>(static_cast<const void*>(std::addressof(callable)));
        }

        /// A free function or a function pointer: its address is copied
        template <typename TCallable>
        requires(kIsOther<TCallable>&& kIsFunction<TCallable>) FunctionRef(TCallable&& callable) noexcept : _call(&CallFunction<std::decay_t<TCallable>>) {
            _target.function = reinterpret_cast<void (*)()>(static_cast<std::decay_t<TCallable>>(callable));
        }

        FunctionRef(const FunctionRef&) noexcept = default;
        FunctionRef& operator=(const FunctionRef&) noexcept = default;

        R operator()(Args... args) const {
            return _call(_target, std::forward<Args>(args)...);
        }

    private:
        union Target {
            void* object;
            void (*function)();
        };

        using Call = R (*)(const Target& target, Args&&... args);

        template <typename TObject>
        static R CallObject(const Target& target, Args&&... args) {
            return std::invoke_r<R>(*static_cast<TObject*>(target.object), std::forward<Args>(args)...);
        }

        template <typename TFunctionPointer>
        static R CallFunction(const Target& target, Args&&... args) {
            return std::invoke_r<R>(reinterpret_cast<TFunctionPointer>(target.function), std::forward<Args>(args)...);
        }

        Target _target;
        Call _call;
    };

}  // namespace BECore
