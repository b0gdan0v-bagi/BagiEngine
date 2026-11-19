#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Types/Types.h>
#include <filesystem>

namespace BECore::Tests {

    /// The sound files the audio tests mount or decode, read from the source tree
    inline std::filesystem::path AudioFixtureFolder() {
        return std::filesystem::path(__FILE__).parent_path() / "AudioData";
    }

    /// A short decoded clip under @p path, for a test of what plays it rather than of what it sounds like
    inline IntrusivePtr<AudioClip> MakeTestClip(PoolString path = "test/clip.wav"_intern) {
        constexpr uint32_t kSampleRate = 22050;
        constexpr size_t kSamples = 16;
        constexpr float kLevel = 0.5f;
        DecodedAudio audio;
        audio.format = AudioFormat{kSampleRate, 1};
        audio.samples.assign(kSamples, kLevel);
        return AudioClip::Create(path, std::move(audio));
    }

    /// The newest record of what @p manager was asked to play; a default record when there is none
    inline AudioPlayRecord LastRecord(const AudioManager& manager) {
        Vector<AudioPlayRecord> records;
        manager.GetRecentPlays(records);
        return records.empty() ? AudioPlayRecord{} : records.back();
    }

}  // namespace BECore::Tests
