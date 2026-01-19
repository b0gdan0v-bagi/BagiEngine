#include <BECore/Tests/ReflectionEventTest.h>
#include <BECore/Assert/AssertMacros.h>
#include <Generated/ReflectionEventTest.gen.hpp>

namespace BECore::Tests {

    bool ReflectionEventTest::Run() {
        TestCompileTime();

        ASSERT(TestEventMeta(), "TestEventMeta failed");
        ASSERT(TestEventFields(), "TestEventFields failed");
        ASSERT(TestZeroOverhead(), "TestZeroOverhead failed");
        ASSERT(TestFieldIteration(), "TestFieldIteration failed");

        return true;
    }

    constexpr void ReflectionEventTest::TestCompileTime() {
        using namespace ReflectionTestEvents;

        // Events have reflection
        static_assert(HasReflection<TestEmptyEvent>);
        static_assert(HasReflection<TestDataEvent>);
        static_assert(HasReflection<TestMessageEvent>);

        // Static type names are correct
        static_assert(TestEmptyEvent::GetStaticTypeName() == "TestEmptyEvent");
        static_assert(TestDataEvent::GetStaticTypeName() == "TestDataEvent");
        static_assert(TestMessageEvent::GetStaticTypeName() == "TestMessageEvent");

        // Field counts are correct
        static_assert(TestEmptyEvent::GetStaticFieldCount() == 0);
        static_assert(TestDataEvent::GetStaticFieldCount() == 2);
        static_assert(TestMessageEvent::GetStaticFieldCount() == 2);

        // Events are default constructible
        static_assert(std::is_default_constructible_v<TestEmptyEvent>);
        static_assert(std::is_default_constructible_v<TestDataEvent>);
        static_assert(std::is_default_constructible_v<TestMessageEvent>);
    }

    bool ReflectionEventTest::TestEventMeta() {
        using namespace ReflectionTestEvents;

        // GetStaticTypeMeta returns valid ClassMeta
        auto meta1 = TestEmptyEvent::GetStaticTypeMeta();
        ASSERT(meta1, "ClassMeta should be valid");
        ASSERT(meta1.typeName == "TestEmptyEvent", "Type name should match");
        ASSERT(meta1.typeHash != 0, "Type hash should be non-zero");

        auto meta2 = TestDataEvent::GetStaticTypeMeta();
        ASSERT(meta2, "ClassMeta should be valid");
        ASSERT(meta2.typeName == "TestDataEvent", "Type name should match");
        ASSERT(meta2.typeHash != 0, "Type hash should be non-zero");

        auto meta3 = TestMessageEvent::GetStaticTypeMeta();
        ASSERT(meta3, "ClassMeta should be valid");
        ASSERT(meta3.typeName == "TestMessageEvent", "Type name should match");
        ASSERT(meta3.typeHash != 0, "Type hash should be non-zero");

        // Type hashes are unique
        ASSERT(meta1.typeHash != meta2.typeHash, "Type hashes should be unique");
        ASSERT(meta2.typeHash != meta3.typeHash, "Type hashes should be unique");
        ASSERT(meta1.typeHash != meta3.typeHash, "Type hashes should be unique");

        // GetStaticTypeHash works
        ASSERT(TestEmptyEvent::GetStaticTypeHash() == meta1.typeHash, "GetStaticTypeHash should match");
        ASSERT(TestDataEvent::GetStaticTypeHash() == meta2.typeHash, "GetStaticTypeHash should match");
        ASSERT(TestMessageEvent::GetStaticTypeHash() == meta3.typeHash, "GetStaticTypeHash should match");

        return true;
    }

    bool ReflectionEventTest::TestEventFields() {
        using namespace ReflectionTestEvents;

        // Empty event has no fields
        ASSERT(FieldCount<TestEmptyEvent>() == 0, "Empty event should have 0 fields");

        // Data event has 2 fields
        ASSERT(FieldCount<TestDataEvent>() == 2, "TestDataEvent should have 2 fields");

        // Check field info
        auto field0 = GetFieldInfo<TestDataEvent, 0>();
        ASSERT(field0.name == "value", "First field should be 'value'");

        auto field1 = GetFieldInfo<TestDataEvent, 1>();
        ASSERT(field1.name == "multiplier", "Second field should be 'multiplier'");

        // Message event has 2 fields
        ASSERT(FieldCount<TestMessageEvent>() == 2, "TestMessageEvent should have 2 fields");

        return true;
    }

    bool ReflectionEventTest::TestZeroOverhead() {
        using namespace ReflectionTestEvents;

        // Empty event should have minimal size (no _typeMeta pointer)
        // Note: sizeof(empty struct) is 1 in C++ (cannot be 0)
        ASSERT(sizeof(TestEmptyEvent) == 1, "Empty event should have size 1 (no overhead)");

        // Data event size should equal its fields (no _typeMeta overhead)
        // int32_t (4) + float (4) = 8 bytes
        ASSERT(sizeof(TestDataEvent) == 8, "TestDataEvent should be 8 bytes (no overhead)");

        // Compare with a BE_CLASS type that has _typeMeta pointer
        // (Those would be 8 bytes larger due to pointer)

        return true;
    }

    bool ReflectionEventTest::TestFieldIteration() {
        using namespace ReflectionTestEvents;

        // Test ForEachFieldStatic with empty event
        TestEmptyEvent emptyEvent;
        int emptyCount = 0;
        TestEmptyEvent::ForEachFieldStatic(emptyEvent, [&](auto, auto&) {
            emptyCount++;
        });
        ASSERT(emptyCount == 0, "Empty event should iterate 0 fields");

        // Test ForEachFieldStatic with data event
        TestDataEvent dataEvent{42, 2.5f};
        int dataCount = 0;
        int32_t capturedValue = 0;
        float capturedMultiplier = 0.0f;

        TestDataEvent::ForEachFieldStatic(dataEvent, [&](eastl::string_view name, const auto& value) {
            dataCount++;
            if (name == "value") {
                capturedValue = value;
            } else if (name == "multiplier") {
                capturedMultiplier = value;
            }
        });

        ASSERT(dataCount == 2, "TestDataEvent should iterate 2 fields");
        ASSERT(capturedValue == 42, "Should capture value field");
        ASSERT(capturedMultiplier == 2.5f, "Should capture multiplier field");

        // Test with message event
        PoolString helloStr = PoolString::Intern("Hello");
        TestMessageEvent msgEvent{helloStr, 123};
        int msgCount = 0;
        PoolString capturedMessage;
        int32_t capturedCode = 0;

        TestMessageEvent::ForEachFieldStatic(msgEvent, [&](eastl::string_view name, const auto& value) {
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

        ASSERT(msgCount == 2, "TestMessageEvent should iterate 2 fields");
        ASSERT(capturedMessage.ToStringView() == "Hello", "Should capture message field");
        ASSERT(capturedCode == 123, "Should capture code field");

        return true;
    }

}  // namespace BECore::Tests
