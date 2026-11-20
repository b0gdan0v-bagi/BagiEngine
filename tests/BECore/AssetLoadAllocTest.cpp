#include <BECore/PoolString/PoolStringEntry.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Common/AllocCheck.h>
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSpriteCount = 2000;
        constexpr int kRepetitions = 5;

        /// The tests of one process share the pool, so a text is new to it only once: every file carries its own salt
        uint32_t NextSalt() {
            static uint32_t salt = 0;
            return ++salt;
        }

        String SpriteName(uint32_t salt, size_t index) {
            return Format("asset_load_sprite_{}_{}", salt, index);
        }

        String SpritePath(uint32_t salt, size_t index) {
            return Format("assets/asset_load/{}/atlas_{}.png", salt, index);
        }

        Rect SpriteRect(size_t index) {
            const auto value = static_cast<float>(index);
            return Rect{value, value * 2.0f, 16.0f, 32.0f};
        }

        /**
         * Writes the library file of @p count sprites the way `TextureLibrary::Write` does, from plain text. A `SpriteEntry` holds
         * `PoolString`s, so building a library to write would intern the names and the load would find them: the loader has to be
         * the first to see them. `TheHandWrittenFileIsWhatWriteWrites` keeps this in step with `SpriteEntry::Serialize`.
         */
        void WriteSprites(ISerializer& serializer, uint32_t salt, size_t count) {
            size_t elements = count;
            if (!serializer.BeginArray("sprites", "sprite", elements)) {
                return;
            }
            for (size_t i = 0; i < count; ++i) {
                if (!serializer.BeginArrayElement()) {
                    continue;
                }
                serializer.WriteAttribute("name", StringView(SpriteName(salt, i)));
                serializer.WriteAttribute("texturePath", StringView(SpritePath(salt, i)));
                if (serializer.BeginObject("srcRect")) {
                    SpriteRect(i).Serialize(serializer);
                    serializer.EndObject();
                }
                serializer.EndArrayElement();
            }
            serializer.EndArray();
        }

        CORE_ENUM(FileFormat, uint8_t, Xml, Binary)

        const char* FormatName(FileFormat format) {
            return format == FileFormat::Xml ? "xml" : "bin";
        }

        /// One file of fresh names, written outside every measurement
        struct AssetFile {
            FileFormat format = FileFormat::Xml;
            String xml;
            Vector<uint8_t> binary;
        };

        AssetFile MakeFile(FileFormat format, uint32_t salt, size_t count) {
            AssetFile file;
            file.format = format;
            if (format == FileFormat::Xml) {
                XmlSerializer serializer;
                WriteSprites(serializer, salt, count);
                file.xml = serializer.SaveToString();
            } else {
                BinarySerializer serializer;
                WriteSprites(serializer, salt, count);
                file.binary = serializer.GetData();
            }
            return file;
        }

        /// Reads the file into @p library; false when the text or the stream is refused
        bool LoadFile(const AssetFile& file, TextureLibrary& library) {
            if (file.format == FileFormat::Xml) {
                XmlDeserializer deserializer;
                if (!deserializer.LoadFromString(file.xml)) {
                    return false;
                }
                library.Load(deserializer);
                return !deserializer.HasErrors();
            }
            BinaryDeserializer deserializer;
            if (!deserializer.LoadFromBuffer(file.binary)) {
                return false;
            }
            library.Load(deserializer);
            return !deserializer.HasErrors();
        }

        /// The library holds exactly what `WriteSprites` wrote for @p salt, in order
        void ExpectLoaded(const TextureLibrary& library, uint32_t salt, size_t count) {
            ASSERT_EQ(library.GetAll().size(), count);
            for (size_t i = 0; i < count; ++i) {
                const SpriteEntry& entry = library.GetAll()[i];
                const Rect expected = SpriteRect(i);
                ASSERT_EQ(entry.name.ToStringView(), StringView(SpriteName(salt, i))) << i;
                ASSERT_EQ(entry.texturePath.ToStringView(), StringView(SpritePath(salt, i))) << i;
                ASSERT_FLOAT_EQ(entry.srcRect.x, expected.x) << i;
                ASSERT_FLOAT_EQ(entry.srcRect.y, expected.y) << i;
                ASSERT_FLOAT_EQ(entry.srcRect.w, expected.w) << i;
                ASSERT_FLOAT_EQ(entry.srcRect.h, expected.h) << i;
            }
        }

        /// What a loader that knew the totals would hand to `PoolString::Reserve`: the distinct new texts of the file and their
        /// aligned entry bytes
        struct PoolReservation {
            size_t strings = 0;
            size_t bytes = 0;
        };

        PoolReservation ReservationFor(uint32_t salt, size_t count) {
            PoolReservation reservation;
            for (size_t i = 0; i < count; ++i) {
                reservation.strings += 2;
                reservation.bytes += Details::PoolEntryBytes(SpriteName(salt, i).size()) + Details::PoolEntryBytes(SpritePath(salt, i).size());
            }
            return reservation;
        }

        CORE_ENUM(PoolHint, uint8_t, None, Reserved)

        struct LoadMeasure {
            uint64_t allocs = 0;
            uint64_t bytes = 0;
            uint64_t newStrings = 0;
            uint64_t writeLocks = 0;
            double loadMs = 0.0;
            uint64_t pugixmlAllocs = 0;
            uint64_t pugixmlBytes = 0;
            uint64_t globalNewAllocs = 0;
            uint64_t globalNewBytes = 0;
        };

        /// Loads a file of fresh names into an empty library and measures the whole load: the parse of the text or the validation
        /// of the stream, then `Load`
        /// With `PoolHint::Reserved` one `PoolString::Reserve` of the exact totals comes first and is part of the measure: the most a
        /// hint in the file could save for the pool.
        LoadMeasure MeasureLoad(FileFormat format, PoolHint hint) {
            const uint32_t salt = NextSalt();
            const AssetFile file = MakeFile(format, salt, kSpriteCount);
            const PoolReservation reservation = ReservationFor(salt, kSpriteCount);
            TextureLibrary library;

            const PoolStringStats poolBefore = PoolString::GetStats();
            const AllocSourceTotals pugixmlBefore = AllocTracker::GetSourceTotals(AllocSource::Pugixml);
            const AllocSourceTotals globalBefore = AllocTracker::GetSourceTotals(AllocSource::GlobalNew);
            const auto start = std::chrono::steady_clock::now();
            bool loaded = false;
            const AllocCount allocs = CountAllocations([&] {
                if (hint == PoolHint::Reserved) {
                    PoolString::Reserve(reservation.strings, reservation.bytes);
                }
                loaded = LoadFile(file, library);
            });
            const auto stop = std::chrono::steady_clock::now();
            const PoolStringStats poolAfter = PoolString::GetStats();
            const AllocSourceTotals pugixmlAfter = AllocTracker::GetSourceTotals(AllocSource::Pugixml);
            const AllocSourceTotals globalAfter = AllocTracker::GetSourceTotals(AllocSource::GlobalNew);

            LoadMeasure measure;
            if (!loaded || library.GetAll().size() != kSpriteCount) {
                ADD_FAILURE() << "the benchmark file did not load";
                return measure;
            }
            measure.allocs = allocs.count;
            measure.bytes = allocs.bytes;
            measure.newStrings = poolAfter.misses - poolBefore.misses;
            measure.writeLocks = poolAfter.writeLocks - poolBefore.writeLocks;
            measure.loadMs = std::chrono::duration<double, std::milli>(stop - start).count();
            measure.pugixmlAllocs = pugixmlAfter.count - pugixmlBefore.count;
            measure.pugixmlBytes = pugixmlAfter.bytes - pugixmlBefore.bytes;
            measure.globalNewAllocs = globalAfter.count - globalBefore.count;
            measure.globalNewBytes = globalAfter.bytes - globalBefore.bytes;
            return measure;
        }

    }  // namespace

    TEST(AssetLoadAllocTest, TheHandWrittenFileIsWhatWriteWrites) {
        constexpr size_t kCount = 3;
        const uint32_t salt = NextSalt();
        TextureLibrary library;
        for (size_t i = 0; i < kCount; ++i) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(SpriteName(salt, i));
            entry.texturePath = PoolString::Intern(SpritePath(salt, i));
            entry.srcRect = SpriteRect(i);
            library.AddOrUpdate(eastl::move(entry));
        }

        XmlSerializer written;
        library.Write(written);
        XmlSerializer byHand;
        WriteSprites(byHand, salt, kCount);
        EXPECT_EQ(written.SaveToString(), byHand.SaveToString());

        BinarySerializer writtenBinary;
        library.Write(writtenBinary);
        BinarySerializer byHandBinary;
        WriteSprites(byHandBinary, salt, kCount);
        EXPECT_EQ(writtenBinary.GetData(), byHandBinary.GetData());
    }

    TEST(AssetLoadAllocTest, BothFormatsLoadTheSameLibrary) {
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            const uint32_t salt = NextSalt();
            const AssetFile file = MakeFile(format, salt, kSpriteCount);
            TextureLibrary library;
            ASSERT_TRUE(LoadFile(file, library)) << FormatName(format);
            ExpectLoaded(library, salt, kSpriteCount);
        }
    }

    /// The entries and the index are one block each, reserved for the reader's count, so the load allocates a few blocks
    /// whatever the number of sprites. The slack is those blocks, the reader's own lists and a pool block.
    TEST(AssetLoadAllocTest, LoadOfAParsedFileAllocatesAFewBlocks) {
        constexpr uint64_t kLoadSlack = 16;
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            const AssetFile file = MakeFile(format, NextSalt(), kSpriteCount);
            TextureLibrary library;
            AllocCount allocs;
            if (format == FileFormat::Xml) {
                XmlDeserializer deserializer;
                ASSERT_TRUE(deserializer.LoadFromString(file.xml));
                allocs = CountAllocations([&] { library.Load(deserializer); });
            } else {
                BinaryDeserializer deserializer;
                ASSERT_TRUE(deserializer.LoadFromBuffer(file.binary));
                allocs = CountAllocations([&] { library.Load(deserializer); });
            }
            ASSERT_EQ(library.GetAll().size(), kSpriteCount) << FormatName(format);
            if constexpr (AllocTracker::kEnabled) {
                EXPECT_LE(allocs.count, kLoadSlack) << FormatName(format);
            } else {
                GTEST_SKIP() << kNoAllocTracker;
            }
        }
    }

    /// A benchmark, not a check: the set of .claude/rules/perf-project.md runs it (the counts are zero without the profile
    /// preset, `kNoAllocTracker`)
    TEST(AssetLoadAllocBenchTest, DISABLED_LoadOfFreshNames) {
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            for (const PoolHint hint : {PoolHint::None, PoolHint::Reserved}) {
                Vector<LoadMeasure> runs;
                for (int i = 0; i < kRepetitions; ++i) {
                    runs.push_back(MeasureLoad(format, hint));
                }
                std::sort(runs.begin(), runs.end(), [](const LoadMeasure& a, const LoadMeasure& b) { return a.loadMs < b.loadMs; });
                const LoadMeasure& median = runs[runs.size() / 2];
                // The first line keeps its name: the metrics of the perf set read it
                std::cout << (hint == PoolHint::None ? "AssetLoad" : "AssetLoadReserved") << " format=" << FormatName(format) << " sprites=" << kSpriteCount << " allocs=" << median.allocs
                          << " bytes=" << median.bytes << " newStrings=" << median.newStrings << " writeLocks=" << median.writeLocks << " loadMs=" << median.loadMs << "\n";
                if (format == FileFormat::Xml && hint == PoolHint::None) {
                    std::cout << "AssetLoadSources format=xml pugixmlAllocs=" << median.pugixmlAllocs << " pugixmlBytes=" << median.pugixmlBytes << " globalNewAllocs=" << median.globalNewAllocs
                              << " globalNewBytes=" << median.globalNewBytes << "\n";
                }
            }
        }
    }

}  // namespace BECore::Tests
