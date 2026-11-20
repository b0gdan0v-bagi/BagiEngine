#include "ReflectionLoadReserveTest.h"

#include <BECore/FactoryInitializeTest.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Generated/ReflectionLoadReserveTest.gen.hpp>

namespace BECore::Tests {

    namespace {

        constexpr size_t kNumberCount = 1000;
        constexpr size_t kElementCount = 100;
        constexpr size_t kFixedCapacity = 3;
        constexpr size_t kOverCapacity = 5;
        // Measured with the tracker: 6 allocations for the load of 1000 numbers (the vector's one block and the reader's bookkeeping);
        // without the reserves the vector and the element list doubled about eleven times each (26 in all).
        constexpr uint64_t kMaxLoadAllocs = 10;
        // Measured: the walk over 1000 elements allocates 5 times (the element list, the array and node stacks, the path), none per
        // element; before the element list was counted first it doubled about eleven times as well.
        constexpr uint64_t kMaxWalkAllocs = 8;

        ReserveHolder MakeHolder() {
            ReserveHolder holder;
            for (size_t i = 0; i < kNumberCount; ++i) {
                holder.numbers.push_back(static_cast<int32_t>(i));
            }
            for (size_t i = 0; i < kElementCount; ++i) {
                ReserveElement element;
                element.id = static_cast<int32_t>(i);
                element.weight = static_cast<float>(i);
                holder.elements.push_back(element);
            }
            return holder;
        }

        void ExpectExactCapacity(const ReserveHolder& loaded) {
            ASSERT_EQ(loaded.numbers.size(), kNumberCount);
            ASSERT_EQ(loaded.elements.size(), kElementCount);
            EXPECT_EQ(loaded.numbers.capacity(), loaded.numbers.size());
            EXPECT_EQ(loaded.elements.capacity(), loaded.elements.size());
            EXPECT_EQ(loaded.numbers.back(), static_cast<int32_t>(kNumberCount - 1));
            EXPECT_EQ(loaded.elements.back().id, static_cast<int32_t>(kElementCount - 1));
        }

        template <typename THolder>
        String WriteXml(const THolder& holder) {
            XmlSerializer serializer;
            if (serializer.BeginObject("Holder")) {
                holder.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        template <typename THolder>
        Vector<uint8_t> WriteBinary(const THolder& holder) {
            BinarySerializer serializer;
            if (serializer.BeginObject("Holder")) {
                holder.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        /// A file with @p count numbers under `items`: the same field name as the fixed holder, which has room for fewer
        String NumbersXml(size_t count) {
            ReserveSmallHolder holder;
            for (size_t i = 0; i < count; ++i) {
                holder.items.push_back(static_cast<int32_t>(i));
            }
            return WriteXml(holder);
        }

    }  // namespace

    TEST(ReflectionLoadReserveTest, XmlArraysAreExactlyAsLargeAsTheirContent) {
        const String xml = WriteXml(MakeHolder());
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ReserveHolder loaded;
        ASSERT_TRUE(deserializer.BeginObject("Holder"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        ExpectExactCapacity(loaded);
    }

    TEST(ReflectionLoadReserveTest, BinaryArraysAreExactlyAsLargeAsTheirContent) {
        const Vector<uint8_t> data = WriteBinary(MakeHolder());
        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(data));
        ReserveHolder loaded;
        ASSERT_TRUE(deserializer.BeginObject("Holder"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        ExpectExactCapacity(loaded);
    }

    TEST(ReflectionLoadReserveTest, AFactoryArrayReservesTheCountAndKeepsTheEnabledElements) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(R"(<root><probes>
                <probe type="InitProbePlain" id="1"/>
                <probe type="InitProbePlain" enabled="false" id="2"/>
                <probe type="InitProbePlain" id="3"/>
            </probes></root>)"));
        InitProbeList list;
        list.Deserialize(deserializer);
        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list.probes.size(), 2u);
        // The count includes the disabled element: an upper bound, never shrunk
        EXPECT_GE(list.probes.capacity(), list.probes.size());
        EXPECT_EQ(list.probes[0]->Cast<InitProbePlain>()->id, 1);
        EXPECT_EQ(list.probes[1]->Cast<InitProbePlain>()->id, 3);
    }

    TEST(ReflectionLoadReserveTest, AFixedVectorKeepsWhatFitsAndReportsTheRest) {
        const String xml = NumbersXml(kOverCapacity);
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ReserveFixedHolder loaded;
        ASSERT_TRUE(deserializer.BeginObject("Holder"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();

        ASSERT_EQ(loaded.items.size(), kFixedCapacity);
        EXPECT_EQ(loaded.items[0], 0);
        EXPECT_EQ(loaded.items[kFixedCapacity - 1], static_cast<int32_t>(kFixedCapacity - 1));
        ASSERT_TRUE(deserializer.HasErrors());
        EXPECT_TRUE(Contains(deserializer.GetErrors()[0].errorMessage, "Data lost"));
    }

    TEST(ReflectionLoadReserveTest, AFixedVectorWithOverflowGrowsOnceForALongerFile) {
        const String xml = NumbersXml(kOverCapacity);
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ReserveSmallHolder loaded;
        ASSERT_TRUE(deserializer.BeginObject("Holder"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(loaded.items.size(), kOverCapacity);
        EXPECT_EQ(loaded.items.capacity(), kOverCapacity);
    }

    TEST(ReflectionLoadReserveTest, LoadingANumberArrayFromAParsedDocumentAllocatesItsBlockAndLittleElse) {
        ReserveHolder source;
        for (size_t i = 0; i < kNumberCount; ++i) {
            source.numbers.push_back(static_cast<int32_t>(i));
        }
        const String xml = WriteXml(source);
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ASSERT_TRUE(deserializer.BeginObject("Holder"));
        ReserveHolder loaded;
        const AllocCount allocs = CountAllocations([&] { loaded.Deserialize(deserializer); });
        deserializer.EndObject();

        ASSERT_EQ(loaded.numbers.size(), kNumberCount);
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_LE(allocs.count, kMaxLoadAllocs);
    }

    TEST(ReflectionLoadReserveTest, TheXmlReaderAllocatesNothingPerElementOfAnArray) {
        const String xml = WriteXml(MakeHolder());
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        ASSERT_TRUE(deserializer.BeginObject("Holder"));
        size_t count = 0;
        size_t visited = 0;
        const AllocCount allocs = CountAllocations([&] {
            if (deserializer.BeginArray("numbers", "item", count)) {
                for (size_t i = 0; i < count; ++i) {
                    if (deserializer.BeginArrayElement()) {
                        ++visited;
                        deserializer.EndArrayElement();
                    }
                }
                deserializer.EndArray();
            }
        });
        deserializer.EndObject();

        ASSERT_EQ(visited, kNumberCount);
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_LE(allocs.count, kMaxWalkAllocs);
    }

}  // namespace BECore::Tests
