#include "FormatTest.h"

#include <BECore/Format/Format.h>
#include <BECore/Logger/Logger.h>

#include <Generated/FormatTest.gen.hpp>

namespace BECore::Tests {

    bool FormatTest::Run() {
        LOG_INFO("[FormatTest] Starting tests...");

        // Compile-time tests (static_assert)
        TestCompileTime();
        LOG_INFO("[FormatTest] Compile-time tests OK");

        // 1. Basic formatting to eastl::string
        eastl::string s1 = BECore::Format("Hello, {}!", "World");
        ASSERT(s1 == "Hello, World!");

        // 2. Numeric formatting
        eastl::string s2 = BECore::Format("Value: {:03d}", 42);
        ASSERT(s2 == "Value: 042");

        // 3. Multiple arguments
        eastl::string s3 = BECore::Format("{} + {} = {}", 1, 1, 2);
        ASSERT(s3 == "1 + 1 = 2");

        // 4. EASTL types formatting (using our custom formatters)
        eastl::string name = "Bagi";
        eastl::string_view suffix = "Engine";
        eastl::string s4 = BECore::Format("Welcome to {}{}", name, suffix);
        ASSERT(s4 == "Welcome to BagiEngine");

        // 5. FormatTo test
        eastl::fixed_string<char, 64> buffer;
        BECore::FormatTo(eastl::back_inserter(buffer), "Buffer: {}", 123);
        ASSERT(buffer == eastl::string_view("Buffer: 123"));

        // 6. Print and PrintLine (visual check)
        BECore::PrintLine("FormatTest: All assertions passed successfully!");
        BECore::Print("Formatted message with Print: {}\n", "Success");

        // 7. User-defined literal _format tests
        using namespace BECore::Literals;
        
        eastl::string s5 = "Hello, {}!"_format("World");
        ASSERT(s5 == "Hello, World!");

        eastl::string s6 = "Value: {}"_format(42);
        ASSERT(s6 == "Value: 42");

        eastl::string s7 = "{} + {} = {}"_format(2, 3, 5);
        ASSERT(s7 == "2 + 3 = 5");

        eastl::string s8 = "Formatted: {:03d}"_format(7);
        ASSERT(s8 == "Formatted: 007");

        // Test with EASTL types
        eastl::string name2 = "Test";
        eastl::string s9 = "Name: {}"_format(name2);
        ASSERT(s9 == "Name: Test");

        BECore::PrintLine("FormatTest: _format literal tests passed!");

        return true;
    }

}  // namespace BECore::Tests
