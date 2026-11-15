#pragma once

#include <EASTL/shared_ptr.h>

namespace BECore {

    /// For the fakes of the tests that really share one object between parties. The engine itself shares through IntrusivePtr
    /// (BECore/RefCounted); these are not in BECore/Types so that no engine source reaches for them.
    template <typename T>
    using SharedPtr = eastl::shared_ptr<T>;  //-NoStd: the test-only alias itself

    template <typename T, typename... Args>
    [[nodiscard]] SharedPtr<T> MakeShared(Args&&... args) {
        return eastl::make_shared<T>(eastl::forward<Args>(args)...);  //-NoStd: the test-only alias itself
    }

}  // namespace BECore
