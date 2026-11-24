#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioControl.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/Control/ControlService.h>
#include <BECore/FakeAudioBackend.h>
#include <Common/ControlTestSupport.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// A ControlService and an AudioManager of its own over a fake backend: no transport, no engine services
    class AudioControlTest : public ::testing::Test {
    protected:
        void SetUp() override {
            AudioSettings settings;
            settings._voices = 2;
            manager.Initialize(settings);
            RegisterAudioControl(service, manager);
        }

        /// Sends one request and runs it; the answer's `result`, or a view that is not valid with the error code in @c lastError
        JsonView Call(StringView method, StringView paramsText = {}) {
            reply = CallControlText(service, 1, method, paramsText);
            lastError = reply.Text().empty() ? String("no answer") : reply.ErrorCode();
            return reply.Result();
        }

        AudioManager manager;
        ControlService service;
        ControlReply reply;
        String lastError;
    };

    TEST_F(AudioControlTest, TheStateWithoutABackendStillShowsTheRequests) {
        manager.Play(*MakeTestClip("sfx/move.wav"_intern), AudioBus::Sfx, 0.5f);

        const JsonView state = Call("audio_state");
        ASSERT_TRUE(state.IsObject()) << lastError.c_str();
        EXPECT_FALSE(state.Find("backend").has_value()) << "no backend: no member";
        EXPECT_EQ(NumberOf(state, "voices"), 0.0);
        EXPECT_EQ(NumberOf(state, "started"), 0.0);
        EXPECT_EQ(NumberOf(state, "requested"), 1.0);

        const JsonView music = MemberOf(state, "music");
        ASSERT_TRUE(music.IsObject());
        EXPECT_FALSE(music.Find("clip").has_value()) << "no music: no member";
        EXPECT_EQ(BoolOf(music, "playing"), Optional<bool>(false));
        EXPECT_EQ(BoolOf(music, "fading"), Optional<bool>(false));

        const JsonView recent = MemberOf(state, "recent");
        ASSERT_TRUE(recent.IsArray());
        ASSERT_EQ(recent.Size(), 1u);
        const JsonView first = ItemAt(recent, 0);
        EXPECT_EQ(NumberOf(first, "index"), 1.0);
        EXPECT_EQ(StringOf(first, "clip"), "sfx/move.wav");
        EXPECT_EQ(StringOf(first, "bus"), "sfx");
        EXPECT_EQ(NumberOf(first, "volume"), 0.5);
        EXPECT_EQ(BoolOf(first, "played"), Optional<bool>(false));
    }

    TEST_F(AudioControlTest, AFloatVolumeIsAnsweredAsTheDoubleItWidensTo) {
        manager.Play(*MakeTestClip("sfx/quiet.wav"_intern), AudioBus::Sfx, 0.1f);

        const JsonView state = Call("audio_state");
        ASSERT_TRUE(state.IsObject()) << lastError.c_str();
        const JsonView recent = MemberOf(state, "recent");
        ASSERT_TRUE(recent.IsArray());
        ASSERT_EQ(recent.Size(), 1u);
        EXPECT_EQ(NumberOf(ItemAt(recent, 0), "volume"), static_cast<double>(0.1f));
        EXPECT_NE(NumberOf(ItemAt(recent, 0), "volume"), 0.1);
    }

    TEST_F(AudioControlTest, TheStateShowsTheBackendTheLevelsAndWhatPlays) {
        auto backend = New<FakeAudioBackend>();
        manager.SetBackend(backend);
        manager.SetBusMuted(AudioBus::Music, true);
        manager.Play(*MakeTestClip("ui/click.wav"_intern), AudioBus::Ui);

        const JsonView state = Call("audio_state");
        ASSERT_TRUE(state.IsObject()) << lastError.c_str();
        EXPECT_EQ(StringOf(state, "backend"), "IAudioBackend") << "the fake has no reflection of its own";
        EXPECT_EQ(NumberOf(state, "voices"), 2.0);
        EXPECT_EQ(NumberOf(state, "playing"), 1.0);
        EXPECT_EQ(NumberOf(state, "started"), 1.0);

        const JsonView levels = MemberOf(state, "levels");
        ASSERT_TRUE(levels.IsObject());
        for (const char* key : {"master", "sfx", "music", "ui"}) {
            const JsonView level = MemberOf(levels, key);
            ASSERT_TRUE(level.IsObject()) << key;
            EXPECT_EQ(NumberOf(level, "volume"), 1.0) << key;
            EXPECT_EQ(BoolOf(level, "muted"), Optional<bool>(StringView(key) == "music")) << key;
        }
        const JsonView recent = MemberOf(state, "recent");
        ASSERT_TRUE(recent.IsArray());
        ASSERT_EQ(recent.Size(), 1u);
        EXPECT_EQ(StringOf(ItemAt(recent, 0), "bus"), "ui");
        EXPECT_EQ(BoolOf(ItemAt(recent, 0), "played"), Optional<bool>(true));
    }

    TEST_F(AudioControlTest, SetChangesTheLevelOfABusOrTheMaster) {
        const JsonView levels = Call("audio_set", R"({"bus":"music","volume":0.25})");
        ASSERT_TRUE(levels.IsObject()) << lastError.c_str();
        EXPECT_EQ(NumberOf(MemberOf(levels, "music"), "volume"), 0.25);
        EXPECT_FLOAT_EQ(manager.GetSettings()._music._volume, 0.25f);

        ASSERT_TRUE(Call("audio_set", R"({"bus":"master","muted":true,"volume":0.5})").IsObject()) << lastError.c_str();
        EXPECT_TRUE(manager.GetSettings()._master._muted);
        EXPECT_FLOAT_EQ(manager.GetSettings()._master._volume, 0.5f);
    }

    TEST_F(AudioControlTest, ABadSetIsRefusedAndChangesNothing) {
        const auto refused = [this](StringView paramsText, const char* why) {
            const uint64_t revision = manager.GetLevelsRevision();
            EXPECT_FALSE(Call("audio_set", paramsText).IsValid()) << why;
            EXPECT_EQ(lastError, "invalid_params") << why;
            EXPECT_EQ(manager.GetLevelsRevision(), revision) << why;
        };
        refused(R"({})", "no bus");
        refused(R"({"bus":"voice","volume":0.5})", "an unknown bus");
        refused(R"({"bus":"sfx","volume":"loud"})", "a volume that is not a number");
        refused(R"({"bus":"sfx","muted":1,"volume":0.5})", "a mute that is not a bool");
        refused(R"({"bus":"sfx"})", "neither field");
    }

    TEST_F(AudioControlTest, BothMethodsAreDescribed) {
        bool state = false;
        bool set = false;
        for (const auto& method : service.GetMethods()) {
            state = state || (method.name == "audio_state" && method.info != nullptr);
            set = set || (method.name == "audio_set" && method.info != nullptr);
        }
        EXPECT_TRUE(state);
        EXPECT_TRUE(set);
    }

}  // namespace BECore::Tests
