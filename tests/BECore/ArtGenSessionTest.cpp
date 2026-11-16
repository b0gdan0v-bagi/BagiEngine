#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kFrameGuard = 100;

        Json Object() {
            return Json::object();
        }

        Json Preset(StringView name, StringView error = {}) {
            Json item = Json::object();
            item["name"] = ToStdString(name);
            item["title"] = ToStdString(name);
            if (!error.empty()) {
                item["error"] = ToStdString(error);
            }
            return item;
        }

        Json ReadyPoll() {
            Json params = Json::object();
            params["state"] = "ready";
            params["generator"] = "fake";
            params["presets"] = Json::array({Preset("wood"), Preset("broken", "no workflow 'x'")});
            return params;
        }

        Json Report(uint32_t id) {
            Json params = Json::object();
            params["id"] = id;
            return params;
        }

    }  // namespace

    class ArtGenSessionTest : public ::testing::Test {
    protected:
        ArtGenSessionTest() : session([this] { return now; }) {}

        /// One control call through the service, as the bridge makes it; the parsed response
        Json Call(StringView method, const Json& params = Json::object()) {
            Json request = Json::object();
            request["id"] = ++_nextId;
            request["method"] = ToStdString(method);
            request["params"] = params;
            Optional<Json> response;
            service.Post(DumpJson(request), [&response](String reply) {
                auto parsed = ParseJson(reply);
                response = parsed ? std::move(*parsed) : Json();
            });
            for (int frame = 0; !response && frame < kFrameGuard; ++frame) {
                service.Drain();
                service.EndFrame();
            }
            return response ? std::move(*response) : Json();
        }

        static const Json& Result(const Json& response) {
            static const Json kNull;
            const Json* result = FindJsonMember(response, "result");
            return result != nullptr ? *result : kNull;
        }

        static String ErrorCode(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            return error != nullptr ? GetJsonString(*error, "code").value_or(String()) : String();
        }

        void SetUp() override {
            session.SetGameRoot("D:/Games/Chess");
            session.RegisterControl(service);
        }

        double now = 100.0;
        ControlService service;
        ArtGenSession session;

    private:
        int64_t _nextId = 0;
    };

    TEST_F(ArtGenSessionTest, APollReportsTheServiceAndHandsTheRequestOverOnce) {
        EXPECT_FALSE(session.IsServiceConnected());
        Json poll = Call("artgen_poll", ReadyPoll());
        EXPECT_EQ(GetJsonString(Result(poll), "gameRoot"), Optional<String>("D:/Games/Chess"));
        EXPECT_EQ(FindJsonMember(Result(poll), "request"), nullptr);
        EXPECT_TRUE(session.IsServiceConnected());
        ASSERT_EQ(session.GetServiceStatus().presets.size(), 2u);

        Json params = Object();
        params["preset"] = "wood";
        params["subject"] = "white knight";
        params["count"] = 2;
        params["seed"] = 7;
        Json generated = Call("artgen_generate", params);
        const auto id = GetJsonInt(Result(generated), "id");
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Queued);

        poll = Call("artgen_poll", ReadyPoll());
        const Json* request = FindJsonMember(Result(poll), "request");
        ASSERT_NE(request, nullptr);
        EXPECT_EQ(GetJsonInt(*request, "id"), id);
        EXPECT_EQ(GetJsonString(*request, "preset"), Optional<String>("wood"));
        EXPECT_EQ(GetJsonString(*request, "subject"), Optional<String>("white knight"));
        EXPECT_EQ(GetJsonInt(*request, "count"), Optional<int64_t>(2));
        EXPECT_EQ(GetJsonInt(*request, "seed"), Optional<int64_t>(7));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Running);

        poll = Call("artgen_poll", ReadyPoll());
        EXPECT_EQ(FindJsonMember(Result(poll), "request"), nullptr);
    }

    TEST_F(ArtGenSessionTest, ProgressImagesAndDoneFillTheRun) {
        const uint32_t id = session.Request("wood", "rook", 2, Optional<int64_t>()).value();
        Call("artgen_poll", ReadyPoll());
        const Json request = Result(Call("artgen_poll", ReadyPoll()));
        EXPECT_EQ(FindJsonMember(request, "request"), nullptr);

        Json progress = Report(id);
        progress["image"] = 0;
        progress["step"] = 5;
        progress["steps"] = 25;
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_progress", progress)), "stale"), Optional<bool>(false));
        EXPECT_EQ(session.GetRun().step, 5);
        EXPECT_EQ(session.GetRun().steps, 25);

        for (int64_t seed : {11, 12}) {
            Json image = Report(id);
            image["seed"] = seed;
            image["path"] = Format("D:/Games/Chess/artgen/out/rook_{}.png", seed).c_str();
            image["provenance"] = Format("D:/Games/Chess/artgen/out/rook_{}.artgen.json", seed).c_str();
            Call("artgen_image", image);
        }
        ASSERT_EQ(session.GetRun().variants.size(), 2u);
        EXPECT_EQ(session.GetRun().variants[1].seed, 12);
        EXPECT_EQ(session.GetRun().variants[1].path, "D:/Games/Chess/artgen/out/rook_12.png");

        Json done = Report(id);
        done["status"] = "ok";
        Call("artgen_done", done);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Done);
        EXPECT_FALSE(session.IsBusy());

        const Json state = Result(Call("artgen_state"));
        const Json* run = FindJsonMember(state, "run");
        ASSERT_NE(run, nullptr);
        EXPECT_EQ(GetJsonString(*run, "state"), Optional<String>("Done"));
        EXPECT_EQ(FindJsonMember(*run, "variants")->size(), 2u);
    }

    TEST_F(ArtGenSessionTest, AFailureKeepsItsReason) {
        const uint32_t id = session.Request("wood", "rook", 1, 3).value();
        Call("artgen_poll", ReadyPoll());
        Json done = Report(id);
        done["status"] = "failed";
        done["error"] = "ComfyUI failed: out of memory";
        Call("artgen_done", done);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Failed);
        EXPECT_EQ(session.GetRun().error, "ComfyUI failed: out of memory");
    }

    TEST_F(ArtGenSessionTest, ReportsAboutAnOlderRunAreStale) {
        const uint32_t first = session.Request("wood", "a", 1, 1).value();
        Call("artgen_poll", ReadyPoll());
        Json done = Report(first);
        done["status"] = "cancelled";
        Call("artgen_done", done);
        const uint32_t second = session.Request("wood", "b", 1, 1).value();
        EXPECT_NE(first, second);
        Call("artgen_poll", ReadyPoll());

        Json late = Report(first);
        late["seed"] = 1;
        late["path"] = "x.png";
        EXPECT_EQ(GetJsonBool(Result(Call("artgen_image", late)), "stale"), Optional<bool>(true));
        EXPECT_TRUE(session.GetRun().variants.empty());
        EXPECT_EQ(session.GetRun().subject, "b");
    }

    TEST_F(ArtGenSessionTest, CancelBeforeTheServiceTookItNeedsNoService) {
        session.Request("wood", "a", 1, 1).value();
        Call("artgen_cancel");
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);
        EXPECT_EQ(FindJsonMember(Result(Call("artgen_poll", ReadyPoll())), "request"), nullptr);
    }

    TEST_F(ArtGenSessionTest, CancelOfARunningRequestGoesToTheServiceOnTheNextPoll) {
        const uint32_t id = session.Request("wood", "a", 3, 1).value();
        Call("artgen_poll", ReadyPoll());
        session.Cancel();
        EXPECT_TRUE(session.GetRun().cancelRequested);
        EXPECT_EQ(GetJsonInt(Result(Call("artgen_poll", ReadyPoll())), "cancel"), Optional<int64_t>(id));
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Running);  // until the service says it stopped
        Json done = Report(id);
        done["status"] = "cancelled";
        Call("artgen_done", done);
        EXPECT_EQ(session.GetRun().state, ArtGenRunState::Cancelled);
        EXPECT_EQ(ErrorCode(Call("artgen_cancel")), "artgen_no_run");
    }

    TEST_F(ArtGenSessionTest, RequestsThatCannotRunAreRefused) {
        Call("artgen_poll", ReadyPoll());
        EXPECT_FALSE(session.Request("wood", "a", 0, 1).has_value());
        EXPECT_FALSE(session.Request("wood", "a", ArtGenSession::kMaxCount + 1, 1).has_value());
        EXPECT_FALSE(session.Request("wood", "a", 1, -1).has_value());
        EXPECT_FALSE(session.Request("", "a", 1, 1).has_value());
        EXPECT_FALSE(session.Request("broken", "a", 1, 1).has_value());

        Json params = Object();
        params["preset"] = "wood";
        EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "invalid_params");
        params["subject"] = "a";
        params["count"] = "four";
        EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "invalid_params");
        params.erase("count");
        EXPECT_TRUE(GetJsonInt(Result(Call("artgen_generate", params)), "id").has_value());
        EXPECT_EQ(session.GetRun().count, 4);
        EXPECT_EQ(ErrorCode(Call("artgen_generate", params)), "artgen_busy");
    }

    TEST_F(ArtGenSessionTest, TheServiceIsGoneWhenItStopsPolling) {
        Call("artgen_poll", ReadyPoll());
        now += ArtGenSession::kServiceTimeoutSeconds - 0.5;
        EXPECT_TRUE(session.IsServiceConnected());
        now += 1.0;
        EXPECT_FALSE(session.IsServiceConnected());
    }

    TEST_F(ArtGenSessionTest, ANotReadyGeneratorIsShownWithItsReason) {
        Json params = Object();
        params["state"] = "not_running";
        params["reason"] = "ComfyUI is installed in D:/ComfyUI but not started";
        const uint64_t before = session.Version();
        Call("artgen_poll", params);
        EXPECT_GT(session.Version(), before);
        EXPECT_EQ(session.GetServiceStatus().state, "not_running");
        EXPECT_EQ(session.GetServiceStatus().reason, "ComfyUI is installed in D:/ComfyUI but not started");
        EXPECT_TRUE(session.GetServiceStatus().presets.empty());
    }

    TEST_F(ArtGenSessionTest, ASecondRegistrationOnTheSameServiceChangesNothing) {
        session.RegisterControl(service);
        Call("artgen_poll", ReadyPoll());
        EXPECT_EQ(session.PollCount(), 1u);
        EXPECT_NE(FindJsonMember(Result(Call("artgen_state")), "service"), nullptr);
    }

}  // namespace BECore::Tests
