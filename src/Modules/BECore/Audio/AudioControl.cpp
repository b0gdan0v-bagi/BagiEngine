#include "AudioControl.h"

#include <BECore/Audio/AudioControlAnswers.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Reflection/ClassMeta.h>

namespace BECore {

    namespace {

        /// The request-side name of the master level
        constexpr StringView kMasterKey = "master";

        /// The wire names of the buses
        struct BusNames {
            static constexpr auto sfx = "sfx"_intern;
            static constexpr auto music = "music"_intern;
            static constexpr auto ui = "ui"_intern;
        };

        /// The wire name of a bus: lower case, as the keys of `levels`
        PoolString BusKey(AudioBus bus) {
            // No default: a new bus is a -Wswitch warning here, not a bus that shows as "sfx"
            switch (bus) {
                case AudioBus::Sfx:
                    return BusNames::sfx;
                case AudioBus::Music:
                    return BusNames::music;
                case AudioBus::Ui:
                    return BusNames::ui;
            }
            return BusNames::sfx;
        }

        AudioLevelAnswer LevelAnswer(const AudioLevel& level) {
            AudioLevelAnswer answer;
            answer.muted = level._muted;
            answer.volume = static_cast<double>(level._volume);
            return answer;
        }

        AudioLevelsAnswer LevelsAnswer(const AudioSettings& settings) {
            AudioLevelsAnswer answer;
            answer.master = LevelAnswer(settings._master);
            answer.music = LevelAnswer(settings.LevelOf(AudioBus::Music));
            answer.sfx = LevelAnswer(settings.LevelOf(AudioBus::Sfx));
            answer.ui = LevelAnswer(settings.LevelOf(AudioBus::Ui));
            return answer;
        }

        void HandleState(ControlCall& call, const AudioManager& manager) {
            AudioStateAnswer answer;
            if (const auto& backend = manager.GetBackend()) {
                answer.backend = PoolString::Intern(backend->GetTypeMeta().typeName);
                answer.voices = manager.GetEffectVoiceCount();
            }
            answer.playing = manager.GetPlayingCount();
            answer.started = manager.GetStartedCount();
            answer.requested = manager.GetRequestCount();
            answer.levels = LevelsAnswer(manager.GetSettings());

            const AudioMusicState music = manager.GetMusicState();
            if (!music.clip.Empty()) {
                answer.music.clip = music.clip;
            }
            answer.music.playing = music.playing;
            answer.music.loop = music.loop;
            answer.music.volume = static_cast<double>(music.volume);
            answer.music.fading = music.fading;

            Vector<AudioPlayRecord> records;
            manager.GetRecentPlays(records);
            answer.recent.reserve(records.size());
            for (const AudioPlayRecord& record : records) {
                AudioRecentPlay item;
                item.bus = BusKey(record.bus);
                item.clip = record.clip;
                item.index = record.index;
                item.played = record.played;
                item.volume = static_cast<double>(record.volume);
                answer.recent.push_back(item);
            }
            call.ReplyObject(answer);
        }

        void HandleSet(ControlCall& call, AudioManager& manager) {
            const Json& params = call.Params();
            const auto busName = GetJsonString(params, "bus");
            Optional<AudioBus> bus;
            bool master = false;
            if (busName && StringView(*busName) == kMasterKey) {
                master = true;
            } else if (busName) {
                for (const AudioBus candidate : {AudioBus::Sfx, AudioBus::Music, AudioBus::Ui}) {
                    if (StringView(*busName) == BusKey(candidate).ToStringView()) {
                        bus = candidate;
                    }
                }
            }
            if (!master && !bus) {
                call.Fail(ControlErrors::kInvalidParams, "'bus' has to be \"master\", \"sfx\", \"music\" or \"ui\"");
                return;
            }
            const bool hasVolume = FindJsonMember(params, "volume") != nullptr;
            const bool hasMuted = FindJsonMember(params, "muted") != nullptr;
            const auto volume = GetJsonNumber(params, "volume");
            const auto muted = GetJsonBool(params, "muted");
            if ((hasVolume && !volume) || (hasMuted && !muted)) {
                call.Fail(ControlErrors::kInvalidParams, "'volume' has to be a number and 'muted' a boolean");
                return;
            }
            if (!volume && !muted) {
                call.Fail(ControlErrors::kInvalidParams, "give 'volume', 'muted' or both");
                return;
            }
            if (volume) {
                if (master) {
                    manager.SetMasterVolume(static_cast<float>(*volume));
                } else {
                    manager.SetBusVolume(*bus, static_cast<float>(*volume));
                }
            }
            if (muted) {
                if (master) {
                    manager.SetMasterMuted(*muted);
                } else {
                    manager.SetBusMuted(*bus, *muted);
                }
            }
            call.ReplyObject(LevelsAnswer(manager.GetSettings()));
        }

    }  // namespace

    void RegisterAudioControl(ControlService& service, AudioManager& manager) {
        service.RegisterHandler("audio_state", [&manager](ControlCall& call) { HandleState(call, manager); });
        service.RegisterHandler("audio_set", [&manager](ControlCall& call) { HandleSet(call, manager); });

        service.DescribeMethod("audio_state", {"The audio backend, the levels and the last sounds the game asked for", {}});
        service.DescribeMethod("audio_set", {"Sets the volume or the mute of the master or a bus",
                                             {{"bus", "string", true, "master, sfx, music or ui"},
                                              {"volume", "number", false, "slider position 0 to 1 (the gain is its cube)"},
                                              {"muted", "boolean", false, "true mutes, false unmutes"}}});
    }

}  // namespace BECore
