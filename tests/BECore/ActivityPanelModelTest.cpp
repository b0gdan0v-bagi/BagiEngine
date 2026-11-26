// The Activity panel's model over a control service of its own: what a row says of an entry of the journal, and Clear.

#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Feedback/ActivityPanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kSteadyFrames = 10;

        class ActivityPanelModelTest : public ::testing::Test {
        protected:
            void SetUp() override {
                RegisterControlBuiltins(service);
                service.RegisterHandler("act", [](ControlCall& call) { call.ReplyEmpty(); });
                service.RegisterHandler("refused", [](ControlCall& call) { call.Fail("not_now", "no"); });
                model.SetService(service);
            }

            void Send(StringView method, StringView params = {}, const IntrusivePtr<ControlClient>& client = {}) {
                service.Post(MakeRequestText(1, method, params), [](const String&) {}, client);
                service.Drain();
            }

            const ActivityRow& Row(size_t index) const {
                return At(model.GetView().rows, index);
            }

            ControlService service;
            ActivityPanelModel model;
        };

    }  // namespace

    TEST_F(ActivityPanelModelTest, TheRowsAreTheJournalNewestFirstWithWhoDidWhat) {
        const IntrusivePtr<ControlClient> client = service.OpenConnectionClient();
        Send("act", R"({"a":1})", client);
        Send("refused", {}, client);
        Send("act");
        model.Update();

        ASSERT_EQ(model.GetView().rows.size(), 4u);
        EXPECT_TRUE(model.GetView().hasRows);
        EXPECT_EQ(Row(0).client, "engine") << "a call with no client";
        EXPECT_EQ(Row(1).method, "refused");
        EXPECT_EQ(Row(1).error, "not_now");
        EXPECT_EQ(Row(2).method, "act");
        EXPECT_EQ(Row(2).params, R"({"a":1})");
        EXPECT_EQ(Row(2).error, "");
        EXPECT_EQ(Row(2).client, "client {}"_format(client->GetId()));
        EXPECT_EQ(Row(3).method, "connected");
        EXPECT_FALSE(Row(3).time.empty());
    }

    TEST_F(ActivityPanelModelTest, AClientThatNamesItselfIsShownByThatNameInItsOlderRowsToo) {
        const IntrusivePtr<ControlClient> client = service.OpenConnectionClient();
        Send("act", {}, client);
        model.Update();
        Send("client_hello", R"({"name":"painter"})", client);
        service.CloseConnectionClient(client);
        service.Drain();
        model.Update();

        ASSERT_EQ(model.GetView().rows.size(), 4u);
        EXPECT_EQ(Row(0).method, "disconnected");
        EXPECT_EQ(Row(1).method, "named");
        EXPECT_EQ(Row(1).params, "painter");
        for (const ActivityRow& row : model.GetView().rows) {
            EXPECT_EQ(row.client, "painter") << row.method.c_str();
        }
    }

    TEST_F(ActivityPanelModelTest, ClearEmptiesTheJournalAndALaterCallIsListedAgain) {
        Send("act");
        model.Update();
        EXPECT_FALSE(model.RunCommand("fileChanges.rescan", {}));
        ASSERT_EQ(model.GetView().rows.size(), 1u);

        const PoolString clear = EditorCommandIds::kActivityClear;
        EXPECT_TRUE(model.RunCommand(clear.ToStringView(), {}));
        EXPECT_EQ(service.Activity().Size(), 0u);
        EXPECT_TRUE(model.GetView().rows.empty());
        EXPECT_FALSE(model.GetView().hasRows);

        Send("refused");
        model.Update();
        ASSERT_EQ(model.GetView().rows.size(), 1u);
        EXPECT_EQ(Row(0).method, "refused");
    }

    TEST_F(ActivityPanelModelTest, AJournalThatStandsStillAllocatesNothing) {
        const IntrusivePtr<ControlClient> client = service.OpenConnectionClient();
        Send("act", {}, client);
        model.Update();

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                model.Update();
            }
        });
    }

}  // namespace BECore::Tests
