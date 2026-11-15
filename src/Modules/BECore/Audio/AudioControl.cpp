#include "AudioControl.h"

#include <BECore/Audio/AudioManager.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Reflection/ClassMeta.h>

namespace BECore {

    namespace {

        constexpr StringView kMasterKey = "master";

        /// The wire name of a bus: lower case, as the keys of `levels`
        StringView BusKey(AudioBus bus) {
            // No default: a new bus is a -Wswitch warning here, not a bus that shows as "sfx"
            switch (bus) {
                case AudioBus::Sfx:
                    return "sfx";
                case AudioBus::Music:
                    return "music";
                case AudioBus::Ui:
                    return "ui";
            }
            return "sfx";
        }

        Json LevelJson(const AudioLevel& level) {
            Json result = Json::object();
            result["volume"] = level._volume;
            result["muted"] = level._muted;
            return result;
        }

        Json LevelsJson(const AudioSettings& settings) {
            Json result = Json::object();
            result[ToStd(kMasterKey)] = LevelJson(settings._master);
            for (const AudioBus bus : {AudioBus::Sfx, AudioBus::Music, AudioBus::Ui}) {
                result[ToStd(BusKey(bus))] = LevelJson(settings.LevelOf(bus));
            }
            return result;
        }

        void HandleState(ControlCall& call, const AudioManager& manager) {
            Json result = Json::object();
            if (const auto& backend = manager.GetBackend()) {
                result["backend"] = MakeJsonString(backend->GetTypeMeta().typeName);
                result["voices"] = backend->GetVoiceCount();
            } else {
                result["backend"] = nullptr;
                result["voices"] = 0;
            }
            result["playing"] = manager.GetPlayingCount();
            result["started"] = manager.GetStartedCount();
            result["requested"] = manager.GetRequestCount();
            result["levels"] = LevelsJson(manager.GetSettings());

            Vector<AudioPlayRecord> records;
            manager.GetRecentPlays(records);
            Json recent = Json::array();
            for (const AudioPlayRecord& record : records) {
                Json item = Json::object();
                item["index"] = record.index;
                item["clip"] = MakeJsonString(record.clip.ToStringView());
                item["bus"] = MakeJsonString(BusKey(record.bus));
                item["volume"] = record.volume;
                item["played"] = record.played;
                recent.push_back(std::move(item));
            }
            result["recent"] = std::move(recent);
            call.Reply(std::move(result));
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
                    if (StringView(*busName) == BusKey(candidate)) {
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
            call.Reply(LevelsJson(manager.GetSettings()));
        }

    }  // namespace

    void RegisterAudioControl(ControlService& service, AudioManager& manager) {
        service.RegisterHandler("audio_state", [&manager](ControlCall& call) { HandleState(call, manager); });
        service.RegisterHandler("audio_set", [&manager](ControlCall& call) { HandleSet(call, manager); });

        service.DescribeMethod("audio_state", {"The audio backend, the levels and the last sounds the game asked for", {}});
        service.DescribeMethod("audio_set", {"Sets the volume or the mute of the master or a bus",
                                             {{"bus", "string", true, "master, sfx, music or ui"}, {"volume", "number", false, "0 to 1"}, {"muted", "boolean", false, "true mutes, false unmutes"}}});
    }

}  // namespace BECore
