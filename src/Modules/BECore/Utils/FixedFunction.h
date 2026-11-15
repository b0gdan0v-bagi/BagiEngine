#pragma once

#include <BECore/Utils/MoveOnlyFunction.h>

namespace BECore {

    /**
     * @brief A MoveOnlyFunction that never allocates: a callable that does not fit the buffer of @p InlineBytes (see
     * `kFunctionFitsInline`) is rejected at compile time, so a callback that must stay off the heap says so in its type.
     *
     * Not `eastl::fixed_function`: it copies its callable, so it cannot hold a move-only capture. The constraint of the constructor
     * (not a `static_assert`) makes `std::is_constructible_v` say false, and the compiler's note names the callable and the buffer.
     */
    template <size_t InlineBytes, typename TSignature>
    using FixedFunction = detail::FunctionImpl<TSignature, InlineBytes, false>;

}  // namespace BECore
