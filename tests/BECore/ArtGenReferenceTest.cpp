#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        Json ReferenceJson() {
            return Json{{"mode", "init"}, {"image", "assets/artgen/rook.png"}, {"weight", 0.4}};
        }

        String ErrorOf(const Json& value) {
            const auto read = ReadArtGenReference(value);
            return read ? String() : read.error();
        }

    }  // namespace

    TEST(ArtGenReferenceTest, AReferenceReadFromJsonWritesBackTheSame) {
        const auto read = ReadArtGenReference(ReferenceJson());
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->mode, "init");
        EXPECT_EQ(read->image, "assets/artgen/rook.png");
        EXPECT_EQ(read->weight, Optional<double>(0.4));
        EXPECT_EQ(ArtGenReferenceToJson(*read), ReferenceJson());
    }

    TEST(ArtGenReferenceTest, AMissingOrNullWeightIsLeftOutOfTheJson) {
        Json json = ReferenceJson();
        json.erase("weight");
        auto read = ReadArtGenReference(json);
        ASSERT_TRUE(read.has_value());
        EXPECT_FALSE(read->weight.has_value());
        EXPECT_EQ(FindJsonMember(ArtGenReferenceToJson(*read), "weight"), nullptr);

        json["weight"] = Json();
        read = ReadArtGenReference(json);
        ASSERT_TRUE(read.has_value());
        EXPECT_FALSE(read->weight.has_value());
    }

    TEST(ArtGenReferenceTest, AnIntegerWeightIsANumber) {
        Json json = ReferenceJson();
        json["weight"] = 1;
        const auto read = ReadArtGenReference(json);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->weight, Optional<double>(1.0));
    }

    TEST(ArtGenReferenceTest, ShapeErrorsNameTheKey) {
        EXPECT_NE(ErrorOf(Json::array()).find("'reference' must be an object"), String::npos);
        EXPECT_NE(ErrorOf(Json::object()).find("reference.mode"), String::npos);

        Json json = ReferenceJson();
        json["mode"] = 3;
        EXPECT_NE(ErrorOf(json).find("reference.mode"), String::npos);
        json["mode"] = "";
        EXPECT_NE(ErrorOf(json).find("reference.mode"), String::npos);

        json = ReferenceJson();
        json.erase("image");
        EXPECT_NE(ErrorOf(json).find("reference.image"), String::npos);
        json["image"] = "";
        EXPECT_NE(ErrorOf(json).find("reference.image"), String::npos);
        json["image"] = 7;
        EXPECT_NE(ErrorOf(json).find("reference.image"), String::npos);

        json = ReferenceJson();
        json["weight"] = "x";
        EXPECT_NE(ErrorOf(json).find("reference.weight"), String::npos);
    }

    TEST(ArtGenReferenceTest, TheServiceNotTheShapeCheckJudgesTheModeAndTheRange) {
        Json json = ReferenceJson();
        json["mode"] = "style";
        json["weight"] = 7.5;
        EXPECT_TRUE(ReadArtGenReference(json).has_value());
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

    TEST(ArtGenReferenceTest, ASpriteIsItsAcceptedFile) {
        const auto reference = ArtGenReferenceOfSprite("rook");
        EXPECT_EQ(reference.mode, "init");
        EXPECT_EQ(reference.image, "assets/artgen/rook.png");
        EXPECT_FALSE(reference.weight.has_value());
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

    TEST(ArtGenReferenceTest, EveryModeIsLookedUpByItsKey) {
        EXPECT_EQ(ArtGenReferenceModeOf("init").mode, "init");
        EXPECT_EQ(ArtGenReferenceModeOf("style").mode, "style");
        EXPECT_EQ(ArtGenReferenceModeOf("structure").mode, "structure");
        EXPECT_DOUBLE_EQ(ArtGenReferenceDefaultWeight("init"), 0.4);
        EXPECT_DOUBLE_EQ(ArtGenReferenceDefaultWeight("style"), 0.8);
        EXPECT_DOUBLE_EQ(ArtGenReferenceDefaultWeight("structure"), 0.8);
    }

    TEST(ArtGenReferenceTest, AnUnknownModeGetsTheFirstRow) {
        EXPECT_EQ(&ArtGenReferenceModeOf("depth"), &kArtGenReferenceModes[0]);
        EXPECT_EQ(&ArtGenReferenceModeOf(""), &kArtGenReferenceModes[0]);
        EXPECT_DOUBLE_EQ(ArtGenReferenceDefaultWeight("depth"), kArtGenReferenceModes[0].defaultWeight);
    }

    TEST(ArtGenReferenceTest, InitIsTheFirstModeThenStyleAndStructure) {
        ASSERT_EQ(kArtGenReferenceModes.size(), 3u);
        EXPECT_EQ(kArtGenReferenceModes[0].mode, "init");
        EXPECT_EQ(kArtGenReferenceModes[1].mode, "style");
        EXPECT_EQ(kArtGenReferenceModes[2].mode, "structure");
    }

    TEST(ArtGenReferenceTest, TheDefaultWeightOfEveryModeIsInsideItsSlidersRange) {
        for (const ArtGenReferenceModeRow& row : kArtGenReferenceModes) {
            EXPECT_LT(row.minWeight, row.maxWeight) << row.mode.data();
            EXPECT_GT(row.defaultWeight, row.minWeight) << row.mode.data();
            EXPECT_LT(row.defaultWeight, row.maxWeight) << row.mode.data();
            EXPECT_GT(row.minWeight, 0.0f) << row.mode.data() << ": the service's interval is open at 0";
        }
    }

    TEST(ArtGenReferenceTest, EveryModeHasATooltipAndAUniqueKey) {
        for (size_t i = 0; i < kArtGenReferenceModes.size(); ++i) {
            ASSERT_NE(kArtGenReferenceModes[i].tip, nullptr);
            EXPECT_NE(StringView(kArtGenReferenceModes[i].tip), "") << kArtGenReferenceModes[i].mode.data();
            for (size_t j = i + 1; j < kArtGenReferenceModes.size(); ++j) {
                EXPECT_NE(kArtGenReferenceModes[i].mode, kArtGenReferenceModes[j].mode);
            }
        }
    }

}  // namespace BECore::Tests
