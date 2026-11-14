#include "ReflectionEventTest.h"

#include <Common/Printers.h>
#include <Generated/ReflectionEventTest.gen.hpp>

namespace BECore::Tests {

    using namespace ReflectionTestEvents;

    // Compile-time checks: a regression breaks the build.
    // Events have reflection
    static_assert(HasReflection<TestEmptyEvent>);
    static_assert(HasReflection<TestDataEvent>);
    static_assert(HasReflection<TestMessageEvent>);

    // Events are default constructible
    static_assert(std::is_default_constructible_v<TestEmptyEvent>);
    static_assert(std::is_default_constructible_v<TestDataEvent>);
    static_assert(std::is_default_constructible_v<TestMessageEvent>);

    TEST(ReflectionEventTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(ReflectionEventTest, StaticTypeNamesAndFieldCounts) {
        EXPECT_EQ(TestEmptyEvent::GetStaticTypeName(), "TestEmptyEvent");
        EXPECT_EQ(TestDataEvent::GetStaticTypeName(), "TestDataEvent");
        EXPECT_EQ(TestMessageEvent::GetStaticTypeName(), "TestMessageEvent");

        EXPECT_EQ(TestEmptyEvent::GetStaticFieldCount(), 0u);
        EXPECT_EQ(TestDataEvent::GetStaticFieldCount(), 2u);
        EXPECT_EQ(TestMessageEvent::GetStaticFieldCount(), 2u);
    }

    TEST(ReflectionEventTest, EventMeta) {
        auto meta1 = TestEmptyEvent::GetStaticTypeMeta();
        EXPECT_TRUE(static_cast<bool>(meta1)) << "ClassMeta should be valid";
        EXPECT_EQ(meta1.typeName, "TestEmptyEvent");
        EXPECT_NE(meta1.typeHash, 0u) << "Type hash should be non-zero";

        auto meta2 = TestDataEvent::GetStaticTypeMeta();
        EXPECT_TRUE(static_cast<bool>(meta2)) << "ClassMeta should be valid";
        EXPECT_EQ(meta2.typeName, "TestDataEvent");
        EXPECT_NE(meta2.typeHash, 0u) << "Type hash should be non-zero";

        auto meta3 = TestMessageEvent::GetStaticTypeMeta();
        EXPECT_TRUE(static_cast<bool>(meta3)) << "ClassMeta should be valid";
        EXPECT_EQ(meta3.typeName, "TestMessageEvent");
        EXPECT_NE(meta3.typeHash, 0u) << "Type hash should be non-zero";

        // Type hashes are unique
        EXPECT_NE(meta1.typeHash, meta2.typeHash);
        EXPECT_NE(meta2.typeHash, meta3.typeHash);
        EXPECT_NE(meta1.typeHash, meta3.typeHash);

        // GetStaticTypeHash matches the meta
        EXPECT_EQ(TestEmptyEvent::GetStaticTypeHash(), meta1.typeHash);
        EXPECT_EQ(TestDataEvent::GetStaticTypeHash(), meta2.typeHash);
        EXPECT_EQ(TestMessageEvent::GetStaticTypeHash(), meta3.typeHash);
    }

    TEST(ReflectionEventTest, EventFields) {
        EXPECT_EQ(FieldCount<TestEmptyEvent>(), 0u) << "Empty event should have no fields";
        EXPECT_EQ(FieldCount<TestDataEvent>(), 2u);
        EXPECT_EQ(FieldCount<TestMessageEvent>(), 2u);

        auto field0 = GetFieldInfo<TestDataEvent, 0>();
        EXPECT_EQ(field0.name, "value");

        auto field1 = GetFieldInfo<TestDataEvent, 1>();
        EXPECT_EQ(field1.name, "multiplier");
    }

    TEST(ReflectionEventTest, ZeroOverhead) {
        // sizeof(empty struct) is 1 in C++ (cannot be 0); no vptr
        EXPECT_EQ(sizeof(TestEmptyEvent), 1u) << "Empty event should have size 1 (no overhead)";

        // int32_t (4) + float (4) = 8 bytes, no vptr
        EXPECT_EQ(sizeof(TestDataEvent), 8u) << "TestDataEvent should be 8 bytes (no overhead)";
    }

    TEST(ReflectionEventTest, FieldIteration) {
        // Empty event
        TestEmptyEvent emptyEvent;
        int emptyCount = 0;
        TestEmptyEvent::ForEachFieldStatic(emptyEvent, [&](auto, auto&) { emptyCount++; });
        EXPECT_EQ(emptyCount, 0);

        // Data event
        TestDataEvent dataEvent{42, 2.5f};
        int dataCount = 0;
        int32_t capturedValue = 0;
        float capturedMultiplier = 0.0f;

        TestDataEvent::ForEachFieldStatic(dataEvent, [&](StringView name, const auto& value) {
            dataCount++;
            if (name == "value") {
                capturedValue = value;
            } else if (name == "multiplier") {
                capturedMultiplier = value;
            }
        });

        EXPECT_EQ(dataCount, 2);
        EXPECT_EQ(capturedValue, 42);
        EXPECT_FLOAT_EQ(capturedMultiplier, 2.5f);

        // Message event
        PoolString helloStr = PoolString::Intern("Hello");
        TestMessageEvent msgEvent{helloStr, 123};
        int msgCount = 0;
        PoolString capturedMessage;
        int32_t capturedCode = 0;

        TestMessageEvent::ForEachFieldStatic(msgEvent, [&](StringView name, const auto& value) {
            msgCount++;
            if (name == "message") {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, PoolString>) {
                    capturedMessage = value;
                }
            } else if (name == "code") {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, int32_t>) {
                    capturedCode = value;
                }
            }
        });

        EXPECT_EQ(msgCount, 2);
        EXPECT_EQ(capturedMessage.ToStringView(), "Hello");
        EXPECT_EQ(capturedCode, 123);
    }

}  // namespace BECore::Tests
