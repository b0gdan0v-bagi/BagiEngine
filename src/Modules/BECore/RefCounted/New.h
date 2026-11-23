#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <utility>

namespace BECore {

    /// The one way to make a ref-counted object: the first owner takes the count from 0 to 1.
    template <typename T, typename... Args>
    IntrusivePtr<T> New(Args&&... args) {
        return IntrusivePtr<T>(new T(std::forward<Args>(args)...));
    }

    template <typename T, typename... Args>
    [[deprecated("use New")]] IntrusivePtr<T> MakeIntrusive(Args&&... args) {
        return New<T>(std::forward<Args>(args)...);
    }

}  // namespace BECore
