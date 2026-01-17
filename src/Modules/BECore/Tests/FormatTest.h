#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Format/Format.h>
#include <BECore/Assert/AssertMacros.h>
#include <EASTL/fixed_string.h>
#include <iostream>

namespace BECore::Tests {

    /**
     * @brief Test for BECore::Format functionality
     */
    class FormatTest : public ITest {
    public:
        FormatTest() = default;
        ~FormatTest() override = default;

        const char* GetName() const override {
            return "FormatTest";
        }

        bool Run() override {
            std::cout << "[FormatTest] Starting tests..." << std::endl;

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

            return true;
        }
    };

}  // namespace BECore::Tests
