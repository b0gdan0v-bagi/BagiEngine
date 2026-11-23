#include <BECore/Config/ContentVersion.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/PoolString/PoolStringEntry.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Profile.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Time/Time.h>
#include <Common/AllocCheck.h>
#include <Common/BinaryStreams.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/SceneTestSupport.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/ZoneAllocReport.h>
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

        /// `LoadFile` with a zone around each phase, for the zone pass. Not the measured path: the medians carry no zone cost, so the two
        /// repeat the few lines of the phase split instead of sharing them.
        bool LoadFileByZone(const AssetFile& file, TextureLibrary& library) {
            if (file.format == FileFormat::Xml) {
                XmlDeserializer deserializer;
                {
                    BE_PROFILE_SCOPE("Text parse");
                    if (!deserializer.LoadFromString(file.xml)) {
                        return false;
                    }
                }
                {
                    BE_PROFILE_SCOPE("Library load");
                    library.Load(deserializer);
                }
                return !deserializer.HasErrors();
            }
            BinaryDeserializer deserializer;
            {
                BE_PROFILE_SCOPE("Stream open");
                if (!deserializer.LoadFromBuffer(file.binary)) {
                    return false;
                }
            }
            {
                BE_PROFILE_SCOPE("Library load");
                library.Load(deserializer);
            }
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

        /// `LoadWideFile` with a zone around each phase, as `LoadFileByZone`
        bool LoadWideFileByZone(const AssetFile& file, Vector<WideItem>& items) {
            if (file.format == FileFormat::Xml) {
                XmlDeserializer deserializer;
                {
                    BE_PROFILE_SCOPE("Text parse");
                    if (!deserializer.LoadFromString(file.xml)) {
                        return false;
                    }
                }
                {
                    BE_PROFILE_SCOPE("Wide load");
                    ReadWide(deserializer, items);
                }
                return !deserializer.HasErrors();
            }
            BinaryDeserializer deserializer;
            {
                BE_PROFILE_SCOPE("Stream open");
                if (!deserializer.LoadFromBuffer(file.binary)) {
                    return false;
                }
            }
            {
                BE_PROFILE_SCOPE("Wide load");
                ReadWide(deserializer, items);
            }
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

        /// The scene's XML text, its node file in a temp folder and the bytes the cook writes for it. The default one is the scene of
        /// groups; another scene brings its text and its own folder prefix (two fixtures of one case would share a folder otherwise).
        class SceneFixture {
        public:
            SceneFixture() : SceneFixture("BagiAssetLoadScene", SceneText(), SceneHudFile()) {}

            /// @p hudFile is the node file `kSceneHudRef` names; empty for a scene with no reference
            SceneFixture(const String& dirPrefix, String text, StringView hudFile) : _dir(TestTempDirectory(dirPrefix)), _text(eastl::move(text)) {
                std::filesystem::remove_all(_dir);
                if (!hudFile.empty()) {
                    WriteFile(_dir / "nodes" / "AssetLoadHud.xml", hudFile);
                }
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
            IntrusivePtr<SceneManager> LoadFromXml() const {
                pugi::xml_document document;
                if (!document.load_buffer(_text.data(), _text.size())) {
                    return {};
                }
                return LoadSceneManager(XmlNode{document.document_element()}, MakeDirectoryNodeLoader(_dir));
            }

            IntrusivePtr<SceneManager> LoadFromBinary() const {
                BinaryDeserializer reader;
                if (!reader.LoadFromView(_binary.data(), _binary.size())) {
                    return {};
                }
                return LoadSceneManagerFromBinary(reader);
            }

            /**
             * `LoadFromXml` with a zone per phase, for the zone pass. The engine has no zone in `LoadSceneManager`, so this repeats its
             * three steps (copy and expand the references, deserialize; keep it in step with `SceneLoader.cpp`) and the zones stay in
             * the test. @p diagnostics is the mode of the deserialize: `Warn` is what a config load runs with, `Off` shows what the
             * unknown-key bookkeeping costs.
             */
            IntrusivePtr<SceneManager> LoadFromXmlByZone(DiagnosticsMode diagnostics = DiagnosticsMode::Warn) const {
                pugi::xml_document document;
                {
                    BE_PROFILE_SCOPE("Scene parse");
                    if (!document.load_buffer(_text.data(), _text.size())) {
                        return {};
                    }
                }
                pugi::xml_document expanded;
                {
                    BE_PROFILE_SCOPE("Scene expand");
                    expanded.append_copy(document.document_element());
                    ExpandNodeReferences(expanded.document_element(), MakeDirectoryNodeLoader(_dir), true);
                }
                auto manager = New<SceneManager>();
                BE_PROFILE_SCOPE("Scene deserialize");
                XmlDeserializer deserializer;
                deserializer.SetDiagnostics(diagnostics);
                if (deserializer.LoadFromXmlNode(XmlNode{expanded.document_element()})) {
                    deserializer.NoteKnownKey(kContentVersionAttribute);
                    manager->Deserialize(deserializer);
                    deserializer.FinishLoad();
                }
                return manager;
            }

            /// `LoadFromBinary` with a zone around the stream check and around the build, for the zone pass
            IntrusivePtr<SceneManager> LoadFromBinaryByZone() const {
                BinaryDeserializer reader;
                {
                    BE_PROFILE_SCOPE("Scene stream open");
                    if (!reader.LoadFromView(_binary.data(), _binary.size())) {
                        return {};
                    }
                }
                BE_PROFILE_SCOPE("Scene build cooked");
                return LoadSceneManagerFromBinary(reader);
            }

        private:
            std::filesystem::path _dir;
            String _text;
            Vector<uint8_t> _binary;
        };

        LoadMeasure MeasureSceneLoad(const SceneFixture& fixture, FileFormat format) {
            IntrusivePtr<SceneManager> manager;
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

        constexpr size_t kZoneReportNames = 4;
        constexpr size_t kParticleEmitters = 100;
        constexpr size_t kLoopingRate = 120;
        constexpr size_t kLoopingMaxParticles = 256;
        constexpr size_t kBurstCount = 64;
        constexpr size_t kBurstMaxParticles = 128;
        constexpr double kParticleFrame = 1.0 / 32.0;  // a power of two, so the sums of frames are exact
        /// More than the longest life (a second), so the looping emitters hold their steady number of particles and the bursts are over
        constexpr int kWarmupFrames = 64;
        constexpr int kRunFrames = 64;
        constexpr const char* kParticleUpdateZone = "Particles update";
        constexpr const char* kParticleDrawZone = "Particles draw";

        /// `kParticleEmitters` nodes of a transform and an emitter without a sprite (no texture library entry is needed): the even ones
        /// loop, the odd ones fire a burst at the first update and are over; the seeds differ by node
        String ParticleSceneText() {
            String nodes;
            for (size_t i = 0; i < kParticleEmitters; ++i) {
                const String effect = i % 2 == 0
                                          ? Format(R"(<effect looping="true" rate="{}" maxParticles="{}" lifetimeMin="1" lifetimeMax="1" seed="{}"/>)", kLoopingRate, kLoopingMaxParticles, i + 1)
                                          : Format(R"(<effect burstCount="{}" maxParticles="{}" lifetimeMin="0.5" lifetimeMax="0.5" seed="{}"/>)", kBurstCount, kBurstMaxParticles, i + 1);
                nodes.append(Format(R"(<node name="Emitter{0}"><components><component type="TransformComponent" x="{0}" y="6" width="4" height="4"/>)"
                                    R"(<component type="ParticleEmitterComponent">{1}</component></components></node>)",
                                    i, effect));
            }
            return SceneConfigText(nodes, kMainSceneView800x600);
        }

        SceneFixture MakeParticleFixture() {
            return SceneFixture("BagiAssetLoadParticles", ParticleSceneText(), {});
        }

        /// Gives the renderer back on every exit, a failed check included
        class RendererGuard {
        public:
            explicit RendererGuard(IntrusivePtr<IRenderer> renderer) {
                CoreManager::GetRendererManager().SetRenderer(eastl::move(renderer));
            }
            ~RendererGuard() {
                CoreManager::GetRendererManager().SetRenderer({});
                CoreManager::GetTime().Reset();
            }
            RendererGuard(const RendererGuard&) = delete;
            RendererGuard& operator=(const RendererGuard&) = delete;
        };

        struct ParticleRun {
            bool ran = false;
            size_t emitters = 0;
            size_t live = 0;
            size_t geometryCalls = 0;  ///< of the last frame
            uint64_t updateCalls = 0;  ///< of the zones; zero without the tracker
            uint64_t drawCalls = 0;
            double updateAllocsPerFrame = 0.0;
            double drawAllocsPerFrame = 0.0;
            double updateBytesPerFrame = 0.0;
            double drawBytesPerFrame = 0.0;
        };

        size_t CountLive(SceneManager& manager, size_t& emitters) {
            size_t live = 0;
            emitters = 0;
            Scene* scene = manager.GetSceneByName("Main"_intern);
            if (scene == nullptr) {
                return 0;
            }
            for (const auto& node : scene->GetRootNode().GetChildren()) {
                if (const auto emitter = node->GetComponent<ParticleEmitterComponent>()) {
                    ++emitters;
                    live += emitter->GetLiveCount();
                }
            }
            return live;
        }

        /**
         * Loads the scene from XML, runs `kWarmupFrames` frames so the pools are full, then `kRunFrames` frames with the update and the
         * draw each in a zone and the recorder's `ClearCalls` outside both. The figures are per frame, zero without the profile preset.
         * With @p print the tracker's report of the frames follows the `AssetLoadParticlesRun` line.
         *
         * `RecordingRenderer` copies the vertices of every draw call: when it keeps no spare storage between frames the draw figure
         * includes the test double, so the update figure is the one that counts.
         */
        ParticleRun RunParticleFrames(const SceneFixture& fixture, bool print) {
            ParticleRun run;
            const auto manager = fixture.LoadFromXml();
            if (manager == nullptr) {
                return run;
            }
            auto renderer = New<RecordingRenderer>();
            const RendererGuard guard(renderer);
            manager->Initialize();

            for (int frame = 0; frame < kWarmupFrames; ++frame) {
                CoreManager::GetTime().Advance(kParticleFrame);
                manager->UpdateAll();
                renderer->ClearCalls();
                manager->DrawAll();
            }

            AllocTracker::ResetZoneStats();
            for (int frame = 0; frame < kRunFrames; ++frame) {
                CoreManager::GetTime().Advance(kParticleFrame);
                {
                    BE_PROFILE_SCOPE(kParticleUpdateZone);
                    manager->UpdateAll();
                }
                renderer->ClearCalls();
                {
                    BE_PROFILE_SCOPE(kParticleDrawZone);
                    manager->DrawAll();
                }
            }

            run.ran = true;
            run.live = CountLive(*manager, run.emitters);
            for (const DrawCall& call : renderer->calls) {
                run.geometryCalls += call.kind == DrawKind::Geometry ? 1 : 0;
            }
            const Vector<ZoneReportRow> rows = CollectZoneReport();
            const ZoneReportRow* updateRow = FindZoneRow(rows, kParticleUpdateZone);
            const ZoneReportRow* drawRow = FindZoneRow(rows, kParticleDrawZone);
            const ZoneReportRow none;  // no tracker, no rows
            const ZoneReportRow& update = updateRow != nullptr ? *updateRow : none;
            const ZoneReportRow& draw = drawRow != nullptr ? *drawRow : none;
            run.updateCalls = update.calls;
            run.drawCalls = draw.calls;
            constexpr double kFrames = kRunFrames;
            run.updateAllocsPerFrame = static_cast<double>(update.allocs) / kFrames;
            run.drawAllocsPerFrame = static_cast<double>(draw.allocs) / kFrames;
            run.updateBytesPerFrame = static_cast<double>(update.bytes) / kFrames;
            run.drawBytesPerFrame = static_cast<double>(draw.bytes) / kFrames;
            if (print) {
                std::cout << Format("AssetLoadParticlesRun emitters={} live={} frames={} updateAllocsPerFrame={:.2f} drawAllocsPerFrame={:.2f} updateBytesPerFrame={:.1f} drawBytesPerFrame={:.1f}\n",
                                    run.emitters, run.live, kRunFrames, run.updateAllocsPerFrame, run.drawAllocsPerFrame, run.updateBytesPerFrame, run.drawBytesPerFrame)
                                 .c_str();
                PrintZoneReport(std::cout, "particles-run", kZoneReportNames);
            }
            return run;
        }

        /**
         * One zone pass: @p load is a load of a file made before the call, its phases in zones. The zone statistics are reset right
         * before it and read right after, so they hold this load and nothing else. False when the load failed (nothing is printed).
         */
        bool PrintZonePass(StringView caseName, FunctionRef<bool()> load) {
            AllocTracker::ResetZoneStats();
            const bool loaded = load();
            if (!loaded) {
                ADD_FAILURE() << "the zone pass " << caseName.data() << " did not load";
                return false;
            }
            PrintZoneReport(std::cout, caseName, kZoneReportNames);
            return true;
        }

        String ZoneCaseName(StringView stem, FileFormat format) {
            return Format("{}-{}", stem, FormatName(format));
        }

        void RunLibraryZonePass(FileFormat format) {
            const AssetFile file = MakeFile(format, NextSalt(), kSpriteCount);
            TextureLibrary library;
            PrintZonePass(ZoneCaseName("library", format), [&] { return LoadFileByZone(file, library) && library.GetAll().size() == kSpriteCount; });
        }

        void RunWideZonePass(FileFormat format) {
            const AssetFile file = MakeWideFile(format, NextSalt(), kWideObjects);
            Vector<WideItem> items;
            PrintZonePass(ZoneCaseName("wide", format), [&] { return LoadWideFileByZone(file, items) && items.size() == kWideObjects; });
        }

        void RunSceneZonePass(const SceneFixture& fixture, FileFormat format, StringView stem = "scene") {
            IntrusivePtr<SceneManager> manager;
            PrintZonePass(ZoneCaseName(stem, format), [&] {
                manager = format == FileFormat::Xml ? fixture.LoadFromXmlByZone() : fixture.LoadFromBinaryByZone();
                return manager != nullptr;
            });
        }

        /// The XML scene again with the deserialize in the mode `Off`: its difference to `scene-xml` is the cost of `Warn`
        void RunSceneOffZonePass(const SceneFixture& fixture) {
            IntrusivePtr<SceneManager> manager;
            PrintZonePass("scene-xml-off", [&] {
                manager = fixture.LoadFromXmlByZone(DiagnosticsMode::Off);
                return manager != nullptr;
            });
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

    TEST(AssetLoadAllocTest, TheParticleSceneLoadsTheSameFromXmlAndFromItsCookedBytes) {
        EngineEnvironment::EnsureCoreServices();
        const SceneFixture fixture = MakeParticleFixture();
        const auto fromXml = fixture.LoadFromXml();
        const auto fromBinary = fixture.LoadFromBinary();
        ASSERT_NE(fromXml, nullptr);
        ASSERT_NE(fromBinary, nullptr);
        const String expected = PrintedScenes(*fromXml);
        EXPECT_NE(expected.find(Format("Emitter{}", kParticleEmitters - 1)), String::npos);
        EXPECT_EQ(PrintedScenes(*fromBinary), expected);
        size_t emitters = 0;
        CountLive(*fromBinary, emitters);
        EXPECT_EQ(emitters, kParticleEmitters);
    }

    TEST(AssetLoadAllocTest, TheParticleSceneRunsParticles) {
        EngineEnvironment::EnsureCoreServices();
        const SceneFixture fixture = MakeParticleFixture();
        const ParticleRun run = RunParticleFrames(fixture, false);
        ASSERT_TRUE(run.ran);
        EXPECT_EQ(run.emitters, kParticleEmitters);
        EXPECT_GT(run.live, 0u);
        EXPECT_GT(run.geometryCalls, 0u);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(run.updateCalls, static_cast<uint64_t>(kRunFrames)) << "the zone name of the lookup is the one of the scope";
            EXPECT_EQ(run.drawCalls, static_cast<uint64_t>(kRunFrames));
        }
        EXPECT_EQ(CoreManager::GetRendererManager().GetRenderer(), nullptr) << "the renderer is given back";
    }

    TEST(AssetLoadAllocTest, TheZoneVersionsOfTheLoadsLoadWhatTheMeasuredOnesLoad) {
        EngineEnvironment::EnsureCoreServices();
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            const uint32_t salt = NextSalt();
            TextureLibrary library;
            ASSERT_TRUE(LoadFileByZone(MakeFile(format, salt, kSpriteCount), library)) << FormatName(format);
            ASSERT_NO_FATAL_FAILURE(ExpectLoaded(library, salt, kSpriteCount));

            const uint32_t wideSalt = NextSalt();
            Vector<WideItem> items;
            ASSERT_TRUE(LoadWideFileByZone(MakeWideFile(format, wideSalt, kWideObjects), items)) << FormatName(format);
            ASSERT_NO_FATAL_FAILURE(ExpectWideLoaded(items, wideSalt, kWideObjects));
        }

        const SceneFixture fixture;
        const auto plain = fixture.LoadFromXml();
        const auto fromXml = fixture.LoadFromXmlByZone();
        const auto fromXmlOff = fixture.LoadFromXmlByZone(DiagnosticsMode::Off);
        const auto fromBinary = fixture.LoadFromBinaryByZone();
        ASSERT_NE(plain, nullptr);
        ASSERT_NE(fromXml, nullptr);
        ASSERT_NE(fromXmlOff, nullptr);
        ASSERT_NE(fromBinary, nullptr);
        const String expected = PrintedScenes(*plain);
        EXPECT_EQ(PrintedScenes(*fromXml), expected);
        EXPECT_EQ(PrintedScenes(*fromXmlOff), expected);
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
            const SceneFixture particles = MakeParticleFixture();
            for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
                const LoadMeasure median = MedianOf([&] { return MeasureSceneLoad(particles, format); });
                std::cout << "AssetLoadParticles format=" << FormatName(format) << " emitters=" << kParticleEmitters << " allocs=" << median.allocs << " bytes=" << median.bytes
                          << " loadMs=" << median.loadMs << "\n";
            }
        }
    }

    /// The zone pass of the benchmark: one more load of a file of fresh names per case, its phases in zones, and the allocations per
    /// zone (HowToProfile.md, "The asset load by zone"). A separate run, so the medians of `DISABLED_LoadOfFreshNames` carry no zone cost.
    TEST(AssetLoadAllocBenchTest, DISABLED_LoadByZone) {
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            RunLibraryZonePass(format);
        }
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            RunWideZonePass(format);
        }
        EngineEnvironment::EnsureCoreServices();
        const SceneFixture fixture;
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            RunSceneZonePass(fixture, format);
        }
        RunSceneOffZonePass(fixture);
        const SceneFixture particles = MakeParticleFixture();
        for (const FileFormat format : {FileFormat::Xml, FileFormat::Binary}) {
            RunSceneZonePass(particles, format, "particles");
        }
        if (!RunParticleFrames(particles, true).ran) {
            ADD_FAILURE() << "the particle scene did not load";
        }
    }

}  // namespace BECore::Tests
