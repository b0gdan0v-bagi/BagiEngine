#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        void WriteBytes(const std::filesystem::path& file, const void* data, size_t size) {
            std::FILE* stream = std::fopen(file.string().c_str(), "wb");
            ASSERT_NE(stream, nullptr);
            std::fwrite(data, 1, size, stream);
            std::fclose(stream);
        }

        /// The first bytes of a PNG of @p width x @p height: what ReadPngSize reads (the rest of the file does not matter to it)
        void WritePngHeader(const std::filesystem::path& file, uint32_t width, uint32_t height) {
            Array<uint8_t, 33> bytes{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
            for (int i = 0; i < 4; ++i) {
                bytes[16 + i] = static_cast<uint8_t>(width >> (24 - 8 * i));
                bytes[20 + i] = static_cast<uint8_t>(height >> (24 - 8 * i));
            }
            WriteBytes(file, bytes.data(), bytes.size());
        }

        void WriteText(const std::filesystem::path& file, StringView text) {
            WriteBytes(file, text.data(), text.size());
        }

        String ReadText(const std::filesystem::path& file) {
            String text;
            std::FILE* stream = std::fopen(file.string().c_str(), "rb");
            if (stream == nullptr) {
                return text;
            }
            char buffer[256];
            size_t read = 0;
            while ((read = std::fread(buffer, 1, sizeof(buffer), stream)) > 0) {
                text.append(buffer, read);
            }
            std::fclose(stream);
            return text;
        }

    }  // namespace

    class ArtGenAcceptTest : public ::testing::Test {
    protected:
        void SetUp() override {
            folder = TestTempDirectory("artgen_accept");
            std::filesystem::create_directories(folder / "out");
            std::filesystem::create_directories(folder / "assets");
            png = folder / "out" / "queen_7.png";
            provenance = folder / "out" / "queen_7.artgen.json";
            WritePngHeader(png, 1024, 768);
            WriteText(provenance, "{\"seed\": 7}");
        }

        void TearDown() override {
            std::error_code ignored;
            std::filesystem::remove_all(folder, ignored);
        }

        std::filesystem::path folder;
        std::filesystem::path png;
        std::filesystem::path provenance;
    };

    TEST(ArtGenSpriteNameTest, OnlyPlainNamesAreSpriteNames) {
        EXPECT_TRUE(IsValidSpriteName("white_knight"));
        EXPECT_TRUE(IsValidSpriteName("Gem-Red-2"));
        EXPECT_FALSE(IsValidSpriteName(""));
        EXPECT_FALSE(IsValidSpriteName("../config/x"));
        EXPECT_FALSE(IsValidSpriteName("a b"));
        EXPECT_FALSE(IsValidSpriteName("knight.png"));
        EXPECT_FALSE(IsValidSpriteName(String(kMaxSpriteNameLength + 1, 'a')));
        EXPECT_TRUE(IsValidSpriteName(String(kMaxSpriteNameLength, 'a')));
    }

    TEST_F(ArtGenAcceptTest, ThePngSizeComesFromItsHeader) {
        const auto size = ReadPngSize(png);
        ASSERT_TRUE(size.has_value());
        EXPECT_EQ(size->first, 1024u);
        EXPECT_EQ(size->second, 768u);
        EXPECT_FALSE(ReadPngSize(provenance).has_value());
        EXPECT_FALSE(ReadPngSize(folder / "missing.png").has_value());
    }

    TEST_F(ArtGenAcceptTest, TheVariantGoesIntoAssetsUnderTheSpriteName) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "white_queen");
        ASSERT_TRUE(copied.has_value()) << copied.error().c_str();
        EXPECT_EQ(copied->texturePath, "assets/artgen/white_queen.png");
        EXPECT_EQ(copied->width, 1024u);
        EXPECT_EQ(copied->height, 768u);
        EXPECT_TRUE(std::filesystem::exists(folder / "assets" / "artgen" / "white_queen.png"));
        EXPECT_EQ(ReadText(folder / "assets" / "artgen" / "white_queen.artgen.json"), "{\"seed\": 7}");
    }

    TEST_F(ArtGenAcceptTest, ASecondAcceptOfTheSameNameReplacesTheFiles) {
        ASSERT_TRUE(CopyVariantIntoAssets(png, provenance, folder / "assets", "queen").has_value());
        const auto other = folder / "out" / "queen_8.png";
        WritePngHeader(other, 512, 512);
        WriteText(provenance, "{\"seed\": 8}");
        const auto copied = CopyVariantIntoAssets(other, provenance, folder / "assets", "queen");
        ASSERT_TRUE(copied.has_value());
        EXPECT_EQ(copied->width, 512u);
        EXPECT_EQ(ReadPngSize(folder / "assets" / "artgen" / "queen.png")->first, 512u);
        EXPECT_EQ(ReadText(folder / "assets" / "artgen" / "queen.artgen.json"), "{\"seed\": 8}");
    }

    TEST_F(ArtGenAcceptTest, ABadNameOrANonPngIsRefusedAndWritesNothing) {
        EXPECT_FALSE(CopyVariantIntoAssets(png, provenance, folder / "assets", "../escape").has_value());
        EXPECT_FALSE(CopyVariantIntoAssets(provenance, provenance, folder / "assets", "queen").has_value());
        EXPECT_FALSE(std::filesystem::exists(folder / "assets" / "artgen"));
    }

    TEST_F(ArtGenAcceptTest, AVariantWithoutProvenanceIsStillAccepted) {
        std::filesystem::remove(provenance);
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen");
        ASSERT_TRUE(copied.has_value());
        EXPECT_FALSE(std::filesystem::exists(copied->provenanceFile));
    }

    TEST_F(ArtGenAcceptTest, TheProvenanceFileIsBesideTheMediaFile) {
        EXPECT_EQ(ProvenanceFileOf("assets/artgen/queen.png").generic_string(), "assets/artgen/queen.artgen.json");
        EXPECT_EQ(ProvenanceFileOf("assets/sounds/move.wav").generic_string(), "assets/sounds/move.artgen.json");
    }

    TEST_F(ArtGenAcceptTest, KeptIsAddedToTheCopiedProvenanceAndItsOwnKeysStay) {
        WriteText(provenance, "{\"seed\": 7, \"preset\": \"gem\"}");
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen");
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());

        const auto record = ParseJson(StringView(ReadText(copied->provenanceFile)));
        ASSERT_TRUE(record.has_value());
        EXPECT_EQ(GetJsonInt(*record, "seed").value_or(-1), 7);
        EXPECT_EQ(GetJsonString(*record, "preset").value_or(String()), "gem");
        const Json* kept = FindJsonMember(*record, "kept");
        ASSERT_NE(kept, nullptr);
        EXPECT_EQ(GetJsonInt(*kept, "seed").value_or(-1), 7);
        EXPECT_EQ(GetJsonString(*kept, "date").value_or(String()), "2026-11-17T10:00:00Z");
    }

    TEST_F(ArtGenAcceptTest, ASecondKeptReplacesTheFirst) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen");
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 9, "2026-11-17T11:00:00Z").has_value());

        const auto record = ParseJson(StringView(ReadText(copied->provenanceFile)));
        ASSERT_TRUE(record.has_value());
        const Json* kept = FindJsonMember(*record, "kept");
        ASSERT_NE(kept, nullptr);
        EXPECT_EQ(GetJsonInt(*kept, "seed").value_or(-1), 9);
        EXPECT_EQ(GetJsonString(*kept, "date").value_or(String()), "2026-11-17T11:00:00Z");
    }

    TEST_F(ArtGenAcceptTest, TheGameFileIsWhereAcceptWritesIt) {
        EXPECT_EQ(ArtGenGameFile("queen", "image"), "assets/artgen/queen.png");
        EXPECT_EQ(ArtGenGameFile("click", "audio"), "assets/artgen/click.wav");

        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen");
        ASSERT_TRUE(copied.has_value());
        EXPECT_EQ(copied->texturePath, ArtGenGameFile("queen", "image"));
    }

    TEST_F(ArtGenAcceptTest, KeepingAMissingOrBrokenProvenanceIsAnErrorThatNamesTheFile) {
        const auto missing = folder / "assets" / "nothing.artgen.json";
        const auto result = MarkProvenanceKept(missing, 7, "2026-11-17T10:00:00Z");
        ASSERT_FALSE(result.has_value());
        EXPECT_NE(result.error().find("nothing.artgen.json"), String::npos);
        EXPECT_FALSE(std::filesystem::exists(missing));

        WriteText(provenance, "[1, 2]");
        const auto notObject = MarkProvenanceKept(provenance, 7, "2026-11-17T10:00:00Z");
        ASSERT_FALSE(notObject.has_value());
        EXPECT_NE(notObject.error().find("queen_7.artgen.json"), String::npos);
        EXPECT_EQ(ReadText(provenance), "[1, 2]");
    }

    TEST_F(ArtGenAcceptTest, ASwitchRewritesTheProvenanceFromTheVariantWithoutKept) {
        const auto copied = CopyVariantIntoAssets(png, provenance, folder / "assets", "queen");
        ASSERT_TRUE(copied.has_value());
        ASSERT_TRUE(MarkProvenanceKept(copied->provenanceFile, 7, "2026-11-17T10:00:00Z").has_value());
        WriteText(provenance, "{\"seed\": 8}");
        ASSERT_TRUE(CopyVariantIntoAssets(png, provenance, folder / "assets", "queen").has_value());
        EXPECT_EQ(ReadText(copied->provenanceFile).find("kept"), String::npos);
    }

}  // namespace BECore::Tests
