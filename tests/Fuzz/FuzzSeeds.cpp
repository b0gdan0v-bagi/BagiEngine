#include "FuzzSeeds.h"

#include "FuzzProbeAccess.h"
#include "FuzzTargets.h"

#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlSerializer.h>

namespace BECore::Fuzz {

    namespace {

        constexpr const char* kBinaryFolder = "binary";
        constexpr const char* kXmlFolder = "xml";
        constexpr const char* kSaveFolder = "save";
        constexpr const char* kSceneFolder = "scene";

        /// The game id and the schema versions of the save seeds against the schema of FuzzSaveEnvelope (game "FuzzGame", writes 3, reads 2 and 3)
        constexpr eastl::string_view kSaveGame = "FuzzGame";
        constexpr uint32_t kSaveVersionCurrent = 3;
        constexpr uint32_t kSaveVersionOldest = 2;
        constexpr uint32_t kSaveVersionTooOld = 1;
        constexpr uint32_t kSaveVersionTooNew = 4;

        /// A scene that references the node file with overrides of the node and of one component
        constexpr eastl::string_view kSceneWithOverrides = R"(<SceneManager><scenes><scene type="Scene" name="Main" viewWidth="800" viewHeight="600"><nodes>
  <node name="Plain"/>
  <node ref="nodes/Fuzz" name="Renamed" layer="3"><components><component type="TransformComponent" x="40"/></components></node>
</nodes></scene></scenes></SceneManager>)";

        /// A scene with a reference and no override, and one that the node file cannot satisfy
        constexpr eastl::string_view kSceneWithReference =
            R"(<SceneManager><scenes><scene type="Scene" name="Main"><nodes><node ref="nodes/Fuzz"/><node ref="nodes/Missing"/></nodes></scene></scenes></SceneManager>)";

        /// A node file of the built-in components
        constexpr eastl::string_view kBoxNodeFile = R"(<node name="Box" layer="2"><components>
  <component type="TransformComponent" x="1" y="2" width="30" height="40"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
  <component type="ClickableComponent" actionName="box"/>
</components></node>)";

        /// A node file with a canvas of UI elements
        constexpr eastl::string_view kCanvasNodeFile = R"(<node name="Panel"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UIElement" name="Title" x="8" y="8" width="184" height="30" fontSize="24">
        <components>
          <component type="UITextComponent" text="Base"/>
        </components>
      </element>
    </elements>
  </component>
</components></node>)";

        /// How many bytes a cut from the end takes off the stream that "truncated" is made from
        constexpr size_t kTruncatedBytes = 9;

        /// Offset of the u32 length of the only string entry of MakeStringStream(): header, tag, name length, the name "s"
        constexpr size_t kStringLengthOffset = BinaryFormat::HeaderSize + 1 + sizeof(uint16_t) + 1;

        /// Offset of the u32 count of the only array entry of MakeArrayStream(): header, tag, name length, the name "a", the size
        constexpr size_t kArrayCountOffset = BinaryFormat::HeaderSize + 1 + sizeof(uint16_t) + 1 + sizeof(uint32_t);

        constexpr uint32_t kHugeLength = 0xFFFFFFFFu;

        FuzzBytes ToBytes(const BinarySerializer& serializer) {
            const eastl::vector<uint8_t>& data = serializer.GetData();
            return FuzzBytes(data.begin(), data.end());
        }

        /// A stream with every tag, nested objects, an array of objects, an array of values and a string
        FuzzBytes MakeAllTagsStream() {
            BinarySerializer writer;
            writer.WriteAttribute("flag", true);
            writer.WriteAttribute("i8", static_cast<int8_t>(-8));
            writer.WriteAttribute("u8", static_cast<uint8_t>(200));
            writer.WriteAttribute("i16", static_cast<int16_t>(-1600));
            writer.WriteAttribute("u16", static_cast<uint16_t>(60000));
            writer.WriteAttribute("i32", static_cast<int32_t>(-320000));
            writer.WriteAttribute("u32", static_cast<uint32_t>(4000000000u));
            writer.WriteAttribute("i64", static_cast<int64_t>(-6400000000000));
            writer.WriteAttribute("u64", static_cast<uint64_t>(18000000000000000000ull));
            writer.WriteAttribute("f32", 1.5f);
            writer.WriteAttribute("f64", -2.25);
            writer.WriteAttribute("text", eastl::string_view("hello"));

            if (writer.BeginObject("player")) {
                writer.WriteAttribute("health", static_cast<int32_t>(100));
                writer.WriteAttribute("name", eastl::string_view("Hero"));
                if (writer.BeginObject("position")) {
                    writer.WriteAttribute("x", 1.0f);
                    writer.WriteAttribute("y", 2.0f);
                    writer.EndObject();
                }
                writer.EndObject();
            }

            size_t itemCount = 3;
            if (writer.BeginArray("items", "item", itemCount)) {
                for (int32_t id = 0; id < 3; ++id) {
                    if (writer.BeginArrayElement()) {
                        writer.WriteAttribute("id", id);
                        writer.WriteAttribute("label", eastl::string_view(id == 0 ? "first" : "other"));
                        writer.EndArrayElement();
                    }
                }
                writer.EndArray();
            }

            size_t valueCount = 4;
            if (writer.BeginArray("values", "v", valueCount)) {
                for (int32_t value = 10; value < 14; ++value) {
                    if (writer.BeginArrayElement()) {
                        writer.WriteAttribute("value", value);
                        writer.EndArrayElement();
                    }
                }
                writer.EndArray();
            }
            return ToBytes(writer);
        }

        /// Only the string "s" = "abc"
        FuzzBytes MakeStringStream() {
            BinarySerializer writer;
            writer.WriteAttribute("s", eastl::string_view("abc"));
            return ToBytes(writer);
        }

        /// Only an array "a" of two elements with one value each
        FuzzBytes MakeArrayStream() {
            BinarySerializer writer;
            size_t count = 2;
            if (writer.BeginArray("a", "e", count)) {
                for (int32_t value = 0; value < 2; ++value) {
                    if (writer.BeginArrayElement()) {
                        writer.WriteAttribute("v", value);
                        writer.EndArrayElement();
                    }
                }
                writer.EndArray();
            }
            return ToBytes(writer);
        }

        FuzzBytes TextBytes(eastl::string_view text) {
            const auto* begin = reinterpret_cast<const uint8_t*>(text.data());
            return FuzzBytes(begin, begin + text.size());
        }

        /// The input of FuzzSceneNode: the scene text, a NUL byte, the node file
        FuzzBytes SceneAndNode(eastl::string_view scene, eastl::string_view node) {
            FuzzBytes bytes = TextBytes(scene);
            bytes.push_back(0);
            const FuzzBytes nodeBytes = TextBytes(node);
            bytes.insert(bytes.end(), nodeBytes.begin(), nodeBytes.end());
            return bytes;
        }

        /// The probe state as the XML a content file holds
        FuzzBytes MakeProbeXml() {
            XmlSerializer writer;
            MakeFilledProbe().Serialize(writer);
            const eastl::string text = writer.SaveToString();
            return TextBytes(eastl::string_view(text.data(), text.size()));
        }

        void StoreWord(FuzzBytes& bytes, size_t offset, uint32_t value) {
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
        }

        /// Make the payload size of the header true again after the stream was cut
        void FixPayloadSize(FuzzBytes& bytes) {
            StoreWord(bytes, BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(bytes.size() - BinaryFormat::HeaderSize));
        }

    }  // namespace

    eastl::vector<FuzzSeedFile> MakeGeneratedSeeds() {
        eastl::vector<FuzzSeedFile> seeds;

        const FuzzBytes allTags = MakeAllTagsStream();
        seeds.push_back({kBinaryFolder, "all-tags.bin", allTags});

        // The header is right and the last entry is cut: the sizes inside lie
        FuzzBytes truncated = allTags;
        truncated.resize(truncated.size() - kTruncatedBytes);
        FixPayloadSize(truncated);
        seeds.push_back({kBinaryFolder, "truncated.bin", truncated});

        // Cut with no care for the header
        FuzzBytes shortStream = allTags;
        shortStream.resize(BinaryFormat::HeaderSize + 5);
        seeds.push_back({kBinaryFolder, "header-only-cut.bin", shortStream});

        FuzzBytes hugeString = MakeStringStream();
        StoreWord(hugeString, kStringLengthOffset, kHugeLength);
        seeds.push_back({kBinaryFolder, "string-length-ffffffff.bin", hugeString});

        FuzzBytes hugeCount = MakeArrayStream();
        StoreWord(hugeCount, kArrayCountOffset, kHugeLength);
        seeds.push_back({kBinaryFolder, "array-count-ffffffff.bin", hugeCount});

        seeds.push_back({kBinaryFolder, "array.bin", MakeArrayStream()});

        seeds.push_back({kXmlFolder, "probe-filled.xml", MakeProbeXml()});

        // Saves of the schema FuzzSaveEnvelope loads with: the current version, the oldest it still reads, one too new, one too old, another game
        seeds.push_back({kSaveFolder, "save-current.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionCurrent)});
        seeds.push_back({kSaveFolder, "save-oldest.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionOldest)});
        seeds.push_back({kSaveFolder, "save-too-old.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionTooOld)});
        seeds.push_back({kSaveFolder, "save-too-new.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionTooNew)});
        seeds.push_back({kSaveFolder, "save-other-game.bin", MakeSaveFileBytes("OtherGame", kSaveVersionCurrent)});

        seeds.push_back({kSceneFolder, "scene-overrides.bin", SceneAndNode(kSceneWithOverrides, kBoxNodeFile)});
        seeds.push_back({kSceneFolder, "scene-reference.bin", SceneAndNode(kSceneWithReference, kCanvasNodeFile)});
        seeds.push_back({kSceneFolder, "node-file-only.xml", TextBytes(kBoxNodeFile)});
        seeds.push_back({kSceneFolder, "node-file-canvas.xml", TextBytes(kCanvasNodeFile)});
        return seeds;
    }

    bool WriteGeneratedSeeds(const std::filesystem::path& corpusRoot) {
        for (const FuzzSeedFile& seed : MakeGeneratedSeeds()) {
            const std::filesystem::path folder = corpusRoot / seed.folder;
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (error) {
                return false;
            }
            std::ofstream file(folder / seed.name, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(seed.bytes.data()), static_cast<std::streamsize>(seed.bytes.size()));
            if (!file) {
                return false;
            }
        }
        return true;
    }

}  // namespace BECore::Fuzz
