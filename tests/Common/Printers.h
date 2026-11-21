#pragma once

#include <BECore/Math/Color.h>
#include <BECore/PoolString/PoolString.h>
#include <gtest/gtest.h>
#include <ostream>

// gtest cannot print EASTL strings (of any allocator: String is not eastl::string), PoolString or Color on its own (it falls back to a byte dump),
// so failure messages would be unreadable. ADL finds PrintTo only in the type's own namespace.
namespace eastl {

    /// `ASSERT_TRUE(x) << message` takes a String as it takes a std::string
    template <typename Alloc>
    std::ostream& operator<<(std::ostream& os, const basic_string<char, Alloc>& value) {
        return os.write(value.data(), static_cast<std::streamsize>(value.size()));
    }

    template <typename Alloc>
    void PrintTo(const basic_string<char, Alloc>& value, std::ostream* os) {
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

    /// Color has no `operator==`: compare with `EXPECT_PRED2(ColorsEqual, a, b)` (UI/UIColor.h), which prints both through this
    inline void PrintTo(const Color& value, std::ostream* os) {
        *os << "Color{" << static_cast<int>(value.r) << ", " << static_cast<int>(value.g) << ", " << static_cast<int>(value.b) << ", " << static_cast<int>(value.a) << "}";
    }

}  // namespace BECore
