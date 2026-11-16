#include "ArtGenAccept.h"

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <algorithm>
#include <cstdio>
#include <system_error>

namespace BECore {

    namespace {

        constexpr Array<uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        // The signature, the IHDR length and type, then width and height as big-endian 32-bit numbers
        constexpr size_t kPngHeaderBytes = 24;
        constexpr size_t kPngWidthOffset = 16;
        constexpr StringView kPngExtension = ".png";
        constexpr StringView kProvenanceExtension = ".artgen.json";

        uint32_t ReadBigEndian32(const uint8_t* bytes) {
            return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
        }

        std::filesystem::path PathOf(StringView text) {
            return std::filesystem::path(ToStd(text));  //-NoStd: std::filesystem::path takes a std::string
        }

        std::expected<void, String> CopyOver(const std::filesystem::path& from, const std::filesystem::path& to) {
            std::error_code error;
            std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                return std::unexpected(Format("cannot copy '{}' to '{}': {}", from.generic_string(), to.generic_string(), error.message()));
            }
            return {};
        }

        constexpr StringView kWavExtension = ".wav";
        constexpr StringView kAssetsPrefix = "assets/";
        constexpr int kProvenanceIndent = 2;

        std::expected<Vector<uint8_t>, String> ReadBytes(const std::filesystem::path& file) {
            std::FILE* stream = std::fopen(file.string().c_str(), "rb");
            if (stream == nullptr) {
                return std::unexpected(Format("cannot open '{}'", file.generic_string()));
            }
            Vector<uint8_t> bytes;
            Array<uint8_t, 65536> chunk{};
            for (size_t read = std::fread(chunk.data(), 1, chunk.size(), stream); read > 0; read = std::fread(chunk.data(), 1, chunk.size(), stream)) {
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(read));
            }
            std::fclose(stream);
            return bytes;
        }

        std::expected<void, String> WriteBytes(const std::filesystem::path& file, Span<const uint8_t> bytes) {
            const std::filesystem::path temp = PathOf(Format("{}.tmp", file.generic_string()));
            std::FILE* stream = std::fopen(temp.string().c_str(), "wb");
            if (stream == nullptr) {
                return std::unexpected(Format("cannot write '{}'", temp.generic_string()));
            }
            const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), stream);
            const bool closed = std::fclose(stream) == 0;
            std::error_code error;
            if (written != bytes.size() || !closed) {
                std::filesystem::remove(temp, error);
                return std::unexpected(Format("cannot write '{}'", temp.generic_string()));
            }
            // A game may be playing the old clip: the rename replaces it whole, never a half-written file
            std::filesystem::rename(temp, file, error);
            if (error) {
                std::filesystem::remove(temp, error);
                return std::unexpected(Format("cannot replace '{}': {}", file.generic_string(), error.message()));
            }
            return {};
        }

        bool IsClipPart(StringView part) {
            if (part.empty() || part == "." || part == "..") {
                return false;
            }
            return std::all_of(part.begin(), part.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; });
        }

        void ReloadSourcesUnder(SceneNode& node, PoolString clip, uint32_t& count) {
            if (auto source = node.GetComponent<AudioSourceComponent>()) {
                if (source->_clip == clip) {
                    source->ReloadClip();
                    ++count;
                }
            }
            for (const auto& child : node.GetChildren()) {
                ReloadSourcesUnder(*child, clip, count);
            }
        }

        void ReloadUnder(SceneNode& node, PoolString sprite, PoolString texturePath, uint32_t& count) {
            if (auto renderer = node.GetComponent<SpriteRendererComponent>()) {
                if ((!sprite.Empty() && renderer->_spriteId == sprite) || (!texturePath.Empty() && renderer->_texturePath == texturePath)) {
                    renderer->ReloadTexture();
                    ++count;
                }
            }
            for (const auto& child : node.GetChildren()) {
                ReloadUnder(*child, sprite, texturePath, count);
            }
        }

    }  // namespace

    bool IsValidSpriteName(StringView name) {
        if (name.empty() || name.size() > kMaxSpriteNameLength) {
            return false;
        }
        for (const char c : name) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    Optional<Pair<uint32_t, uint32_t>> ReadPngSize(const std::filesystem::path& file) {
        std::FILE* stream = std::fopen(file.string().c_str(), "rb");
        if (stream == nullptr) {
            return {};
        }
        Array<uint8_t, kPngHeaderBytes> header{};
        const size_t read = std::fread(header.data(), 1, header.size(), stream);
        std::fclose(stream);
        if (read != header.size() || !std::equal(kPngSignature.begin(), kPngSignature.end(), header.begin())) {
            return {};
        }
        return Pair<uint32_t, uint32_t>(ReadBigEndian32(header.data() + kPngWidthOffset), ReadBigEndian32(header.data() + kPngWidthOffset + 4));
    }

    bool IsValidClipPath(StringView clip) {
        if (!clip.starts_with(kAssetsPrefix) || !clip.ends_with(kWavExtension) || clip.size() <= kAssetsPrefix.size() + kWavExtension.size()) {
            return false;
        }
        StringView rest = clip.substr(kAssetsPrefix.size());
        while (!rest.empty()) {
            const size_t slash = rest.find('/');
            const StringView part = rest.substr(0, slash);
            if (!IsClipPart(part)) {
                return false;
            }
            rest = slash == StringView::npos ? StringView() : rest.substr(slash + 1);
            if (slash != StringView::npos && rest.empty()) {
                return false;  // a trailing slash
            }
        }
        return true;
    }

    std::expected<ArtGenSoundWritten, String> WriteSoundIntoAssets(const std::filesystem::path& sourceFile, const std::filesystem::path& provenance, const std::filesystem::path& assetsDir,
                                                                   const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings) {
        String clipPath;
        if (!target.clip.empty() && target.name.empty()) {
            if (!IsValidClipPath(StringView(target.clip))) {
                return std::unexpected(Format("'{}' is not a clip path of the game (assets/<folders>/<name>.wav)", target.clip));
            }
            clipPath = target.clip;
        } else if (target.clip.empty() && !target.name.empty()) {
            if (!IsValidSpriteName(StringView(target.name))) {
                return std::unexpected(Format("'{}' is not a sound name (1 to {} of A-Z a-z 0-9 _ -)", target.name, kMaxSpriteNameLength));
            }
            clipPath = Format("assets/{}/{}{}", kArtGenAssetsFolder, target.name, kWavExtension);
        } else {
            return std::unexpected(String("give a sound name or a clip path, one of them"));
        }

        auto bytes = ReadBytes(sourceFile);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto decoded = DecodeAudio(*bytes);
        if (!decoded) {
            return std::unexpected(Format("'{}': {}", sourceFile.generic_string(), decoded.error()));
        }
        auto processed = ProcessArtGenSound(*decoded, settings);
        if (!processed) {
            return std::unexpected(processed.error());
        }
        auto wav = EncodeWav(processed->samples, processed->format, WavSampleFormat::Pcm16);
        if (!wav) {
            return std::unexpected(wav.error());
        }

        ArtGenSoundWritten written;
        written.clipPath = clipPath;
        written.wavFile = assetsDir / PathOf(StringView(clipPath).substr(kAssetsPrefix.size()));
        written.provenanceFile = PathOf(Format("{}{}", written.wavFile.generic_string().substr(0, written.wavFile.generic_string().size() - kWavExtension.size()), kProvenanceExtension));
        written.format = processed->format;
        const ArtGenSoundStats stats = MeasureArtGenSound(*processed);
        written.seconds = stats.seconds;
        written.peakDb = stats.PeakDb();

        std::error_code error;
        std::filesystem::create_directories(written.wavFile.parent_path(), error);
        if (error) {
            return std::unexpected(Format("cannot create '{}': {}", written.wavFile.parent_path().generic_string(), error.message()));
        }
        if (auto result = WriteBytes(written.wavFile, *wav); !result) {
            return std::unexpected(result.error());
        }

        // The generation's record, and what Accept did to the sound, so the clip can be made again
        Json record = Json::object();
        if (auto text = ReadTextFile(provenance); text) {
            if (auto parsed = ParseJson(*text); parsed && parsed->is_object()) {
                record = std::move(*parsed);
            }
        }
        Json accepted = Json::object();
        accepted["clip"] = ToStdString(clipPath);
        accepted["source"] = sourceFile.generic_string();
        accepted["processing"] = ArtGenSoundSettingsToJson(settings);
        accepted["sampleRate"] = written.format.sampleRate;
        accepted["channels"] = written.format.channels;
        accepted["seconds"] = written.seconds;
        accepted["peakDb"] = written.peakDb;
        record["accepted"] = std::move(accepted);
        const std::string text = record.dump(kProvenanceIndent, ' ', false, Json::error_handler_t::replace) + "\n";  //-NoStd: nlohmann's dump
        if (auto result = WriteTextFileAtomically(written.provenanceFile, StringView(text.data(), text.size())); !result) {
            return std::unexpected(result.error());
        }
        return written;
    }

    uint32_t ReloadAudioSourcesUsing(SceneNode& root, PoolString clip) {
        uint32_t count = 0;
        ReloadSourcesUnder(root, clip, count);
        return count;
    }

    std::expected<Json, ControlError> AcceptSoundForControl(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* node) {
        auto fail = [](const String& message) { return std::unexpected(ControlError{String("artgen_accept_failed"), message}); };
        const std::filesystem::path assetsDir = CoreManager::GetFileSystem().GetMountedPath("assets"_intern);
        if (assetsDir.empty()) {
            return fail(String("the game has no assets folder"));
        }
        if (node != nullptr && !node->GetComponent<AudioSourceComponent>()) {
            return fail(Format("'{}' has no AudioSourceComponent", node->GetName().ToStringView()));
        }
        auto written = WriteSoundIntoAssets(PathOf(variant.path), PathOf(variant.provenancePath), assetsDir, target, settings);
        if (!written) {
            return fail(written.error());
        }
        // The old sound of this path may be cached: drop it, so a play by path and every source of the scene read the new file
        const PoolString clip = PoolString::Intern(written->clipPath);
        CoreManager::GetResourceManager().Evict(written->clipPath);
        uint32_t reloaded = 0;
        if (Scene* scene = CoreManager::GetSceneManager().GetActiveScene()) {
            reloaded = ReloadAudioSourcesUsing(scene->GetRootNode(), clip);
        }
        Json reply = Json::object();
        reply["clip"] = ToStdString(written->clipPath);
        reply["wav"] = written->wavFile.generic_string();
        reply["sampleRate"] = written->format.sampleRate;
        reply["channels"] = written->format.channels;
        reply["seconds"] = written->seconds;
        reply["peakDb"] = written->peakDb;
        reply["reloadedSources"] = reloaded;
        reply["usedOn"] = Json();
        if (node != nullptr) {
            auto source = node->GetComponent<AudioSourceComponent>();
            source->_clip = clip;
            source->ReloadClip();
            reply["usedOn"] = ToStdString(node->GetName().ToStringView());
            SceneEditedEvent::Emit(CoreManager::GetSceneManager().GetActiveScene());
        }
        return reply;
    }

    std::expected<ArtGenCopied, String> CopyVariantIntoAssets(const std::filesystem::path& png, const std::filesystem::path& provenance, const std::filesystem::path& assetsDir, StringView sprite) {
        if (!IsValidSpriteName(sprite)) {
            return std::unexpected(Format("'{}' is not a sprite name (1 to {} of A-Z a-z 0-9 _ -)", sprite, kMaxSpriteNameLength));
        }
        const auto size = ReadPngSize(png);
        if (!size) {
            return std::unexpected(Format("'{}' is not a PNG file", png.generic_string()));
        }
        const std::filesystem::path folder = assetsDir / ToStd(kArtGenAssetsFolder);  //-NoStd: std::filesystem::path takes a std::string
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error) {
            return std::unexpected(Format("cannot create '{}': {}", folder.generic_string(), error.message()));
        }
        ArtGenCopied copied;
        copied.pngFile = folder / PathOf(Format("{}{}", sprite, kPngExtension));
        copied.provenanceFile = folder / PathOf(Format("{}{}", sprite, kProvenanceExtension));
        copied.width = size->first;
        copied.height = size->second;
        copied.texturePath = Format("assets/{}/{}{}", kArtGenAssetsFolder, sprite, kPngExtension);
        if (auto result = CopyOver(png, copied.pngFile); !result) {
            return std::unexpected(result.error());
        }
        if (!provenance.empty() && std::filesystem::exists(provenance)) {
            if (auto result = CopyOver(provenance, copied.provenanceFile); !result) {
                return std::unexpected(result.error());
            }
        }
        return copied;
    }

    uint32_t ReloadSpritesUsing(SceneNode& root, PoolString sprite, PoolString texturePath) {
        uint32_t count = 0;
        ReloadUnder(root, sprite, texturePath, count);
        return count;
    }

    std::expected<ArtGenAccepted, String> AcceptVariant(const ArtGenVariant& variant, StringView sprite) {
        const std::filesystem::path assetsDir = CoreManager::GetFileSystem().GetMountedPath("assets"_intern);
        if (assetsDir.empty()) {
            return std::unexpected(String("the game has no assets folder"));
        }
        auto& library = CoreManager::GetTextureLibrary();
        auto copied = CopyVariantIntoAssets(PathOf(variant.path), PathOf(variant.provenancePath), assetsDir, sprite);
        if (!copied) {
            return std::unexpected(copied.error());
        }

        const PoolString name = PoolString::Intern(sprite);
        const PoolString texturePath = PoolString::Intern(copied->texturePath);
        SpriteEntry entry;
        entry.name = name;
        entry.texturePath = texturePath;
        entry.srcRect = Rect(0.0f, 0.0f, static_cast<float>(copied->width), static_cast<float>(copied->height));
        Optional<SpriteEntry> previous;
        if (const SpriteEntry* existing = library.GetSprite(name)) {
            previous = *existing;
        }
        library.AddOrUpdate(entry);
        if (!library.Save()) {
            // Keep the library as it was on disk: the next Save of the Sprite Library panel must not write a half accept
            if (previous) {
                library.AddOrUpdate(*previous);
            } else {
                library.Remove(name);
            }
            return std::unexpected(Format("the PNG is in '{}', but TextureLibrary.xml could not be saved (does the game have one?)", copied->pngFile.generic_string()));
        }

        // The old picture of this path may be cached: drop it so the reload reads the new file
        CoreManager::GetResourceManager().Evict(copied->texturePath);
        ArtGenAccepted accepted;
        accepted.copied = std::move(*copied);
        if (Scene* scene = CoreManager::GetSceneManager().GetActiveScene()) {
            accepted.reloadedSprites = ReloadSpritesUsing(scene->GetRootNode(), name, texturePath);
        }
        return accepted;
    }

    std::expected<void, String> UseSpriteOn(SceneNode& node, PoolString sprite) {
        auto renderer = node.GetComponent<SpriteRendererComponent>();
        if (!renderer) {
            return std::unexpected(Format("'{}' has no SpriteRendererComponent", node.GetName().ToStringView()));
        }
        renderer->_spriteId = sprite;
        renderer->ReloadTexture();
        return {};
    }

    std::expected<Json, ControlError> AcceptForControl(const ArtGenVariant& variant, StringView sprite, SceneNode* node) {
        auto fail = [](const String& message) { return std::unexpected(ControlError{String("artgen_accept_failed"), message}); };
        auto accepted = AcceptVariant(variant, sprite);
        if (!accepted) {
            return fail(accepted.error());
        }
        Json reply = Json::object();
        reply["sprite"] = ToStdString(sprite);
        reply["texturePath"] = ToStdString(accepted->copied.texturePath);
        reply["png"] = accepted->copied.pngFile.generic_string();
        reply["width"] = accepted->copied.width;
        reply["height"] = accepted->copied.height;
        reply["reloadedSprites"] = accepted->reloadedSprites;
        reply["usedOn"] = Json();
        if (node != nullptr) {
            if (auto used = UseSpriteOn(*node, PoolString::Intern(sprite)); !used) {
                return fail(Format("accepted as '{}', but: {}", sprite, used.error()));
            }
            reply["usedOn"] = ToStdString(node->GetName().ToStringView());
            SceneEditedEvent::Emit(CoreManager::GetSceneManager().GetActiveScene());
        }
        return reply;
    }

}  // namespace BECore
