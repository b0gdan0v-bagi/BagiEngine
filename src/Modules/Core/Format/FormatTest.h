#pragma once

#include <Core/Format/Format.h>
#include <Core/Assert/AssertMacros.h>
#include <EASTL/fixed_string.h>
#include <iostream>

namespace Core::Tests {

    inline void FormatTest() {
        std::cout << "[FormatTest] Starting tests..." << std::endl;

        // 1. Basic formatting to eastl::string
        eastl::string s1 = Core::Format("Hello, {}!", "World");
        ASSERT(s1 == "Hello, World!");

        // 2. Numeric formatting
        eastl::string s2 = Core::Format("Value: {:03d}", 42);
        ASSERT(s2 == "Value: 042");

        // 3. Multiple arguments
        eastl::string s3 = Core::Format("{} + {} = {}", 1, 1, 2);
        ASSERT(s3 == "1 + 1 = 2");

        // 4. EASTL types formatting (using our custom formatters)
        eastl::string name = "Bagi";
        eastl::string_view suffix = "Engine";
        eastl::string s4 = Core::Format("Welcome to {}{}", name, suffix);
        ASSERT(s4 == "Welcome to BagiEngine");

        // 5. FormatTo test
        eastl::fixed_string<char, 64> buffer;
        Core::FormatTo(eastl::back_inserter(buffer), "Buffer: {}", 123);
        ASSERT(buffer == eastl::string_view("Buffer: 123"));

        // 6. Print and PrintLine (visual check)
        Core::PrintLine("FormatTest: All assertions passed successfully!");
        Core::Print("Formatted message with Print: {}\n", "Success");
    }

} // namespace Core::Tests
