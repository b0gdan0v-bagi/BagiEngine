#include <BECore/Config/ContentVersion.h>
#include <BECore/PoolString/PoolStringEntry.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <Common/AllocCheck.h>
#include <Common/BinaryStreams.h>
#include <Common/EngineEnvironment.h>
#include <Common/SceneTestSupport.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
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

        void RetagAsPoolStrings(Vector<BinaryStreams::Entry>& entries, const Vector<StringView>& names) {
            for (BinaryStreams::Entry& entry : entries) {
                if (entry.tag == BinaryFormat::Tag::String && eastl::find(names.begin(), names.end(), StringView(entry.name)) != names.end()) {
                    entry.tag = BinaryFormat::Tag::PoolString;
                }
                RetagAsPoolStrings(entry.children, names);
            }
        }

        /**
         * The same document with the string entries called @p names as `PoolString` entries (tag 15, text in the table). The fields
         * are `PoolString`s, and `Write` of one writes the table form; the hand-written files cannot build a `PoolString` without
         * interning it, so they write the text inline and this makes them what `Write` writes.
         */
        Vector<uint8_t> WithPoolStrings(const Vector<uint8_t>& stream, std::initializer_list<StringView> names) {
            Vector<BinaryStreams::Entry> tree = BinaryStreams::ReadTree(stream);
            RetagAsPoolStrings(tree, Vector<StringView>(names));
            return BinaryStreams::Build(tree);
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
                file.binary = WithPoolStrings(serializer.GetData(), {"name", "texturePath"});
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

        /// The wide shape: an array of objects with many scalar fields each and a nested object, where a lookup that scans the whole
        /// scope per field costs the square of the field count. The loader reads the fields in the order the writer wrote them, as the
        /// generated `Deserialize` does.
        constexpr size_t kWideObjects = 500;
        constexpr size_t kWideInts = 8;
        constexpr size_t kWideFloats = 8;
        constexpr size_t kWideBools = 6;
        constexpr size_t kWideFields = kWideInts + kWideFloats + kWideBools + 2;
        constexpr size_t kWideChildFloats = 4;

        struct WideNames {
            Vector<String> ints;
            Vector<String> floats;
            Vector<String> bools;
            Vector<String> child;
        };

        /// Built once, outside every measure: a name made inside the loader would add allocations the formats do not own
        const WideNames& WideFieldNames() {
            static const WideNames names = [] {
                WideNames result;
                for (size_t k = 0; k < kWideInts; ++k) {
                    result.ints.push_back(Format("int{}", k));
                }
                for (size_t k = 0; k < kWideFloats; ++k) {
                    result.floats.push_back(Format("float{}", k));
                }
                for (size_t k = 0; k < kWideBools; ++k) {
                    result.bools.push_back(Format("flag{}", k));
                }
                for (size_t k = 0; k < kWideChildFloats; ++k) {
                    result.child.push_back(Format("c{}", k));
                }
                return result;
            }();
            return names;
        }

        struct WideItem {
            int32_t ints[kWideInts] = {};
            float floats[kWideFloats] = {};
            bool bools[kWideBools] = {};
            PoolString first;
            PoolString second;
            float child[kWideChildFloats] = {};
        };

        int32_t WideInt(size_t index, size_t field) {
            return static_cast<int32_t>(index * 31 + field * 7) - 100;
        }

        float WideFloat(size_t index, size_t field) {
            return static_cast<float>(index) + static_cast<float>(field) * 0.25f;
        }

        bool WideBool(size_t index, size_t field) {
            return ((index + field) & 1) != 0;
        }

        float WideChild(size_t index, size_t field) {
            return static_cast<float>(index * 4 + field);
        }

        String WideFirst(uint32_t salt, size_t index) {
            return Format("asset_wide_first_{}_{}", salt, index);
        }

        String WideSecond(uint32_t salt, size_t index) {
            return Format("asset_wide_second_{}_{}", salt, index);
        }

        void WriteWide(ISerializer& serializer, uint32_t salt, size_t count) {
            const WideNames& names = WideFieldNames();
            size_t elements = count;
            if (!serializer.BeginArray("items", "item", elements)) {
                return;
            }
            for (size_t i = 0; i < count; ++i) {
                if (!serializer.BeginArrayElement()) {
                    continue;
                }
                for (size_t k = 0; k < kWideInts; ++k) {
                    serializer.WriteAttribute(StringView(names.ints[k]), WideInt(i, k));
                }
                for (size_t k = 0; k < kWideFloats; ++k) {
                    serializer.WriteAttribute(StringView(names.floats[k]), WideFloat(i, k));
                }
                for (size_t k = 0; k < kWideBools; ++k) {
                    serializer.WriteAttribute(StringView(names.bools[k]), WideBool(i, k));
                }
                serializer.WriteAttribute("first", StringView(WideFirst(salt, i)));
                serializer.WriteAttribute("second", StringView(WideSecond(salt, i)));
                if (serializer.BeginObject("child")) {
                    for (size_t k = 0; k < kWideChildFloats; ++k) {
                        serializer.WriteAttribute(StringView(names.child[k]), WideChild(i, k));
                    }
                    serializer.EndObject();
                }
                serializer.EndArrayElement();
            }
            serializer.EndArray();
        }

        void ReadWide(IDeserializer& deserializer, Vector<WideItem>& items) {
            const WideNames& names = WideFieldNames();
            size_t count = 0;
            if (!deserializer.BeginArray("items", "item", count)) {
                return;
            }
            items.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (!deserializer.BeginArrayElement()) {
                    break;
                }
                WideItem item;
                for (size_t k = 0; k < kWideInts; ++k) {
                    deserializer.ReadAttribute(StringView(names.ints[k]), item.ints[k]);
                }
                for (size_t k = 0; k < kWideFloats; ++k) {
                    deserializer.ReadAttribute(StringView(names.floats[k]), item.floats[k]);
                }
                for (size_t k = 0; k < kWideBools; ++k) {
                    deserializer.ReadAttribute(StringView(names.bools[k]), item.bools[k]);
                }
                deserializer.ReadAttribute("first", item.first);
                deserializer.ReadAttribute("second", item.second);
                if (deserializer.BeginObject("child")) {
                    for (size_t k = 0; k < kWideChildFloats; ++k) {
                        deserializer.ReadAttribute(StringView(names.child[k]), item.child[k]);
                    }
                    deserializer.EndObject();
                }
                deserializer.EndArrayElement();
                items.push_back(item);
            }
            deserializer.EndArray();
        }

        AssetFile MakeWideFile(FileFormat format, uint32_t salt, size_t count) {
            AssetFile file;
            file.format = format;
            if (format == FileFormat::Xml) {
                XmlSerializer serializer;
                WriteWide(serializer, salt, count);
                file.xml = serializer.SaveToString();
            } else {
                BinarySerializer serializer;
                WriteWide(serializer, salt, count);
                file.binary = WithPoolStrings(serializer.GetData(), {"first", "second"});
            }
            return file;
        }

        /// Reads the wide file into @p items; false when the text or the stream is refused or a field did not read
        bool LoadWideFile(const AssetFile& file, Vector<WideItem>& items) {
            if (file.format == FileFormat::Xml) {
                XmlDeserializer deserializer;
                if (!deserializer.LoadFromString(file.xml)) {
                    return false;
                }
                ReadWide(deserializer, items);
                return !deserializer.HasErrors();
            }
            BinaryDeserializer deserializer;
            if (!deserializer.LoadFromBuffer(file.binary)) {
                return false;
            }
            ReadWide(deserializer, items);
            return !deserializer.HasErrors();
        }

        void ExpectWideLoaded(const Vector<WideItem>& items, uint32_t salt, size_t count) {
            ASSERT_EQ(items.size(), count);
            for (size_t i = 0; i < count; ++i) {
                const WideItem& item = items[i];
                for (size_t k = 0; k < kWideInts; ++k) {
                    ASSERT_EQ(item.ints[k], WideInt(i, k)) << i << " int " << k;
                }
                for (size_t k = 0; k < kWideFloats; ++k) {
                    ASSERT_FLOAT_EQ(item.floats[k], WideFloat(i, k)) << i << " float " << k;
                }
                for (size_t k = 0; k < kWideBools; ++k) {
                    ASSERT_EQ(item.bools[k], WideBool(i, k)) << i << " flag " << k;
                }
                ASSERT_EQ(item.first.ToStringView(), StringView(WideFirst(salt, i))) << i;
                ASSERT_EQ(item.second.ToStringView(), StringView(WideSecond(salt, i))) << i;
                for (size_t k = 0; k < kWideChildFloats; ++k) {
                    ASSERT_FLOAT_EQ(item.child[k], WideChild(i, k)) << i << " child " << k;
                }
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

        /// Runs @p load and measures it: allocations, new strings, time and the allocations of the parsers. False in @p loaded when
        /// the load failed (the figures are then zero)
        LoadMeasure MeasureCall(FunctionRef<bool()> load, bool& loaded) {
            const PoolStringStats poolBefore = PoolString::GetStats();
            const AllocSourceTotals pugixmlBefore = AllocTracker::GetSourceTotals(AllocSource::Pugixml);
            const AllocSourceTotals globalBefore = AllocTracker::GetSourceTotals(AllocSource::GlobalNew);
            const auto start = std::chrono::steady_clock::now();
            const AllocCount allocs = CountAllocations([&] { loaded = load(); });
            const auto stop = std::chrono::steady_clock::now();
            const PoolStringStats poolAfter = PoolString::GetStats();
            const AllocSourceTotals pugixmlAfter = AllocTracker::GetSourceTotals(AllocSource::Pugixml);
            const AllocSourceTotals globalAfter = AllocTracker::GetSourceTotals(AllocSource::GlobalNew);

            LoadMeasure measure;
            if (!loaded) {
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

        /// Loads a file of fresh names into an empty library and measures the whole load: the parse of the text or the validation
        /// of the stream, then `Load`
        /// With `PoolHint::Reserved` one `PoolString::Reserve` of the exact totals comes first and is part of the measure: the most one
        /// `PoolString::Reserve` could save for the pool.
        LoadMeasure MeasureLoad(FileFormat format, PoolHint hint) {
            const uint32_t salt = NextSalt();
            const AssetFile file = MakeFile(format, salt, kSpriteCount);
            const PoolReservation reservation = ReservationFor(salt, kSpriteCount);
            TextureLibrary library;

            bool loaded = false;
            const LoadMeasure measure = MeasureCall(
                [&] {
                    if (hint == PoolHint::Reserved) {
                        PoolString::Reserve(reservation.strings, reservation.bytes);
                    }
                    return LoadFile(file, library);
                },
                loaded);
            if (!loaded || library.GetAll().size() != kSpriteCount) {
                ADD_FAILURE() << "the benchmark file did not load";
                return LoadMeasure{};
            }
            return measure;
        }

        /// The wide file of fresh names loaded into a list of items, in @p format
        LoadMeasure MeasureWideLoad(FileFormat format) {
            const uint32_t salt = NextSalt();
            const AssetFile file = MakeWideFile(format, salt, kWideObjects);
            Vector<WideItem> items;
            bool loaded = false;
            const LoadMeasure measure = MeasureCall([&] { return LoadWideFile(file, items); }, loaded);
            if (!loaded || items.size() != kWideObjects) {
                ADD_FAILURE() << "the wide benchmark file did not load";
                return LoadMeasure{};
            }
            return measure;
        }

        CORE_ENUM(BinSource, uint8_t, Buffer, View, File)

        const char* SourceName(BinSource source) {
            switch (source) {
                case BinSource::Buffer:
                    return "buffer";
                case BinSource::View:
                    return "view";
                default:
                    return "file";
            }
        }

        /// The sprite stream in binary, copied from the memory of the caller, viewed in it, or read from a file, the file read being part of the measure.
        /// The file is written before the measure and removed after it.
        LoadMeasure MeasureBinSource(BinSource source) {
            const uint32_t salt = NextSalt();
            const AssetFile file = MakeFile(FileFormat::Binary, salt, kSpriteCount);
            const std::filesystem::path path = TestTempPath(Format("asset_load_bin_{}", salt));
            if (source == BinSource::File) {
                WriteFile(path, StringView(reinterpret_cast<const char*>(file.binary.data()), file.binary.size()));
            }
            TextureLibrary library;
            bool loaded = false;
            const LoadMeasure measure = MeasureCall(
                [&] {
                    BinaryDeserializer deserializer;
                    bool opened = false;
                    switch (source) {
                        case BinSource::Buffer:
                            opened = deserializer.LoadFromBuffer(file.binary);
                            break;
                        case BinSource::View:
                            opened = deserializer.LoadFromView(file.binary.data(), file.binary.size());
                            break;
                        default:
                            opened = deserializer.LoadFromFile(path);
                            break;
                    }
                    if (!opened) {
                        return false;
                    }
                    library.Load(deserializer);
                    return !deserializer.HasErrors();
                },
                loaded);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            if (!loaded || library.GetAll().size() != kSpriteCount) {
                ADD_FAILURE() << "the benchmark file did not load";
                return LoadMeasure{};
            }
            return measure;
        }

        constexpr size_t kSceneGroups = 600;
        constexpr StringView kSceneHudRef = "nodes/AssetLoadHud";

        /// The node file the scene references: two components and a child, overridden by name at each use
        String SceneHudFile() {
            return Format(R"(<node contentVersion="{}" name="Hud" layer="10"><components><component type="TransformComponent" x="1" y="2" width="3" height="4"/>)"
                          R"(<component type="QuadRendererComponent"><color r="9" g="8" b="7" a="255"/></component></components><children><node name="Child" layer="1"/></children></node>)",
                          kContentVersion);
        }

        /// `kSceneGroups` groups, each an inline node with a component and two children plus a reference to the node file: a few
        /// thousand nodes with components and the references to expand
        String SceneText() {
            String nodes;
            for (size_t i = 0; i < kSceneGroups; ++i) {
                nodes.append(Format(R"(<node name="Group{0}" layer="3"><components><component type="TransformComponent" x="{0}" y="6"/></components>)"
                                    R"(<children><node name="Left{0}"><components><component type="TransformComponent" x="1" y="2"/></components></node><node name="Right{0}"/></children></node>)"
                                    R"(<node ref="{1}" name="Hud{0}"/>)",
                                    i, kSceneHudRef));
            }
            return SceneConfigText(nodes, kMainSceneView800x600);
        }

        String PrintedScenes(SceneManager& manager) {
            XmlSerializer serializer;
            manager.Serialize(serializer);
            return serializer.SaveToString();
        }

        /// The scene's XML text, its node file in a temp folder and the bytes the cook writes for it
        class SceneFixture {
        public:
            SceneFixture() : _dir(TestTempDirectory("BagiAssetLoadScene")), _text(SceneText()) {
                std::filesystem::remove_all(_dir);
                WriteFile(_dir / "nodes" / "AssetLoadHud.xml", SceneHudFile());
                const auto manager = LoadFromXml();
                BinarySerializer writer;
                if (manager != nullptr && WriteContentStamp(writer, kSceneManagerRootType)) {
                    manager->Serialize(writer);
                }
                _binary = writer.GetData();
            }

            ~SceneFixture() {
                std::error_code ignored;
                std::filesystem::remove_all(_dir, ignored);
            }

            SceneFixture(const SceneFixture&) = delete;
            SceneFixture& operator=(const SceneFixture&) = delete;

            /// Parse, expansion of the references and deserialize
            IntrusivePtrAtomic<SceneManager> LoadFromXml() const {
                pugi::xml_document document;
                if (!document.load_buffer(_text.data(), _text.size())) {
                    return {};
                }
                return LoadSceneManager(XmlNode{document.document_element()}, MakeDirectoryNodeLoader(_dir));
            }

            IntrusivePtrAtomic<SceneManager> LoadFromBinary() const {
                BinaryDeserializer reader;
                if (!reader.LoadFromView(_binary.data(), _binary.size())) {
                    return {};
                }
                return LoadSceneManagerFromBinary(reader);
            }

        private:
            std::filesystem::path _dir;
            String _text;
            Vector<uint8_t> _binary;
        };

        LoadMeasure MeasureSceneLoad(const SceneFixture& fixture, FileFormat format) {
            IntrusivePtrAtomic<SceneManager> manager;
            bool loaded = false;
            const LoadMeasure measure = MeasureCall(
                [&] {
                    manager = format == FileFormat::Xml ? fixture.LoadFromXml() : fixture.LoadFromBinary();
                    return manager != nullptr;
                },
                loaded);
            if (!loaded) {
                ADD_FAILURE() << "the scene did not load";
                return LoadMeasure{};
            }
            return measure;
        }

        /// The median by `loadMs` of `kRepetitions` runs of @p measure
        template <typename TMeasure>
        LoadMeasure MedianOf(TMeasure&& measure) {
            Vector<LoadMeasure> runs;
            for (int i = 0; i < kRepetitions; ++i) {
                runs.push_back(measure());
            }
            std::sort(runs.begin(), runs.end(), [](const LoadMeasure& a, const LoadMeasure& b) { return a.loadMs < b.loadMs; });
            return runs[runs.size() / 2];
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
        EXPECT_EQ(writtenBinary.GetData(), WithPoolStrings(byHandBinary.GetData(), {"name", "texturePath"}));
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
    /// whatever the number of sprites. The slack is those blocks, the reader's own lists and a pool block. The binary file's
    /// `PoolString`s are interned by one `InternBatch`, whose lists of new texts grow by doubling: a number of blocks that
    /// grows with the logarithm of the count, which is why its slack is larger.
    TEST(AssetLoadAllocTest, LoadOfAParsedFileAllocatesAFewBlocks) {
        constexpr uint64_t kXmlLoadSlack = 16;
        constexpr uint64_t kBinaryLoadSlack = 96;
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
                EXPECT_LE(allocs.count, format == FileFormat::Xml ? kXmlLoadSlack : kBinaryLoadSlack) << FormatName(format);
            } else {
                GTEST_SKIP() << kNoAllocTracker;
            }
        }
    }

    TEST(AssetLoadAllocTest, TheWideFileLoadsTheSameFromBothFormats) {
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            const uint32_t salt = NextSalt();
            const AssetFile file = MakeWideFile(format, salt, kWideObjects);
            Vector<WideItem> items;
            ASSERT_TRUE(LoadWideFile(file, items)) << FormatName(format);
            ExpectWideLoaded(items, salt, kWideObjects);
        }
    }

    TEST(AssetLoadAllocTest, TheBinaryFileSourceReadsTheSameLibraryAsTheBufferSource) {
        const uint32_t salt = NextSalt();
        const AssetFile file = MakeFile(FileFormat::Binary, salt, kSpriteCount);
        const std::filesystem::path path = TestTempPath(Format("asset_load_source_{}", salt));
        WriteFile(path, StringView(reinterpret_cast<const char*>(file.binary.data()), file.binary.size()));

        TextureLibrary fromFile;
        BinaryDeserializer deserializer;
        const bool opened = deserializer.LoadFromFile(path);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        ASSERT_TRUE(opened);
        fromFile.Load(deserializer);
        ASSERT_FALSE(deserializer.HasErrors());
        ExpectLoaded(fromFile, salt, kSpriteCount);

        TextureLibrary fromBuffer;
        ASSERT_TRUE(LoadFile(file, fromBuffer));
        ExpectLoaded(fromBuffer, salt, kSpriteCount);
    }

    TEST(AssetLoadAllocTest, TheSceneLoadsTheSameFromXmlAndFromItsCookedBytes) {
        EngineEnvironment::EnsureCoreServices();
        const SceneFixture fixture;
        const auto fromXml = fixture.LoadFromXml();
        const auto fromBinary = fixture.LoadFromBinary();
        ASSERT_NE(fromXml, nullptr);
        ASSERT_NE(fromBinary, nullptr);
        const String expected = PrintedScenes(*fromXml);
        EXPECT_NE(expected.find("Hud599"), String::npos);
        EXPECT_EQ(PrintedScenes(*fromBinary), expected);
    }

    /// A benchmark, not a check: the set of .claude/rules/perf-project.md runs it (the counts are zero without the profile
    /// preset, `kNoAllocTracker`)
    TEST(AssetLoadAllocBenchTest, DISABLED_LoadOfFreshNames) {
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            for (const PoolHint hint : {PoolHint::None, PoolHint::Reserved}) {
                const LoadMeasure median = MedianOf([&] { return MeasureLoad(format, hint); });
                // The first line keeps its name: the metrics of the perf set read it
                std::cout << (hint == PoolHint::None ? "AssetLoad" : "AssetLoadReserved") << " format=" << FormatName(format) << " sprites=" << kSpriteCount << " allocs=" << median.allocs
                          << " bytes=" << median.bytes << " newStrings=" << median.newStrings << " writeLocks=" << median.writeLocks << " loadMs=" << median.loadMs << "\n";
                if (format == FileFormat::Xml && hint == PoolHint::None) {
                    std::cout << "AssetLoadSources format=xml pugixmlAllocs=" << median.pugixmlAllocs << " pugixmlBytes=" << median.pugixmlBytes << " globalNewAllocs=" << median.globalNewAllocs
                              << " globalNewBytes=" << median.globalNewBytes << "\n";
                }
            }
        }
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            const LoadMeasure median = MedianOf([&] { return MeasureWideLoad(format); });
            std::cout << "AssetLoadWide format=" << FormatName(format) << " objects=" << kWideObjects << " fields=" << kWideFields << " allocs=" << median.allocs << " bytes=" << median.bytes
                      << " loadMs=" << median.loadMs << "\n";
        }
        for (const BinSource source : {BinSource::Buffer, BinSource::View, BinSource::File}) {
            const LoadMeasure median = MedianOf([&] { return MeasureBinSource(source); });
            std::cout << "AssetLoadBinSource source=" << SourceName(source) << " allocs=" << median.allocs << " bytes=" << median.bytes << " loadMs=" << median.loadMs << "\n";
        }
        {
            EngineEnvironment::EnsureCoreServices();
            const SceneFixture fixture;
            for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
                const LoadMeasure median = MedianOf([&] { return MeasureSceneLoad(fixture, format); });
                std::cout << "AssetLoadScene format=" << FormatName(format) << " groups=" << kSceneGroups << " allocs=" << median.allocs << " bytes=" << median.bytes << " loadMs=" << median.loadMs
                          << "\n";
            }
        }
    }

}  // namespace BECore::Tests
