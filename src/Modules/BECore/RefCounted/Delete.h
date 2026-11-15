#pragma once

namespace BECore {
    template <typename T>
    void Delete(T* ptr) {
        delete ptr;
    }
}  // namespace BECore
