#pragma once

#include <gtest/gtest.h>
#include <ostream>

// gtest cannot print EASTL strings or PoolString on its own (it falls back to a byte dump),
// so failure messages would be unreadable. ADL finds PrintTo only in the type's own namespace.
namespace eastl {

    inline void PrintTo(const string& value, std::ostream* os) {
        *os << '"' << value.c_str() << '"';
    }

    inline void PrintTo(const string_view& value, std::ostream* os) {
        *os << '"';
        os->write(value.data(), static_cast<std::streamsize>(value.size()));
        *os << '"';
    }

}  // namespace eastl

namespace BECore {

    inline void PrintTo(const PoolString& value, std::ostream* os) {
        *os << "PoolString(\"" << value.CStr() << "\")";
    }

}  // namespace BECore
