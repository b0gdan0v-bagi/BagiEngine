#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerControl.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>
#include <vector>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kNanosecondsPerMs = 1000000;

        /// A clock the test moves by hand
        uint64_t g_fakeNow = 0;
        uint64_t FakeNow() {
            return g_fakeNow;
        }
        void AdvanceMs(uint64_t ms) {
            g_fakeNow += ms * kNanosecondsPerMs;
        }

        IntrusivePtr<ITexture> MakeTexture(RecordingRenderer& renderer) {
            const uint8_t pixels[4] = {255, 255, 255, 255};
            return renderer.CreateTexture(pixels, 1, 1);
        }

        // Three quads and two sprites (the sprites get their textures from the test)
        constexpr const char* kCountedSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Counted"><nodes>
    <node name="Quad1"><components><component type="TransformComponent" x="0" y="0" width="10" height="10"/><component type="QuadRendererComponent"/></components></node>
    <node name="Quad2"><components><component type="TransformComponent" x="20" y="0" width="10" height="10"/><component type="QuadRendererComponent"/></components></node>
    <node name="Quad3"><components><component type="TransformComponent" x="40" y="0" width="10" height="10"/><component type="QuadRendererComponent"/></components></node>
    <node name="SpriteA"><components><component type="TransformComponent" x="0" y="20" width="10" height="10"/><component type="SpriteRendererComponent"/></components></node>
    <node name="SpriteB"><components><component type="TransformComponent" x="20" y="20" width="10" height="10"/><component type="SpriteRendererComponent"/></components></node>
  </nodes></scene>
</scenes></SceneManager>)";

    }  // namespace

    // ---- Draw counters ----

    TEST(RenderCountersTest, CountsDrawsTrianglesAndTextureSwitches) {
        auto renderer = New<RecordingRenderer>();
        const IntrusivePtr<ITexture> a = MakeTexture(*renderer);
        const IntrusivePtr<ITexture> b = MakeTexture(*renderer);
        renderer->DrawFilledRect(0, 0, 1, 1, Color{});
        renderer->DrawFilledRect(0, 0, 1, 1, Color{});
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        renderer->DrawTexture(*b, nullptr, 0, 0, 1, 1);

        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.drawCalls, 5u);
        EXPECT_EQ(counters.triangles, 10u);
        EXPECT_EQ(counters.textureSwitches, 2u);
    }

    TEST(RenderCountersTest, GeometryCountsItsWholeTriangles) {
        auto renderer = New<RecordingRenderer>();
        const GeometryVertex vertices[3] = {};
        const int indices[7] = {0, 1, 2, 0, 1, 2, 0};
        renderer->DrawGeometry(nullptr, vertices, indices, 0.0f, 0.0f);
        const RenderCounters counters = renderer->TakeRenderCounters();
        EXPECT_EQ(counters.drawCalls, 1u);
        EXPECT_EQ(counters.triangles, 2u);
        EXPECT_EQ(counters.textureSwitches, 0u);
    }

    TEST(RenderCountersTest, APauseHidesDrawsAndTakingStartsAfresh) {
        auto renderer = New<RecordingRenderer>();
        const IntrusivePtr<ITexture> a = MakeTexture(*renderer);
        {
            const ScopedRenderCountersPause pause(*renderer);
            renderer->DrawFilledRect(0, 0, 1, 1, Color{});
        }
        EXPECT_EQ(renderer->PeekRenderCounters().drawCalls, 0u);

        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        EXPECT_EQ(renderer->TakeRenderCounters().textureSwitches, 1u);
        EXPECT_EQ(renderer->PeekRenderCounters().drawCalls, 0u);
        // A new frame: the same texture is a switch again
        renderer->DrawTexture(*a, nullptr, 0, 0, 1, 1);
        EXPECT_EQ(renderer->TakeRenderCounters().textureSwitches, 1u);
    }

    class SceneDrawCountTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kCountedSceneXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<SpriteRendererComponent> Sprite(const char* nodeName) const {
            Scene& scene = *_manager->GetSceneByName("Counted"_intern);
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<SpriteRendererComponent>();
                }
            }
            return {};
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SceneDrawCountTest, AKnownSceneMakesTheExpectedDraws) {
        ASSERT_TRUE(Sprite("SpriteA"));
        Sprite("SpriteA")->SetTexture(MakeTexture(*_renderer));
        Sprite("SpriteB")->SetTexture(MakeTexture(*_renderer));
        _manager->SetActiveScene("Counted"_intern);
        _renderer->TakeRenderCounters();

        _manager->DrawAll();

        const RenderCounters counters = _renderer->TakeRenderCounters();
        EXPECT_EQ(counters.drawCalls, 5u);
        EXPECT_EQ(counters.triangles, 10u);
        EXPECT_EQ(counters.textureSwitches, 2u);
    }

    // ---- The collector ----

    TEST(FrameStatsCollectorTest, TimesTheStagesOfAFrame) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        g_fakeNow = 0;

        collector.BeginFrame();
        collector.BeginStage(FrameStage::Update);
        AdvanceMs(1);
        collector.BeginStage(FrameStage::SceneUpdate);
        AdvanceMs(2);
        collector.EndStage(FrameStage::SceneUpdate);
        collector.EndStage(FrameStage::Update);
        collector.BeginStage(FrameStage::Render);
        AdvanceMs(4);
        collector.EndStage(FrameStage::Render);
        collector.SetCounter("Things"_intern, 3.0);
        collector.SetCounter("Things"_intern, 5.0);
        collector.EndFrame(RenderCounters{.drawCalls = 7, .triangles = 14, .textureSwitches = 1}, AllocTotals{});

        const FrameStats* last = collector.GetLast();
        ASSERT_NE(last, nullptr);
        EXPECT_EQ(last->frame, 1u);
        EXPECT_DOUBLE_EQ(last->cpuMs, 7.0);
        EXPECT_DOUBLE_EQ(last->frameMs, 7.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::Update), 3.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::SceneUpdate), 2.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::Render), 4.0);
        EXPECT_DOUBLE_EQ(last->GetStageMs(FrameStage::Present), 0.0);
        EXPECT_EQ(last->draws.drawCalls, 7u);
        EXPECT_EQ(last->FindCounter("Things"_intern), 5.0);
        EXPECT_FALSE(last->FindCounter("Other"_intern).has_value());

        // The second frame: frameMs runs from the end of the first
        AdvanceMs(10);
        collector.BeginFrame();
        AdvanceMs(6);
        collector.EndFrame({}, AllocTotals{});
        EXPECT_DOUBLE_EQ(collector.GetLast()->cpuMs, 6.0);
        EXPECT_DOUBLE_EQ(collector.GetLast()->frameMs, 16.0);
        EXPECT_TRUE(collector.GetLast()->counters.empty());
    }

    TEST(FrameStatsCollectorTest, TheAllocationsOfAFrameAreTheDifference) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        collector.BeginFrame();
        const AllocTotals atBegin = AllocTracker::GetTotals();
        AllocTotals atEnd = atBegin;
        atEnd.count += 3;
        atEnd.bytes += 300;
        atEnd.liveBytes = 1234;
        collector.EndFrame({}, atEnd);
        EXPECT_EQ(collector.GetLast()->allocCount, 3u);
        EXPECT_EQ(collector.GetLast()->allocBytes, 300u);
        EXPECT_EQ(collector.GetLast()->liveBytes, 1234u);
    }

    TEST(FrameStatsCollectorTest, TheHistoryKeepsTheLastFramesOldestFirst) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        EXPECT_EQ(collector.GetLast(), nullptr);
        EXPECT_TRUE(collector.GetHistory(5).empty());

        const size_t frames = FrameStatsCollector::kFrameHistory + 10;
        for (size_t i = 0; i < frames; ++i) {
            collector.BeginFrame();
            collector.EndFrame(RenderCounters{.drawCalls = static_cast<uint32_t>(i)}, AllocTotals{});
        }
        const Vector<FrameStats> lastThree = collector.GetHistory(3);
        ASSERT_EQ(lastThree.size(), 3u);
        EXPECT_EQ(lastThree[0].frame, frames - 2);
        EXPECT_EQ(lastThree[2].frame, frames);
        EXPECT_EQ(lastThree[2].draws.drawCalls, frames - 1);
        EXPECT_EQ(collector.GetHistory(10000).size(), FrameStatsCollector::kFrameHistory);
        EXPECT_EQ(collector.GetHistory(10000).front().frame, frames - FrameStatsCollector::kFrameHistory + 1);
    }

    TEST(FrameStatsCollectorTest, AHeldFrameIsMarkedPausedAndTheHistoryKeepsIt) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);

        collector.BeginFrame();
        collector.EndFrame({}, AllocTotals{});
        EXPECT_FALSE(collector.GetLast()->paused) << "a frame is a game frame by default";

        collector.BeginFrame(true);
        collector.EndFrame({}, AllocTotals{});
        EXPECT_TRUE(collector.GetLast()->paused);

        collector.BeginFrame(false);  // a stepped frame is a game frame
        collector.EndFrame({}, AllocTotals{});
        EXPECT_FALSE(collector.GetLast()->paused) << "the flag of one frame does not leak into the next";

        const Vector<FrameStats> history = collector.GetHistory(3);
        ASSERT_EQ(history.size(), 3u);
        EXPECT_FALSE(history[0].paused);
        EXPECT_TRUE(history[1].paused);
        EXPECT_FALSE(history[2].paused);
    }

    TEST(FrameStatsCollectorTest, StagesAndCountersOutsideAFrameAreIgnored) {
        FrameStatsCollector collector;
        collector.SetClock(FakeNow);
        collector.BeginStage(FrameStage::Update);
        collector.EndStage(FrameStage::Update);
        collector.SetCounter("Lost"_intern, 1.0);
        collector.EndFrame({}, AllocTotals{});
        EXPECT_EQ(collector.GetFrameCount(), 0u);
    }

    // ---- The control methods ----

    class ProfilerControlTest : public ::testing::Test {
    protected:
        void SetUp() override {
            RegisterProfilerControl(service);
        }

        /// Sends one request and runs the main-thread side; the response
        Json Call(StringView method, const Json& params = Json::object()) {
            Json request = Json::object();
            request["id"] = 1;
            request["method"] = ToStdString(method);
            request["params"] = params;
            Json response;
            service.Post(DumpJson(request), [&response](String text) {
                auto parsed = ParseJson(text);
                response = parsed ? std::move(*parsed) : Json();
            });
            service.Drain();
            return response;
        }

        static String ErrorCode(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            const auto code = error != nullptr ? GetJsonString(*error, "code") : std::nullopt;
            return code ? String(code->c_str()) : String();
        }

        ControlService service;
    };

    TEST_F(ProfilerControlTest, FrameStatsAnswersTheLastFrames) {
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        for (int i = 0; i < 3; ++i) {
            collector.BeginFrame();
            collector.EndFrame(RenderCounters{.drawCalls = 4, .triangles = 8}, AllocTracker::GetTotals());
        }
        Json params = Json::object();
        params["frames"] = 2;
        const Json response = Call("frame_stats", params);
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        const Json* frames = FindJsonMember(*result, "frames");
        ASSERT_NE(frames, nullptr);
        ASSERT_EQ(frames->size(), 2u);
        const Json& last = (*frames)[1];
        EXPECT_EQ(GetJsonInt(last, "drawCalls"), 4);
        EXPECT_EQ(GetJsonInt(last, "triangles"), 8);
        for (const char* key : {"frame", "paused", "frameMs", "cpuMs", "stagesMs", "textureSwitches", "allocCount", "allocBytes", "liveBytes", "counters"}) {
            EXPECT_NE(FindJsonMember(last, key), nullptr) << key;
        }
        EXPECT_NE(FindJsonMember(*FindJsonMember(last, "stagesMs"), "SceneDraw"), nullptr);
        EXPECT_EQ(GetJsonBool(*result, "allocTracking"), AllocTracker::kEnabled);
    }

    TEST_F(ProfilerControlTest, FrameStatsCarryThePausedFlagOfEveryFrame) {
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.BeginFrame(false);
        collector.EndFrame({}, AllocTracker::GetTotals());
        collector.BeginFrame(true);
        collector.EndFrame({}, AllocTracker::GetTotals());

        Json params = Json::object();
        params["frames"] = 2;
        const Json response = Call("frame_stats", params);
        const Json* frames = FindJsonMember(*FindJsonMember(response, "result"), "frames");
        ASSERT_NE(frames, nullptr);
        ASSERT_EQ(frames->size(), 2u);
        EXPECT_EQ(GetJsonBool((*frames)[0], "paused"), false);
        EXPECT_EQ(GetJsonBool((*frames)[1], "paused"), true);
    }

    TEST_F(ProfilerControlTest, InvalidParamsAreRejected) {
        Json zero = Json::object();
        zero["frames"] = 0;
        EXPECT_EQ(ErrorCode(Call("frame_stats", zero)), "invalid_params");
        Json text = Json::object();
        text["frames"] = "3";
        EXPECT_EQ(ErrorCode(Call("frame_stats", text)), "invalid_params");

        for (const char* path : {"../trace.json", "/tmp/trace.json", ""}) {
            Json params = Json::object();
            params["path"] = path;
            EXPECT_EQ(ErrorCode(Call("profiler_dump", params)), "invalid_params") << path;
        }
        Json top = Json::object();
        top["top"] = 0;
        EXPECT_EQ(ErrorCode(Call("zone_allocs", top)), "invalid_params");
        Json reset = Json::object();
        reset["reset"] = 1;
        EXPECT_EQ(ErrorCode(Call("zone_allocs", reset)), "invalid_params");
    }

    TEST_F(ProfilerControlTest, DumpNeedsTheChromeTraceRecording) {
        Profiler::SetBackend(nullptr);
        EXPECT_EQ(ErrorCode(Call("profiler_dump")), "not_recording");
    }

    TEST_F(ProfilerControlTest, ZoneAllocsAnswersTheTotalsAndTheZones) {
        const Json response = Call("zone_allocs");
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonBool(*result, "enabled"), AllocTracker::kEnabled);
        ASSERT_NE(FindJsonMember(*result, "zones"), nullptr);
        const Json* totals = FindJsonMember(*result, "totals");
        ASSERT_NE(totals, nullptr);
        for (const char* key : {"count", "bytes", "frees", "liveBytes", "peakLiveBytes", "largest", "noAllocViolations"}) {
            EXPECT_NE(FindJsonMember(*totals, key), nullptr) << key;
        }
    }

}  // namespace BECore::Tests
