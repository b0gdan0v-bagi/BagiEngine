#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioControl.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/FakeAudioBackend.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String MakeRequest(int64_t id, StringView method, const Json& params = Json()) {
            Json request = Json::object();
            request["id"] = id;
            request["method"] = ToStdString(method);
            if (!params.is_null()) {
                request["params"] = params;
            }
            return DumpJson(request);
        }

        IntrusivePtr<AudioClip> MakeClip(const char* path) {
            DecodedAudio audio;
            audio.format = AudioFormat{22050, 1};
            audio.samples.assign(16, 0.5f);
            return AudioClip::Create(PoolString::Intern(path), std::move(audio));
        }

        double NumberOf(const Json* object, StringView key) {
            const auto value = object != nullptr ? GetJsonNumber(*object, key) : Optional<double>();
            return value ? *value : -1.0;
        }

        Optional<bool> BoolOf(const Json* object, StringView key) {
            return object != nullptr ? GetJsonBool(*object, key) : Optional<bool>();
        }

        String StringOf(const Json* object, StringView key) {
            const auto value = object != nullptr ? GetJsonString(*object, key) : Optional<String>();
            return value ? *value : String();
        }

    }  // namespace

    /// A ControlService and an AudioManager of its own over a fake backend: no transport, no engine services
    class AudioControlTest : public ::testing::Test {
    protected:
        void SetUp() override {
            AudioSettings settings;
            settings._voices = 2;
            manager.Initialize(settings);
            RegisterAudioControl(service, manager);
        }

        /// Sends one request and runs it; the answer's `result`, or null with the error code in @c lastError
        const Json* Call(StringView method, const Json& params = Json()) {
            responses.clear();
            service.Post(MakeRequest(1, method, params), [this](String reply) {
                auto parsed = ParseJson(reply);
                responses.push_back(parsed ? std::move(*parsed) : Json());
            });
            service.Drain();
            lastError = responses.empty() ? String("no answer") : StringOf(FindJsonMember(responses.front(), "error"), "code");
            return responses.empty() ? nullptr : FindJsonMember(responses.front(), "result");
        }

        AudioManager manager;
        ControlService service;
        Vector<Json> responses;
        String lastError;
    };

    TEST_F(AudioControlTest, TheStateWithoutABackendStillShowsTheRequests) {
        manager.Play(*MakeClip("sfx/move.wav"), AudioBus::Sfx, 0.5f);

        const Json* state = Call("audio_state");
        ASSERT_NE(state, nullptr) << lastError.c_str();
        const Json* backend = FindJsonMember(*state, "backend");
        ASSERT_NE(backend, nullptr);
        EXPECT_TRUE(backend->is_null());
        EXPECT_EQ(NumberOf(state, "voices"), 0.0);
        EXPECT_EQ(NumberOf(state, "started"), 0.0);
        EXPECT_EQ(NumberOf(state, "requested"), 1.0);

        const Json* recent = FindJsonMember(*state, "recent");
        ASSERT_NE(recent, nullptr);
        ASSERT_TRUE(recent->is_array());
        ASSERT_EQ(recent->size(), 1u);
        const Json& first = (*recent)[0];
        EXPECT_EQ(NumberOf(&first, "index"), 1.0);
        EXPECT_EQ(StringOf(&first, "clip"), "sfx/move.wav");
        EXPECT_EQ(StringOf(&first, "bus"), "sfx");
        EXPECT_EQ(NumberOf(&first, "volume"), 0.5);
        EXPECT_EQ(BoolOf(&first, "played"), Optional<bool>(false));
    }

    TEST_F(AudioControlTest, TheStateShowsTheBackendTheLevelsAndWhatPlays) {
        auto backend = New<FakeAudioBackend>();
        manager.SetBackend(backend);
        manager.SetBusMuted(AudioBus::Music, true);
        manager.Play(*MakeClip("ui/click.wav"), AudioBus::Ui);

        const Json* state = Call("audio_state");
        ASSERT_NE(state, nullptr) << lastError.c_str();
        EXPECT_EQ(StringOf(state, "backend"), "IAudioBackend") << "the fake has no reflection of its own";
        EXPECT_EQ(NumberOf(state, "voices"), 2.0);
        EXPECT_EQ(NumberOf(state, "playing"), 1.0);
        EXPECT_EQ(NumberOf(state, "started"), 1.0);

        const Json* levels = FindJsonMember(*state, "levels");
        ASSERT_NE(levels, nullptr);
        for (const char* key : {"master", "sfx", "music", "ui"}) {
            const Json* level = FindJsonMember(*levels, key);
            ASSERT_NE(level, nullptr) << key;
            EXPECT_EQ(NumberOf(level, "volume"), 1.0) << key;
            EXPECT_EQ(BoolOf(level, "muted"), Optional<bool>(StringView(key) == "music")) << key;
        }
        const Json* recent = FindJsonMember(*state, "recent");
        ASSERT_NE(recent, nullptr);
        ASSERT_EQ(recent->size(), 1u);
        EXPECT_EQ(StringOf(&(*recent)[0], "bus"), "ui");
        EXPECT_EQ(BoolOf(&(*recent)[0], "played"), Optional<bool>(true));
    }

    TEST_F(AudioControlTest, SetChangesTheLevelOfABusOrTheMaster) {
        Json params = Json::object();
        params["bus"] = "music";
        params["volume"] = 0.25;
        const Json* levels = Call("audio_set", params);
        ASSERT_NE(levels, nullptr) << lastError.c_str();
        EXPECT_EQ(NumberOf(FindJsonMember(*levels, "music"), "volume"), 0.25);
        EXPECT_FLOAT_EQ(manager.GetSettings()._music._volume, 0.25f);

        params = Json::object();
        params["bus"] = "master";
        params["muted"] = true;
        params["volume"] = 0.5;
        ASSERT_NE(Call("audio_set", params), nullptr) << lastError.c_str();
        EXPECT_TRUE(manager.GetSettings()._master._muted);
        EXPECT_FLOAT_EQ(manager.GetSettings()._master._volume, 0.5f);
    }

    TEST_F(AudioControlTest, ABadSetIsRefusedAndChangesNothing) {
        const auto refused = [this](Json params, const char* why) {
            const uint64_t revision = manager.GetLevelsRevision();
            EXPECT_EQ(Call("audio_set", params), nullptr) << why;
            EXPECT_EQ(lastError, "invalid_params") << why;
            EXPECT_EQ(manager.GetLevelsRevision(), revision) << why;
        };
        Json params = Json::object();
        refused(params, "no bus");
        params["bus"] = "voice";
        params["volume"] = 0.5;
        refused(params, "an unknown bus");
        params["bus"] = "sfx";
        params["volume"] = "loud";
        refused(params, "a volume that is not a number");
        params["volume"] = 0.5;
        params["muted"] = 1;
        refused(params, "a mute that is not a bool");
        params = Json::object();
        params["bus"] = "sfx";
        refused(params, "neither field");
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
