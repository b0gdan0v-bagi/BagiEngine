#include <BECore/Audio/AudioManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenPanelModel.h>

namespace BECore {

    namespace {

        constexpr size_t kWaveformBuckets = 110;
        constexpr StringView kPlaceholderName = "<name>";
        constexpr StringView kUseOnSelectedTip = "Put the clip on the AudioSourceComponent of the node selected in the Scene Editor";
        constexpr StringView kSelectANodeTip = "Select a node in the Scene Editor first";

        DecodedAudio DecodedOf(const AudioClip& clip) {
            DecodedAudio audio;
            audio.format = clip.GetFormat();
            audio.samples.assign(clip.GetSamples().begin(), clip.GetSamples().end());
            return audio;
        }

        String RateText(uint32_t rate) {
            return rate == 0 ? String("the file's") : Format("{} Hz", rate);
        }

        int32_t RateIndexOf(uint32_t rate) {
            const auto& rates = ArtGenSoundSettings::kRates;
            for (size_t i = 0; i < rates.size(); ++i) {
                if (rates[i] == rate) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

    }  // namespace

    void ArtGenPanelModel::InitSoundRows() {
        for (const uint32_t rate : ArtGenSoundSettings::kRates) {
            _view.sound.rates.push_back(RateText(rate));
        }
    }

    void ArtGenPanelModel::ApplySoundEdits() {
        const ArtGenSoundView& view = _view.sound;
        ArtGenSoundSettings settings = _soundSettings;
        settings.mono = view.mono;
        if (view.trim != settings.trimDb.has_value()) {
            settings.trimDb = view.trim ? Optional<float>(ArtGenSoundSettings::kDefaultTrimDb) : Optional<float>();
        } else if (view.trim) {
            settings.trimDb = std::clamp(view.trimDb, ArtGenSoundSettings::kMinTrimDb, ArtGenSoundSettings::kMaxTrimDb);
        }
        if (view.normalise != settings.peakDb.has_value()) {
            settings.peakDb = view.normalise ? Optional<float>(ArtGenSoundSettings::kDefaultPeakDb) : Optional<float>();
        } else if (view.normalise) {
            settings.peakDb = std::clamp(view.peakDb, ArtGenSoundSettings::kMinPeakDb, ArtGenSoundSettings::kMaxPeakDb);
        }
        settings.fadeInMs = std::clamp(view.fadeIn, 0.0f, ArtGenSoundSettings::kMaxFadeMs);
        settings.fadeOutMs = std::clamp(view.fadeOut, 0.0f, ArtGenSoundSettings::kMaxFadeMs);
        settings.maxSeconds = std::clamp(view.maxSeconds, 0.0f, ArtGenSoundSettings::kMaxSeconds);
        if (view.rateIndex >= 0 && static_cast<size_t>(view.rateIndex) < ArtGenSoundSettings::kRates.size()) {
            settings.sampleRate = ArtGenSoundSettings::kRates[view.rateIndex];
        }
        if (settings == _soundSettings) {
            return;
        }
        _soundSettings = settings;
        _soundStale = true;
        // What was heard is no longer what Accept writes
        if (_playingRow == kPreviewRow) {
            StopSound();
        }
        _preview.Reset();
        _previewError.clear();
        _soundAcceptStale = true;
    }

    void ArtGenPanelModel::RefreshSoundView() {
        ArtGenSoundView& view = _view.sound;
        const ArtGenSoundSettings& settings = _soundSettings;
        view.mono = settings.mono;
        view.trim = settings.trimDb.has_value();
        if (settings.trimDb) {
            view.trimDb = *settings.trimDb;
        }
        view.normalise = settings.peakDb.has_value();
        if (settings.peakDb) {
            view.peakDb = *settings.peakDb;
        }
        view.fadeIn = settings.fadeInMs;
        view.fadeOut = settings.fadeOutMs;
        view.maxSeconds = settings.maxSeconds;
        view.rateIndex = RateIndexOf(settings.sampleRate);
    }

    const AudioClip* ArtGenPanelModel::PickedClip() const {
        if (!_selectedSeed) {
            return nullptr;
        }
        const ArtGenRun& run = GetSession().GetRun();
        for (size_t i = 0; i < run.variants.size() && i < _sounds.size(); ++i) {
            const SoundRow& row = _sounds[i];
            if (run.variants[i].seed == *_selectedSeed && run.variants[i].IsAudio() && row.clip && row.clip->GetState() == ResourceState::Loaded) {
                return row.clip.Get();
            }
        }
        return nullptr;
    }

    void ArtGenPanelModel::UpdateSoundAccept() {
        ArtGenSoundAcceptView& view = _view.soundAccept;
        const ArtGenVariant* variant = _selectedSeed ? GetSession().FindVariant(*_selectedSeed) : nullptr;
        view.hasPick = variant != nullptr && variant->IsAudio();
        view.canListen = PickedClip() != nullptr;
        view.previewPlaying = _playingRow == kPreviewRow;
        view.clipValid = IsValidClipPath(StringView(view.clipPath));
        view.canAccept = view.replaceClip ? view.clipValid : _view.accept.spriteValid;
        // The tip names the clip path while one is replaced, so it follows the typed path
        if (_soundAcceptStale || view.replaceClip != _tipReplaceClip || (view.replaceClip && view.clipPath != _tipClipPath)) {
            _tipReplaceClip = view.replaceClip;
            _tipClipPath = view.clipPath;
            view.acceptTip = view.replaceClip ? Format("Process the sound and write it over the game's clip {} + .artgen.json. Both are in git. Then reload what plays it.", view.clipPath)
                                              : Format("Process the sound and write it into the game as a 16-bit WAV: {} + .artgen.json. Both are in git. Then reload what plays it.",
                                                       ArtGenGameFile(kPlaceholderName, kKindAudio));
        }
        if (_soundAcceptStale) {
            _soundAcceptStale = false;
            view.hasPreview = _preview.Get() != nullptr;
            view.previewText =
                view.hasPreview ? Format("{:.2f} s, {} Hz, {}", _preview->GetDurationSeconds(), _preview->GetFormat().sampleRate, _preview->GetFormat().channels == 1 ? "mono" : "stereo") : String();
            view.previewError = _previewError;
            view.hasPreviewError = !_previewError.empty();
        }
        const StringView nodeTip = _view.accept.haveNode ? kUseOnSelectedTip : kSelectANodeTip;
        if (StringView(view.useOnSelectedTip) != nodeTip) {
            view.useOnSelectedTip = String(nodeTip);
        }
    }

    void ArtGenPanelModel::RunListen() {
        _soundAcceptStale = true;
        if (_playingRow == kPreviewRow) {
            StopSound();
            return;
        }
        const AudioClip* clip = PickedClip();
        if (clip == nullptr) {
            return;
        }
        if (!_preview) {
            auto processed = ProcessArtGenSound(DecodedOf(*clip), _soundSettings);
            if (processed) {
                _preview = AudioClip::Create("artgen-preview"_intern, std::move(*processed));
                _previewError.clear();
            } else {
                _previewError = processed.error();
            }
        }
        if (_preview) {
            PlaySound(*_preview, kPreviewRow);
        }
    }

    void ArtGenPanelModel::AcceptSound() {
        const ArtGenVariant* variant = _selectedSeed ? GetSession().FindVariant(*_selectedSeed) : nullptr;
        const ArtGenSoundAcceptView& view = _view.soundAccept;
        const ArtGenAcceptView& accept = _view.accept;
        if (variant == nullptr || !variant->IsAudio()) {
            return;
        }
        ArtGenSoundTarget target;
        if (view.replaceClip) {
            if (!IsValidClipPath(StringView(view.clipPath))) {
                return;
            }
            target.clip = view.clipPath;
        } else {
            if (!IsValidSpriteName(StringView(accept.sprite))) {
                return;
            }
            target.name = accept.sprite;
        }
        if (_playingRow == kPreviewRow) {
            StopSound();
        }
        const bool useOnSelected = accept.useOnSelected && GetSession().GetSelectedNodeIfAlive() != nullptr;
        auto result = GetSession().AcceptSound(*_selectedSeed, target, _soundSettings, useOnSelected);
        _acceptFailed = !result.has_value();
        _acceptMessage = result ? Format("Accepted as {}; {} source(s) reloaded", result->clip.value_or(String()), result->reloadedSources.value_or(0)) : result.error().message;
        _acceptStale = true;
    }

    void ArtGenPanelModel::PlaySound(const AudioClip& clip, int32_t row) {
        StopSound();
        // The Ui bus: a preview is the editor's, not the game's effects, and the game's Sfx level does not hide it
        _playing = CoreManager::GetAudio().Play(clip, AudioBus::Ui);
        _playingRow = row;
    }

    void ArtGenPanelModel::StopSound() {
        if (_playing.IsValid()) {
            CoreManager::GetAudio().Stop(_playing);
        }
        _playing = AudioHandle{};
        _playingRow = -1;
    }

    void ArtGenPanelModel::RefreshPlaying() {
        if (_playing.IsValid() && !CoreManager::GetAudio().IsPlaying(_playing)) {
            _playing = AudioHandle{};
            _playingRow = -1;
        }
    }

    void ArtGenPanelModel::ReleaseSetTab() {
        if (_playingRow == kSetTabRow) {
            StopSound();
        }
        _setPanel.DropThumbnails();
    }

    void ArtGenPanelModel::SyncSounds(const ArtGenRun& run) {
        bool same = _sounds.size() <= run.variants.size();
        for (size_t i = 0; same && i < _sounds.size(); ++i) {
            same = _sounds[i].path == run.variants[i].path;
        }
        if (!same) {
            DropSounds();
        }
        auto& resources = CoreManager::GetResourceManager();
        while (_sounds.size() < run.variants.size()) {
            const ArtGenVariant& variant = run.variants[_sounds.size()];
            SoundRow& row = _sounds.emplace_back();
            row.path = variant.path;
            if (variant.IsAudio()) {
                row.clip = resources.Load<AudioClip>(StringView(variant.path.data(), variant.path.size()));
            }
        }
        for (SoundRow& row : _sounds) {
            if (!row.measured && row.clip && row.clip->GetState() == ResourceState::Loaded) {
                const DecodedAudio audio = DecodedOf(*row.clip);
                row.waveform = BuildArtGenWaveform(audio, kWaveformBuckets);
                row.stats = MeasureArtGenSound(audio);
                row.measured = true;
            }
        }
    }

    void ArtGenPanelModel::DropSounds() {
        if (_sounds.empty() && !_preview) {
            return;
        }
        // The Set tab's sound is not one of these rows: it plays on
        if (_playingRow != kSetTabRow) {
            StopSound();
        }
        // Like the thumbnails: a generated sound is heard a few times, not kept in the cache until the editor closes
        auto& resources = CoreManager::GetResourceManager();
        for (const SoundRow& row : _sounds) {
            if (row.clip) {
                resources.Evict(StringView(row.path.data(), row.path.size()));
            }
        }
        _sounds.clear();
        _preview.Reset();
        _soundAcceptStale = true;
    }

}  // namespace BECore
