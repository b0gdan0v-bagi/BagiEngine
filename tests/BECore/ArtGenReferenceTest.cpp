#include <BECore/Control/JsonWriter.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kReferenceText = R"({"image":"assets/artgen/rook.png","mode":"init","weight":0.4})";

        String ErrorOf(StringView text) {
            const auto read = ReadArtGenReference(JsonDoc(text));
            return read ? String() : read.error();
        }

        String ReferenceText(const ArtGenReference& reference) {
            String text;
            JsonWriter writer(text);
            WriteArtGenReference(writer, reference);
            return text;
        }

    }  // namespace

    TEST(ArtGenReferenceTest, AReferenceReadFromJsonWritesBackTheSame) {
        const auto read = ReadArtGenReference(JsonDoc(kReferenceText));
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->mode, "init");
        EXPECT_EQ(read->image, "assets/artgen/rook.png");
        EXPECT_EQ(read->weight, Optional<double>(0.4));
        EXPECT_EQ(ReferenceText(*read), String(kReferenceText));
    }

    TEST(ArtGenReferenceTest, AMissingOrNullWeightIsLeftOutOfTheJson) {
        auto read = ReadArtGenReference(JsonDoc(R"({"image":"a.png","mode":"style"})"));
        ASSERT_TRUE(read.has_value());
        EXPECT_FALSE(read->weight.has_value());
        EXPECT_EQ(ReferenceText(*read), R"({"image":"a.png","mode":"style"})");

        read = ReadArtGenReference(JsonDoc(R"({"image":"a.png","mode":"style","weight":null})"));
        ASSERT_TRUE(read.has_value());
        EXPECT_FALSE(read->weight.has_value());
    }

    TEST(ArtGenReferenceTest, AnIntegerWeightIsANumber) {
        const auto read = ReadArtGenReference(JsonDoc(R"({"image":"a.png","mode":"init","weight":1})"));
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->weight, Optional<double>(1.0));
        EXPECT_EQ(ReferenceText(*read), R"({"image":"a.png","mode":"init","weight":1.0})");
    }

    TEST(ArtGenReferenceTest, TheTextOfAStringIsEscapedAndDecoded) {
        const auto read = ReadArtGenReference(JsonDoc(R"({"image":"a \"b\"\\c.png","mode":"init"})"));
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->image, "a \"b\"\\c.png");
        EXPECT_EQ(ReferenceText(*read), R"({"image":"a \"b\"\\c.png","mode":"init"})");
    }

    TEST(ArtGenReferenceTest, ShapeErrorsNameTheKey) {
        EXPECT_NE(ErrorOf("[]").find("'reference' must be an object"), String::npos);
        EXPECT_NE(ErrorOf("{}").find("reference.mode"), String::npos);

        EXPECT_NE(ErrorOf(R"({"image":"a.png","mode":3})").find("reference.mode"), String::npos);
        EXPECT_NE(ErrorOf(R"({"image":"a.png","mode":""})").find("reference.mode"), String::npos);

        EXPECT_NE(ErrorOf(R"({"mode":"init"})").find("reference.image"), String::npos);
        EXPECT_NE(ErrorOf(R"({"image":"","mode":"init"})").find("reference.image"), String::npos);
        EXPECT_NE(ErrorOf(R"({"image":7,"mode":"init"})").find("reference.image"), String::npos);

        EXPECT_NE(ErrorOf(R"({"image":"a.png","mode":"init","weight":"x"})").find("reference.weight"), String::npos);
        EXPECT_NE(ErrorOf(R"({"image":"a.png","mode":"init","weight":1e999})").find("reference.weight"), String::npos) << "a number that is not finite";
    }

    TEST(ArtGenReferenceTest, TheServiceNotTheShapeCheckJudgesTheModeAndTheRange) {
        EXPECT_TRUE(ReadArtGenReference(JsonDoc(R"({"image":"a.png","mode":"style","weight":7.5})")).has_value());
    }

    TEST(ArtGenReferenceTest, AFileInsideTheGameRootIsRelativeWithSlashes) {
        const auto reference = ArtGenReferenceOfFile("D:/Games/Chess", "D:/Games/Chess/artgen/out/set/rook/x_123.png");
        EXPECT_EQ(reference.mode, "init");
        EXPECT_EQ(reference.image, "artgen/out/set/rook/x_123.png");
        EXPECT_FALSE(reference.weight.has_value());
    }

    TEST(ArtGenReferenceTest, BackslashesOfAWindowsPathAreTurned) {
        EXPECT_EQ(ArtGenReferenceOfFile("D:\\Games\\Chess\\", "D:\\Games\\Chess\\artgen\\out\\x.png").image, "artgen/out/x.png");
        EXPECT_EQ(ArtGenReferenceOfFile("D:\\Games\\Chess", "E:\\Pictures\\a b.png").image, "E:/Pictures/a b.png");
    }

    TEST(ArtGenReferenceTest, AFileOutsideTheGameRootStaysAbsolute) {
        EXPECT_EQ(ArtGenReferenceOfFile("/home/u/Chess", "/home/u/Chess2/x.png").image, "/home/u/Chess2/x.png");
        EXPECT_EQ(ArtGenReferenceOfFile("/home/u/Chess", "/tmp/x.png").image, "/tmp/x.png");
        EXPECT_EQ(ArtGenReferenceOfFile("", "/tmp/x.png").image, "/tmp/x.png");
        EXPECT_EQ(ArtGenReferenceOfFile("/home/u/Chess", "/home/u/Chess").image, "/home/u/Chess");
    }

    TEST(ArtGenReferenceTest, AParentStepKeepsThePathAbsolute) {
        EXPECT_EQ(ArtGenReferenceOfFile("/home/u/Chess", "/home/u/Chess/a/../../x.png").image, "/home/u/Chess/a/../../x.png");
    }

    TEST(ArtGenReferenceTest, TheBuildersTakeTheModeAndDefaultToInit) {
        EXPECT_EQ(ArtGenReferenceOfFile("/g/Chess", "/g/Chess/a.png", "structure").mode, "structure");
        EXPECT_EQ(ArtGenReferenceOfFile("/g/Chess", "/g/Chess/a.png", "structure").image, "a.png");
        EXPECT_EQ(ArtGenReferenceOfFile("/g/Chess", "/g/Chess/a.png").mode, "init");
        EXPECT_EQ(ArtGenReferenceOfSprite("rook", "structure").mode, "structure");
        EXPECT_EQ(ArtGenReferenceOfSprite("rook", "structure").image, "assets/artgen/rook.png");
        EXPECT_EQ(ArtGenReferenceOfSprite("rook").mode, "init");
        EXPECT_FALSE(ArtGenReferenceOfSprite("rook").weight.has_value());
    }

    TEST(ArtGenReferenceTest, ATypedPathWithNothingInItIsNone) {
        EXPECT_FALSE(ArtGenReferenceOfTypedPath("/g/Chess", "", "structure").has_value());
        EXPECT_FALSE(ArtGenReferenceOfTypedPath("/g/Chess", "  \t ", "structure").has_value());
        EXPECT_FALSE(ArtGenReferenceOfTypedPath("/g/Chess", " \"\" ", "structure").has_value());
    }

    TEST(ArtGenReferenceTest, ATypedPathLosesTheSpacesAndTheQuotesAPasteAdds) {
        const auto quoted = ArtGenReferenceOfTypedPath("/g/Chess", "  \"/tmp/a b.png\"  ", "structure");
        ASSERT_TRUE(quoted.has_value());
        EXPECT_EQ(quoted->mode, "structure");
        EXPECT_EQ(quoted->image, "/tmp/a b.png");
        EXPECT_FALSE(quoted->weight.has_value());
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("/g/Chess", "'sketch.png'", "structure")).image, "sketch.png");
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("/g/Chess", " \" sketch.png \" ", "structure")).image, "sketch.png");
    }

    TEST(ArtGenReferenceTest, ATypedAbsolutePathInsideTheGameRootBecomesRelativeAndOutsideStaysAbsolute) {
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("D:\\Games\\Chess", "D:\\Games\\Chess\\sketches\\rook.png", "structure")).image, "sketches/rook.png");
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("/g/Chess", "/g/Chess/sketches/rook.png", "structure")).image, "sketches/rook.png");
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("/g/Chess", "/tmp/rook.png", "structure")).image, "/tmp/rook.png");
    }

    TEST(ArtGenReferenceTest, ATypedRelativePathStaysRelativeWithItsParentStepsAndOnlyItsBackslashesAreTurned) {
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("/g/Chess", "sketches/rook.png", "structure")).image, "sketches/rook.png");
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("/g/Chess", "../elsewhere/rook.png", "structure")).image, "../elsewhere/rook.png");
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("D:\\Games\\Chess", "assets\\sketch.png", "init")).image, "assets/sketch.png");
        EXPECT_EQ(Require(ArtGenReferenceOfTypedPath("", "rook.png", "style")).mode, "style");
    }

    TEST(ArtGenReferenceTest, OnlyAPictureOfAVariantCanBeAReference) {
        const auto image = ArtGenReferenceOfVariant("/g/Chess", "image", "/g/Chess/artgen/out/x_1.png");
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ(image->mode, "init");
        EXPECT_EQ(image->image, "artgen/out/x_1.png");
        EXPECT_FALSE(ArtGenReferenceOfVariant("/g/Chess", "audio", "/g/Chess/artgen/out/x_1.flac").has_value());
        EXPECT_FALSE(ArtGenReferenceOfVariant("/g/Chess", "image", "").has_value());
    }

    TEST(ArtGenReferenceTest, TheFilePathIsUnderTheGameRootUnlessAbsolute) {
        EXPECT_EQ(ArtGenReferenceFilePath("/g/Chess", "assets/artgen/rook.png"), "/g/Chess/assets/artgen/rook.png");
        EXPECT_EQ(ArtGenReferenceFilePath("D:\\g\\Chess\\", "artgen/out/x.png"), "D:/g/Chess/artgen/out/x.png");
        EXPECT_EQ(ArtGenReferenceFilePath("/g/Chess", "/tmp/x.png"), "/tmp/x.png");
        EXPECT_EQ(ArtGenReferenceFilePath("/g/Chess", "E:/Pictures/x.png"), "E:/Pictures/x.png");
        EXPECT_EQ(ArtGenReferenceFilePath("", "assets/x.png"), "assets/x.png");
    }

    TEST(ArtGenReferenceTest, TheSlotShowsTheNameOfTheFile) {
        EXPECT_EQ(ArtGenReferenceFileName("assets/artgen/rook.png"), "rook.png");
        EXPECT_EQ(ArtGenReferenceFileName("E:\\Pictures\\a b.png"), "a b.png");
        EXPECT_EQ(ArtGenReferenceFileName("x.png"), "x.png");
    }

    TEST(ArtGenReferenceTest, EveryModeIsLookedUpByItsKeyAndNoKeyIsTwice) {
        for (const ArtGenReferenceModeRow& row : kArtGenReferenceModes) {
            EXPECT_EQ(&ArtGenReferenceModeOf(row.mode), &row) << row.mode.data();
        }
    }

    TEST(ArtGenReferenceTest, AnUnknownModeGetsTheFirstRow) {
        EXPECT_EQ(&ArtGenReferenceModeOf("depth"), &kArtGenReferenceModes[0]);
        EXPECT_EQ(&ArtGenReferenceModeOf(""), &kArtGenReferenceModes[0]);
        EXPECT_DOUBLE_EQ(ArtGenReferenceDefaultWeight("depth"), kArtGenReferenceModes[0].defaultWeight);
    }

}  // namespace BECore::Tests
