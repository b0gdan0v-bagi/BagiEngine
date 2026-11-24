// ControlClock: the clock of an answer, written by the generated writer, and ControlCall::ReplyObject.

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlClock.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/ControlClockTypes.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Generated/ControlClockTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kFrameSeconds = 0.125;

        template <typename T>
        String Written(const T& value) {
            String text;
            JsonWriter writer(text);
            value.WriteJson(writer);
            EXPECT_TRUE(writer.IsComplete());
            return text;
        }

        /// The clock after the two frames of the fixture, as the wire writes it (the float of the fixed step widened to a double)
        constexpr StringView kClockText = R"({"deltaTime":0.125,"fixedDeltaTime":0.01666666753590107,"frame":2,"totalTime":0.25})";

        class ControlClockTest : public ::testing::Test {
        protected:
            void SetUp() override {
                CoreManager::GetTime().Reset();
                CoreManager::GetTime().Advance(kFrameSeconds);
                CoreManager::GetTime().Advance(kFrameSeconds);
            }

            void TearDown() override {
                CoreManager::GetTime().Reset();
            }
        };

    }  // namespace

    TEST_F(ControlClockTest, TheReadClockIsWrittenAsTheDomPathWroteIt) {
        EXPECT_EQ(Written(ControlClock::Read()), kClockText);
    }

    TEST_F(ControlClockTest, AnAnswerThatDerivesTheClockMergesItsKeysSorted) {
        ControlClockTestAnswer answer;
        static_cast<ControlClock&>(answer) = ControlClock::Read();

        EXPECT_EQ(Written(answer), R"({"a":1,"deltaTime":0.125,"fixedDeltaTime":0.01666666753590107,"frame":2,"fz":2,"totalTime":0.25,"zz":3})");
    }

    TEST_F(ControlClockTest, ReplyObjectAnswersWithTheWrittenValueAsTheResult) {
        ControlService service;
        service.RegisterHandler("clock", [](ControlCall& call) { call.ReplyObject(ControlClock::Read()); });

        String text;
        service.Post(MakeRequestText(4, "clock"), [&text](const String& reply) { text = reply; });
        service.Drain();

        EXPECT_EQ(text, Format(R"({{"id":4,"ok":true,"result":{}}})", kClockText));
    }

}  // namespace BECore::Tests
