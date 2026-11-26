#include <BECore/Config/ConfigNames.h>
#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Events/ConfigEvents.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/Feedback/ToastFeed.h>
#include <Widgets/Feedback/ToastList.h>

namespace BECore::Tests {

    namespace {

        constexpr float kFrameSeconds = 1.0f / 64.0f;
        constexpr float kHalfFadeIn = ToastList::kFadeInSeconds * 0.5f;
        constexpr float kHalfFadeOut = ToastList::kFadeOutSeconds * 0.5f;
        constexpr float kTolerance = 1.0e-4f;
        constexpr int kSteadyFrames = 10;

        const PoolString kSomeConfig = "ToastedConfig"_intern;

        StringView TextOf(const ToastList& list, size_t index) {
            return StringView(list.At(index).text.data(), list.At(index).text.size());
        }

    }  // namespace

    TEST(ToastListTest, AFullListDropsTheOldest) {
        ToastList list;
        for (size_t i = 0; i <= ToastList::kCapacity; ++i) {
            list.Push(ToastLevel::Info, "toast {}"_format(i));
        }

        ASSERT_EQ(list.Size(), ToastList::kCapacity);
        EXPECT_EQ(TextOf(list, 0), "toast 1");
        EXPECT_EQ(TextOf(list, ToastList::kCapacity - 1), "toast {}"_format(ToastList::kCapacity));
    }

    TEST(ToastListTest, ARepeatCountsBecomesTheNewestAndStartsItsLifeAgainWithoutFadingIn) {
        ToastList list;
        list.Push(ToastLevel::Info, "first");
        list.Push(ToastLevel::Info, "second");
        list.Advance(ToastList::kLifeSeconds - kHalfFadeOut);

        list.Push(ToastLevel::Info, "first");

        ASSERT_EQ(list.Size(), 2u);
        EXPECT_EQ(TextOf(list, 1), "first");
        EXPECT_EQ(list.At(1).count, 2u);
        EXPECT_EQ(ToastList::GetAlpha(list.At(1)), 1.0f);

        list.Advance(kHalfFadeOut);
        ASSERT_EQ(list.Size(), 1u) << "only the repeated one outlives the first life";
        EXPECT_EQ(TextOf(list, 0), "first");
    }

    TEST(ToastListTest, ARepeatWhileFadingInDoesNotSkipTheFade) {
        ToastList list;
        list.Push(ToastLevel::Info, "text");
        list.Advance(kHalfFadeIn);
        const float before = ToastList::GetAlpha(list.At(0));

        list.Push(ToastLevel::Info, "text");

        EXPECT_EQ(ToastList::GetAlpha(list.At(0)), before);
    }

    TEST(ToastListTest, TheSameTextOfAnotherLevelIsAnotherToast) {
        ToastList list;
        list.Push(ToastLevel::Info, "text");
        list.Push(ToastLevel::Error, "text");

        EXPECT_EQ(list.Size(), 2u);
    }

    TEST(ToastListTest, AToastFadesInStaysFadesOutAndLeaves) {
        ToastList list;
        list.Push(ToastLevel::Success, "text");
        EXPECT_EQ(ToastList::GetAlpha(list.At(0)), 0.0f);

        list.Advance(kHalfFadeIn);
        EXPECT_NEAR(ToastList::GetAlpha(list.At(0)), 0.5f, kTolerance);

        list.Advance(ToastList::kFadeInSeconds);
        EXPECT_EQ(ToastList::GetAlpha(list.At(0)), 1.0f);

        list.Advance(ToastList::kLifeSeconds - kHalfFadeOut - list.At(0).age);
        ASSERT_EQ(list.Size(), 1u);
        EXPECT_NEAR(ToastList::GetAlpha(list.At(0)), 0.5f, kTolerance);

        list.Advance(kHalfFadeOut);
        EXPECT_EQ(list.Size(), 0u);
    }

    TEST(ToastListTest, TextsSeenBeforeAreTakenAgedAndDroppedWithNoAllocation) {
        // Longer than what a string keeps inside itself
        constexpr StringView kLongText = "a notification whose text does not fit inside the string object itself";
        ToastList list;
        for (size_t i = 0; i < ToastList::kCapacity; ++i) {
            list.Push(ToastLevel::Info, "{} {}"_format(kLongText, i));
        }
        list.Advance(ToastList::kLifeSeconds);

        ExpectNoAllocations([&] {
            list.Push(ToastLevel::Info, kLongText);
            list.Push(ToastLevel::Error, kLongText);
            list.Push(ToastLevel::Info, kLongText);
            list.Advance(ToastList::kLifeSeconds);
        });
    }

    /// A feed over the journal of a control service of its own
    class ToastFeedTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            service.RegisterHandler("act", [](ControlCall& call) { call.ReplyEmpty(); });
        }

        void ExpectOneToast(ToastLevel level, StringView text) {
            const ToastList& list = feed.GetList();
            ASSERT_EQ(list.Size(), 1u);
            EXPECT_EQ(list.At(0).level, level);
            EXPECT_EQ(TextOf(list, 0), text);
        }

        ControlService service;
        ToastFeed feed;
    };

    TEST_F(ToastFeedTest, ASaveOfTheSceneAndOfAnotherConfigAreToasted) {
        feed.Connect(service.Activity());

        ConfigEvents::ConfigChangedEvent::Emit(ConfigNames::scene, ConfigEvents::ConfigChangeCause::Saved);
        ConfigEvents::ConfigChangedEvent::Emit(kSomeConfig, ConfigEvents::ConfigChangeCause::Saved);

        ASSERT_EQ(feed.GetList().Size(), 2u);
        EXPECT_EQ(TextOf(feed.GetList(), 0), "Scene saved");
        EXPECT_EQ(TextOf(feed.GetList(), 1), "Saved ToastedConfig");
        EXPECT_EQ(feed.GetList().At(1).level, ToastLevel::Success);
    }

    TEST_F(ToastFeedTest, AReloadIsToasted) {
        feed.Connect(service.Activity());

        ConfigEvents::ConfigChangedEvent::Emit(kSomeConfig, ConfigEvents::ConfigChangeCause::Reloaded);

        ExpectOneToast(ToastLevel::Info, "Config reloaded: ToastedConfig");
    }

    TEST_F(ToastFeedTest, AFailedReloadIsAnError) {
        feed.Connect(service.Activity());

        ConfigEvents::ConfigReloadFailedEvent::Emit(kSomeConfig, String("not well-formed"));

        ExpectOneToast(ToastLevel::Error, "Config reload failed: ToastedConfig");
    }

    TEST_F(ToastFeedTest, AnEditInPlaceIsNotToasted) {
        feed.Connect(service.Activity());

        ConfigEvents::ConfigChangedEvent::Emit(kSomeConfig);

        EXPECT_EQ(feed.GetList().Size(), 0u);
    }

    TEST_F(ToastFeedTest, AnEditorToastIsShownAsItIs) {
        feed.Connect(service.Activity());

        EditorToastEvent::Emit(ToastLevel::Warning, "{} is not a format string");

        ExpectOneToast(ToastLevel::Warning, "{} is not a format string");
    }

    TEST_F(ToastFeedTest, AConnectionAndItsEndAreToastedOnce) {
        feed.Connect(service.Activity());

        const IntrusivePtr<ControlClient> client = service.OpenConnectionClient();
        service.Drain();
        feed.Update(kFrameSeconds);
        feed.Update(kFrameSeconds);

        ExpectOneToast(ToastLevel::Info, "Control API client connected");
        EXPECT_EQ(feed.GetList().At(0).count, 1u);

        service.CloseConnectionClient(client);
        service.Drain();
        feed.Update(kFrameSeconds);

        ASSERT_EQ(feed.GetList().Size(), 2u);
        EXPECT_EQ(TextOf(feed.GetList(), 1), "Control API client disconnected");
    }

    TEST_F(ToastFeedTest, WhatTheJournalHeldBeforeTheFeedIsNotToasted) {
        const IntrusivePtr<ControlClient> early = service.OpenConnectionClient();
        service.Drain();

        feed.Connect(service.Activity());
        service.Post(R"({"id": 1, "method": "act"})", [](const String&) {}, early);
        service.Drain();
        feed.Update(kFrameSeconds);

        EXPECT_EQ(feed.GetList().Size(), 0u);
    }

    TEST_F(ToastFeedTest, ALocalClientAndACallOfAConnectionAreNotToasted) {
        feed.Connect(service.Activity());

        const IntrusivePtr<ControlClient> local = service.OpenLocalClient("console");
        service.Post(R"({"id": 1, "method": "act"})", [](const String&) {}, local);
        service.Drain();
        feed.Update(kFrameSeconds);

        ASSERT_GT(service.Activity().Size(), 0u);
        EXPECT_EQ(feed.GetList().Size(), 0u);
    }

    TEST_F(ToastFeedTest, AFrameWithNothingNewAllocatesNothing) {
        feed.Connect(service.Activity());
        const IntrusivePtr<ControlClient> client = service.OpenConnectionClient();
        service.Drain();
        feed.Update(kFrameSeconds);

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                feed.Update(kFrameSeconds);
            }
        });
        ASSERT_EQ(feed.GetList().Size(), 1u) << "a standing toast";

        feed.Update(ToastList::kLifeSeconds);
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                feed.Update(kFrameSeconds);
            }
        });
        EXPECT_EQ(feed.GetList().Size(), 0u);
    }

}  // namespace BECore::Tests
