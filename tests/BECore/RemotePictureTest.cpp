// The picture of a remote program read from the answers of `screenshot`, which are a peer's.

#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Utils/Base64.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLScreenshot.h>
#include <Widgets/RemoteEditor/RemotePicture.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The base64 text of a PNG of @p width by @p height pixels of the grey @p shade
        String PngText(uint32_t width, uint32_t height, uint8_t shade) {
            const auto png = EncodePng(PixelBuffer{width, height, Vector<uint8_t>(size_t{width} * height * kBytesPerPixel, shade)}, 0);
            EXPECT_TRUE(png.has_value());
            return png ? Base64Encode(Span<const uint8_t>(png->bytes.data(), png->bytes.size())) : String{};
        }

        String Answer(int64_t width, int64_t height, StringView png) {
            return Format(R"({{"height":{},"png_base64":"{}","width":{}}})", height, png, width);
        }

        void Load(RemotePicture& picture, StringView result) {
            JsonTape tape;
            ASSERT_TRUE(tape.Parse(result).has_value()) << String(result).c_str();
            picture.Load(JsonView(tape, 0));
        }

    }  // namespace

    TEST(RemotePictureTest, AFrameIsKeptAsRgbaAndRepeatingItChangesNothing) {
        RemotePicture picture;
        EXPECT_TRUE(picture.GetImage().pixels.empty());

        Load(picture, Answer(2, 3, PngText(2, 3, 90)));

        EXPECT_FALSE(picture.GetError().IsSet()) << picture.GetError().message.c_str();
        EXPECT_EQ(picture.GetImage().width, 2u);
        EXPECT_EQ(picture.GetImage().height, 3u);
        EXPECT_TRUE(picture.GetImage().pixels == Vector<uint8_t>(size_t{2} * 3 * kBytesPerPixel, 90));
        const uint64_t first = picture.GetRevision();
        const uint8_t* const pixels = picture.GetImage().pixels.data();
        Load(picture, Answer(2, 3, PngText(2, 3, 90)));
        EXPECT_EQ(picture.GetRevision(), first);
        EXPECT_EQ(picture.GetImage().pixels.data(), pixels);

        Load(picture, Answer(2, 3, PngText(2, 3, 91)));
        EXPECT_NE(picture.GetRevision(), first);
        EXPECT_EQ(picture.GetImage().pixels.front(), 91);
    }

    TEST(RemotePictureTest, ATextWithEscapesIsReadAsTheTextItStandsFor) {
        const String png = PngText(1, 1, 255);
        String escaped;
        for (const char character : png) {
            if (character == '/') {
                escaped += "\\/";
            } else {
                escaped.push_back(character);
            }
        }
        escaped.replace(0, 1, "\\u0069");  // a PNG's base64 starts with `i`
        RemotePicture picture;

        Load(picture, Answer(1, 1, escaped));

        EXPECT_FALSE(picture.GetError().IsSet()) << picture.GetError().message.c_str();
        EXPECT_EQ(picture.GetImage().width, 1u);
    }

    TEST(RemotePictureTest, AnAnswerThatCannotBeShownLeavesTheFrameAndSaysWhy) {
        RemotePicture picture;
        Load(picture, Answer(2, 2, PngText(2, 2, 10)));
        const uint64_t revision = picture.GetRevision();
        const String other = PngText(3, 2, 20);

        const String bad[] = {
            "5",
            "{}",
            R"({"png_base64":7,"width":2,"height":2})",
            Format(R"({{"png_base64":"{}"}})", other),
            Answer(0, 2, other),
            Answer(3, -2, other),
            Answer(kMaxScreenshotSize + 1, 2, other),
            Answer(2, 3, other),  // the sides of another picture
            Answer(3, 2, "not base64"),
            Answer(3, 2, "bm90IGEgcGljdHVyZQ=="),
            Answer(3, 2, other.substr(0, 32)),
            Answer(1, 1, PngText(kMaxScreenshotSize + 1, 1, 0)),  // sides the answer does not admit to
        };
        for (const String& result : bad) {
            Load(picture, result);
            EXPECT_TRUE(picture.GetError().IsSet()) << result.substr(0, 80).c_str();
            EXPECT_TRUE(picture.GetError().code.empty());
            EXPECT_EQ(picture.GetRevision(), revision) << result.substr(0, 80).c_str();
            EXPECT_EQ(picture.GetImage().width, 2u);
        }

        // The broken picture says the same when it comes again, and the frame that is shown ends the error
        Load(picture, Answer(3, 2, "not base64"));
        const String reason = picture.GetError().message;
        Load(picture, Answer(3, 2, "not base64"));
        EXPECT_EQ(picture.GetError().message, reason);
        Load(picture, Answer(2, 2, PngText(2, 2, 10)));
        EXPECT_FALSE(picture.GetError().IsSet());
        EXPECT_EQ(picture.GetRevision(), revision);
    }

    TEST(RemotePictureTest, ARefusalKeepsItsCodeAndClearForgetsTheFrameUnderANewRevision) {
        RemotePicture picture;
        Load(picture, Answer(2, 2, PngText(2, 2, 10)));
        const uint64_t revision = picture.GetRevision();
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(R"({"code":"capture_failed","message":"no frame"})").has_value());

        picture.Refuse(JsonView(tape, 0));
        EXPECT_EQ(picture.GetError().code, "capture_failed");
        EXPECT_EQ(picture.GetError().message, "no frame");
        picture.Refuse(JsonView{});
        EXPECT_TRUE(picture.GetError().IsSet()) << "a refusal that says nothing is one still";

        picture.Clear();
        EXPECT_TRUE(picture.GetImage().pixels.empty());
        EXPECT_FALSE(picture.GetError().IsSet());
        EXPECT_GT(picture.GetRevision(), revision);
        Load(picture, Answer(2, 2, PngText(2, 2, 10)));
        EXPECT_FALSE(picture.GetImage().pixels.empty()) << "the same text after Clear() is a frame again";
    }

    TEST(RemotePictureTest, APeersLongRefusalIsKeptAsOneShortLine) {
        // The two-byte characters start at an odd offset, so the limit falls inside one
        String sent = "a\\nb";
        for (size_t i = 0; i < RemoteCallError::kMaxTextBytes; ++i) {
            sent += "\xD0\xB6";
        }
        String kept = "a b";
        while (kept.size() + 2 <= RemoteCallError::kMaxTextBytes) {
            kept += "\xD0\xB6";
        }
        kept += "...";
        const String refusal = Format(R"({{"code":"capture_failed","message":"{}"}})", sent);
        JsonTape tape;
        ASSERT_TRUE(tape.Parse(refusal).has_value());
        RemotePicture picture;

        picture.Refuse(JsonView(tape, 0));

        EXPECT_EQ(picture.GetError().code, "capture_failed");
        EXPECT_EQ(picture.GetError().message, kept);
    }

}  // namespace BECore::Tests
