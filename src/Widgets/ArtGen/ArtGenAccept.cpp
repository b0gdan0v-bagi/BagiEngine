#include "ArtGenAccept.h"

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <Widgets/ArtGen/ArtGenAtlasAccept.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
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

        std::expected<void, String> WriteProvenance(const std::filesystem::path& file, const Json& record) {
            String text;
            AppendJson(text, record, kProvenanceIndent);
            text.push_back('\n');
            return WriteTextFileAtomically(file, text);
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

        bool IsSameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        bool IsSamePackRecord(const Optional<AtlasPackRecord>& a, const Optional<AtlasPackRecord>& b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() || (a->padding == b->padding && a->sizeMode == b->sizeMode && a->maxSide == b->maxSide);
        }

        bool IsSameAtlasLink(const Optional<SpriteAtlasLink>& a, const Optional<SpriteAtlasLink>& b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            if (!a.has_value()) {
                return true;
            }
            if (a->name != b->name || a->source.has_value() != b->source.has_value() || !IsSamePackRecord(a->pack, b->pack)) {
                return false;
            }
            return !a->source.has_value() || (a->source->texturePath == b->source->texturePath && IsSameRect(a->source->rect, b->source->rect));
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

    bool IsSameSpriteEntry(const SpriteEntry& a, const SpriteEntry& b) {
        return a.name == b.name && a.texturePath == b.texturePath && IsSameRect(a.srcRect, b.srcRect) && IsSameAtlasLink(a.atlas, b.atlas);
    }

    String ArtGenGameFile(StringView name, StringView kind) {
        return Format("assets/{}/{}{}", kArtGenAssetsFolder, name, kind == kKindAudio ? kWavExtension : kPngExtension);
    }

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
            clipPath = ArtGenGameFile(target.name, kKindAudio);
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
        written.provenanceFile = ProvenanceFileOf(written.wavFile);
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
        if (auto result = WriteProvenance(written.provenanceFile, record); !result) {
            return std::unexpected(result.error());
        }
        return written;
    }

    uint32_t ReloadAudioSourcesUsing(SceneNode& root, PoolString clip) {
        uint32_t count = 0;
        ReloadSourcesUnder(root, clip, count);
        return count;
    }

    std::expected<ArtGenAcceptAnswer, ControlError> AcceptSoundForControl(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* node) {
        auto fail = [](const String& message) { return std::unexpected(ControlError{String("artgen_accept_failed"), message}); };
        const std::filesystem::path assetsDir = CoreManager::GetFileSystem().GetMountedPath(MountPoints::assets);
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
        ArtGenAcceptAnswer reply;
        reply.clip = written->clipPath;
        reply.wav = String(Format("{}", written->wavFile.generic_string()));
        reply.sampleRate = written->format.sampleRate;
        reply.channels = written->format.channels;
        reply.seconds = written->seconds;
        reply.peakDb = static_cast<double>(written->peakDb);
        reply.reloadedSources = reloaded;
        if (node != nullptr) {
            auto source = node->GetComponent<AudioSourceComponent>();
            source->_clip = clip;
            source->ReloadClip();
            reply.usedOn = String(node->GetName().ToStringView());
            SceneEditedEvent::Emit(CoreManager::GetSceneManager().GetActiveScene());
        }
        return reply;
    }

    namespace {

        constexpr StringView kFrameOfSprite = "sprite";
        constexpr StringView kFrameOfSet = "set";

        /// Where the written picture came from, as the provenance file records it: `{crop, scale, size, frame}`. It stays a document of its own (the file keeps its
        /// writer and merges into what the service wrote); `MakeArtGenProcessingAnswer` is the answer's mirror of it
        Json PlacementToJson(const ArtGenCopied& copied) {
            Json crop = Json::object();
            crop["x"] = copied.crop.x;
            crop["y"] = copied.crop.y;
            crop["w"] = copied.crop.w;
            crop["h"] = copied.crop.h;
            Json size = Json::array();
            size.push_back(copied.width);
            size.push_back(copied.height);
            Json json = Json::object();
            json["crop"] = std::move(crop);
            json["scale"] = copied.scale;
            json["size"] = std::move(size);
            json["frame"] = ToStdString(copied.fromSet ? kFrameOfSet : kFrameOfSprite);
            return json;
        }

        /// The generation's record, and what Accept did to the picture, so the sprite can be made again
        std::expected<void, String> WriteImageProvenance(const std::filesystem::path& source, const std::filesystem::path& provenance, StringView sprite, const ArtGenCopied& copied) {
            Json record = Json::object();
            if (!provenance.empty()) {
                if (auto text = ReadTextFile(provenance); text) {
                    if (auto parsed = ParseJson(*text); parsed && parsed->is_object()) {
                        record = std::move(*parsed);
                    }
                }
            }
            const Optional<int64_t> seed = GetJsonInt(record, "seed");
            Json accepted = PlacementToJson(copied);
            accepted["sprite"] = ToStdString(sprite);
            accepted["source"] = source.generic_string();
            accepted["seed"] = seed ? Json(*seed) : Json();
            accepted["processing"] = ArtGenImageSettingsToJson(copied.settings);
            record["accepted"] = std::move(accepted);
            return WriteProvenance(copied.provenanceFile, record);
        }

    }  // namespace

    ArtGenProcessingAnswer MakeArtGenProcessingAnswer(const ArtGenCopied& copied) {
        ArtGenProcessingAnswer answer;
        answer.trim = copied.settings.trim;
        answer.padding = copied.settings.padding;
        answer.maxSize = copied.settings.maxSize;
        answer.crop.x = copied.crop.x;
        answer.crop.y = copied.crop.y;
        answer.crop.w = copied.crop.w;
        answer.crop.h = copied.crop.h;
        answer.scale = copied.scale;
        answer.size = {copied.width, copied.height};
        answer.frame = String(copied.fromSet ? kFrameOfSet : kFrameOfSprite);
        return answer;
    }

    std::expected<ArtGenCopied, String> CopyVariantIntoAssets(const std::filesystem::path& png, const std::filesystem::path& provenance, const std::filesystem::path& assetsDir, StringView sprite,
                                                              const ArtGenImagePlan& plan) {
        if (!IsValidSpriteName(sprite)) {
            return std::unexpected(Format("'{}' is not a sprite name (1 to {} of A-Z a-z 0-9 _ -)", sprite, kMaxSpriteNameLength));
        }
        const std::filesystem::path folder = assetsDir / ToStd(kArtGenAssetsFolder);  //-NoStd: std::filesystem::path takes a std::string
        ArtGenCopied copied;
        copied.pngFile = folder / PathOf(Format("{}{}", sprite, kPngExtension));
        copied.provenanceFile = ProvenanceFileOf(copied.pngFile);
        copied.texturePath = ArtGenGameFile(sprite, kKindImage);
        copied.settings = plan.settings;
        copied.fromSet = plan.frame.has_value();

        Optional<ArtGenPixels> processed;
        if (plan.settings.IsIdentity()) {
            const auto size = ReadPngSize(png);
            if (!size) {
                return std::unexpected(Format("'{}' is not a PNG file", png.generic_string()));
            }
            copied.width = size->first;
            copied.height = size->second;
            copied.crop = ArtGenIntRect{0, 0, static_cast<int32_t>(size->first), static_cast<int32_t>(size->second)};
        } else {
            auto pixels = ReadArtGenPng(png);
            if (!pixels) {
                return std::unexpected(pixels.error());
            }
            const Optional<ArtGenIntRect> drawn = DrawnBounds(*pixels);
            if (!drawn) {
                return std::unexpected(Format("'{}': nothing drawn: the picture is empty", png.generic_string()));
            }
            ArtGenImageFrame frame;
            if (plan.frame) {
                if (!FrameContains(*plan.frame, *drawn)) {
                    return std::unexpected(Format("'{}' does not fit the frame of the set: it would be cut", png.generic_string()));
                }
                frame = *plan.frame;
            } else {
                auto own = MakeArtGenFrame(Span<const ArtGenIntRect>(&*drawn, 1), static_cast<int32_t>(pixels->width), static_cast<int32_t>(pixels->height), plan.settings);
                if (!own) {
                    return std::unexpected(own.error());
                }
                frame = *own;
            }
            auto made = ProcessArtGenImage(*pixels, frame);
            if (!made) {
                return std::unexpected(Format("'{}': {}", png.generic_string(), made.error()));
            }
            processed = std::move(*made);
            copied.width = processed->width;
            copied.height = processed->height;
            copied.crop = frame.Crop();
            copied.scale = static_cast<double>(frame.outWidth) / frame.w;
        }

        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error) {
            return std::unexpected(Format("cannot create '{}': {}", folder.generic_string(), error.message()));
        }
        if (processed) {
            if (auto result = WriteArtGenPng(copied.pngFile, *processed); !result) {
                return std::unexpected(result.error());
            }
            if (auto result = WriteImageProvenance(png, provenance, sprite, copied); !result) {
                return std::unexpected(result.error());
            }
            return copied;
        }
        if (auto result = CopyOver(png, copied.pngFile); !result) {
            return std::unexpected(result.error());
        }
        if (!provenance.empty() && std::filesystem::exists(provenance)) {
            if (auto result = CopyOver(provenance, copied.provenanceFile); !result) {
                return std::unexpected(result.error());
            }
        } else {
            // No record for this picture: the file an earlier accept of the name left describes another variant (its seed, its `kept`)
            std::filesystem::remove(copied.provenanceFile, error);
            if (error) {
                return std::unexpected(Format("cannot remove the old '{}': {}", copied.provenanceFile.generic_string(), error.message()));
            }
        }
        return copied;
    }

    void EvictAcceptedTextures(const ArtGenCopied& copied, const Optional<AtlasWritten>& repacked, FunctionRef<bool(StringView)> evict) {
        evict(copied.texturePath);
        if (repacked) {
            evict(repacked->texturePath.ToStringView());
        }
    }

    uint32_t ReloadSpritesUsing(SceneNode& root, PoolString sprite, PoolString texturePath) {
        uint32_t count = 0;
        ReloadUnder(root, sprite, texturePath, count);
        return count;
    }

    SpriteEntry AcceptedSpriteEntry(const TextureLibrary& library, PoolString sprite, PoolString texturePath, uint32_t width, uint32_t height) {
        const Rect whole(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        SpriteEntry entry;
        entry.name = sprite;
        entry.texturePath = texturePath;
        entry.srcRect = whole;
        const SpriteEntry* existing = library.GetSprite(sprite);
        if (existing == nullptr || !existing->atlas.has_value()) {
            return entry;
        }
        entry.atlas = existing->atlas;
        if (IsPackedSprite(*existing)) {
            // The sprite stays in its atlas until the repack moves it; only the pixels it is cut from change
            entry.texturePath = existing->texturePath;
            entry.srcRect = existing->srcRect;
            SpriteSource source;
            source.texturePath = texturePath;
            source.rect = whole;
            entry.atlas->source = source;
        }
        return entry;
    }

    std::expected<bool, String> StoreSpriteEntry(TextureLibrary& library, const SpriteEntry& entry, FunctionRef<bool()> save) {
        Optional<SpriteEntry> previous;
        if (const SpriteEntry* existing = library.GetSprite(entry.name)) {
            if (IsSameSpriteEntry(*existing, entry)) {
                return false;
            }
            previous = *existing;
        }
        library.AddOrUpdate(entry);
        if (save()) {
            return true;
        }
        // Keep the library as it was on disk: the next Save of the Sprite Library panel must not write a half accept
        if (previous) {
            library.AddOrUpdate(*previous);
        } else {
            library.Remove(entry.name);
        }
        return std::unexpected(String("TextureLibrary.xml could not be saved (does the game have one?)"));
    }

    ArtGenLibraryBatch::ArtGenLibraryBatch(TextureLibrary& library) : _library(library), _snapshot(library.GetAll()) {}

    std::expected<bool, String> ArtGenLibraryBatch::Commit(FunctionRef<bool()> save) {
        if (!_owed) {
            return false;
        }
        if (save()) {
            return true;
        }
        Vector<PoolString> added;
        for (const SpriteEntry& entry : _library.GetAll()) {
            const bool known = std::any_of(_snapshot.begin(), _snapshot.end(), [&entry](const SpriteEntry& old) { return old.name == entry.name; });
            if (!known) {
                added.push_back(entry.name);
            }
        }
        for (const PoolString name : added) {
            _library.Remove(name);
        }
        for (const SpriteEntry& old : _snapshot) {
            const SpriteEntry* now = _library.GetSprite(old.name);
            if (now == nullptr || !IsSameSpriteEntry(old, *now)) {
                _library.AddOrUpdate(old);
            }
        }
        if (_repacked) {
            return std::unexpected(String("TextureLibrary.xml could not be saved (does the game have one?); an atlas file was already rewritten, so press Pack in the Sprite Library"));
        }
        return std::unexpected(String("TextureLibrary.xml could not be saved (does the game have one?)"));
    }

    ArtGenAtlasAnswer MakeArtGenAtlasAnswer(const AtlasWritten& repacked) {
        ArtGenAtlasAnswer answer;
        answer.name = String(repacked.name.ToStringView());
        answer.texturePath = String(repacked.texturePath.ToStringView());
        answer.width = repacked.width;
        answer.height = repacked.height;
        answer.members = repacked.changed.size();
        return answer;
    }

    std::expected<AcceptStored, RepackError> RepackGroupAndSave(TextureLibrary& library, StringView group, const AtlasIo& io, FunctionRef<bool()> save) {
        // The members as the saved library has them: what a failed save of the repack goes back to
        Vector<SpriteEntry> before;
        for (const PoolString member : AtlasMembers(library, group)) {
            before.push_back(*library.GetSprite(member));
        }
        auto written = WriteAtlas(library, group, PackSettingsOf(library, group), io);
        if (!written) {
            return std::unexpected(RepackError{false, std::move(written.error())});
        }
        AcceptStored result;
        const bool entriesChanged = std::any_of(before.begin(), before.end(), [&library](const SpriteEntry& old) { return !IsSameSpriteEntry(old, *library.GetSprite(old.name)); });
        if (entriesChanged) {
            if (!save()) {
                for (const SpriteEntry& old : before) {
                    library.AddOrUpdate(old);
                }
                return std::unexpected(RepackError{true, String()});
            }
            result.saved = true;
        }
        result.repacked = std::move(*written);
        return result;
    }

    std::expected<AcceptStored, String> AcceptIntoLibrary(TextureLibrary& library, const ArtGenCopied& copied, StringView sprite, const AtlasIo& io, FunctionRef<bool()> save) {
        const PoolString name = PoolString::Intern(sprite);
        const PoolString texturePath = PoolString::Intern(copied.texturePath);
        const SpriteEntry* existing = library.GetSprite(name);
        const bool packed = existing != nullptr && IsPackedSprite(*existing);
        const PoolString group = packed ? existing->atlas->name : PoolString();

        const SpriteEntry entry = AcceptedSpriteEntry(library, name, texturePath, copied.width, copied.height);
        auto stored = StoreSpriteEntry(library, entry, save);
        if (!stored) {
            return std::unexpected(Format("the PNG was already written over the game's file '{}', so the library entry was not updated: {}", copied.pngFile.generic_string(), stored.error()));
        }
        AcceptStored result;
        result.saved = *stored;
        if (!packed) {
            return result;
        }

        auto repacked = RepackGroupAndSave(library, group.ToStringView(), io, save);
        if (!repacked) {
            if (!repacked.error().atlasWritten) {
                return std::unexpected(
                    Format("the PNG was already written over the game's file '{}' and the library points to it, but atlas '{}' could not be repacked: {}; press Pack in the Sprite Library",
                           copied.pngFile.generic_string(), group.ToStringView(), repacked.error().reason));
            }
            return std::unexpected(Format("the PNG was already written over the game's file '{}' and atlas '{}' was rewritten, but TextureLibrary.xml could not be saved, so the library entries "
                                          "were not updated; press Pack in the Sprite Library",
                                          copied.pngFile.generic_string(), group.ToStringView()));
        }
        repacked->saved = repacked->saved || result.saved;
        return std::move(*repacked);
    }

    namespace {

        // Global because the session's acceptors are plain function pointers; the editor is single-threaded, so there is one batch at most
        Optional<ArtGenLibraryBatch> gOpenBatch;

    }  // namespace

    void BeginAcceptBatch() {
        ASSERT(!gOpenBatch.has_value(), "BeginAcceptBatch while an accept batch is open");
        if (gOpenBatch.has_value()) {
            return;
        }
        gOpenBatch.emplace(CoreManager::GetTextureLibrary());
    }

    bool SaveGameLibraryForAccept() {
        return gOpenBatch.has_value() ? gOpenBatch->Defer() : CoreManager::GetTextureLibrary().Save();
    }

    void NoteAtlasRewrittenForAccept() {
        if (gOpenBatch.has_value()) {
            gOpenBatch->NoteRepacked();
        }
    }

    std::expected<void, ControlError> EndAcceptBatch() {
        if (!gOpenBatch.has_value()) {
            return {};
        }
        auto& library = CoreManager::GetTextureLibrary();
        auto committed = gOpenBatch->Commit([&library] { return library.Save(); });
        gOpenBatch.reset();
        if (!committed) {
            return std::unexpected(
                ControlError{String("artgen_accept_failed"), Format("{}; the PNGs of the accepted rows are in the game's assets, the library keeps its old entries", committed.error())});
        }
        return {};
    }

    std::expected<ArtGenAccepted, String> AcceptVariant(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan) {
        const std::filesystem::path assetsDir = CoreManager::GetFileSystem().GetMountedPath(MountPoints::assets);
        if (assetsDir.empty()) {
            return std::unexpected(String("the game has no assets folder"));
        }
        auto& library = CoreManager::GetTextureLibrary();
        if (!plan.atlas.empty()) {
            // Refuse a join the library cannot take before the PNG replaces the file of the sprite
            const PoolString texturePath = PoolString::Intern(ArtGenGameFile(sprite, kKindImage));
            if (auto joined = AcceptedSpriteEntryInGroup(library, PoolString::Intern(sprite), texturePath, 0, 0, StringView(plan.atlas)); !joined) {
                return std::unexpected(joined.error());
            }
        }
        auto copied = CopyVariantIntoAssets(PathOf(variant.path), PathOf(variant.provenancePath), assetsDir, sprite, plan);
        if (!copied) {
            return std::unexpected(copied.error());
        }

        auto& files = CoreManager::GetFileSystem();
        auto resolve = [&files](StringView path) { return files.ResolvePath(path); };
        const AtlasIo io{assetsDir, resolve};
        const auto save = [] { return SaveGameLibraryForAccept(); };
        auto stored = plan.atlas.empty() ? AcceptIntoLibrary(library, *copied, sprite, io, save) : AcceptIntoGroup(library, *copied, sprite, StringView(plan.atlas), plan.deferPack, io, save);
        if (!stored) {
            return std::unexpected(stored.error());
        }

        if (stored->repacked) {
            NoteAtlasRewrittenForAccept();
        }

        const PoolString name = PoolString::Intern(sprite);
        const PoolString texturePath = PoolString::Intern(copied->texturePath);
        // The old pictures of these paths may be cached: drop them so the reload reads the new files
        auto& resources = CoreManager::GetResourceManager();
        EvictAcceptedTextures(*copied, stored->repacked, [&resources](StringView path) { return resources.Evict(path); });
        ArtGenAccepted accepted;
        accepted.copied = std::move(*copied);
        accepted.repacked = std::move(stored->repacked);
        accepted.atlasGroup = plan.atlas;
        if (Scene* scene = CoreManager::GetSceneManager().GetActiveScene()) {
            accepted.reloadedSprites = ReloadSpritesUsing(scene->GetRootNode(), name, texturePath);
            if (accepted.repacked) {
                // The accepted sprite is reloaded above; its neighbours moved in the atlas too
                for (const PoolString member : accepted.repacked->changed) {
                    if (member != name) {
                        accepted.reloadedSprites += ReloadSpritesUsing(scene->GetRootNode(), member, PoolString{});
                    }
                }
            }
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

    std::filesystem::path ProvenanceFileOf(const std::filesystem::path& mediaFile) {
        std::filesystem::path file = mediaFile;
        return file.replace_extension(PathOf(kProvenanceExtension));
    }

    std::expected<void, String> MarkProvenanceKept(const std::filesystem::path& file, int64_t seed, StringView date) {
        auto text = ReadTextFile(file);
        if (!text) {
            return std::unexpected(Format("cannot read '{}': {}", file.generic_string(), text.error()));
        }
        auto parsed = ParseJson(StringView(*text));
        if (!parsed || !parsed->is_object()) {
            return std::unexpected(Format("'{}' is not a JSON object", file.generic_string()));
        }
        Json kept = Json::object();
        kept["seed"] = seed;
        kept["date"] = ToStdString(date);
        (*parsed)["kept"] = std::move(kept);
        return WriteProvenance(file, *parsed);
    }

    std::expected<ArtGenImageFrame, String> MeasureFrameForControl(const Vector<String>& pngFiles, const ArtGenImageSettings& settings) {
        Vector<ArtGenIntRect> bounds;
        int32_t width = 0;
        int32_t height = 0;
        for (const String& file : pngFiles) {
            const std::filesystem::path path = PathOf(StringView(file));
            auto pixels = ReadArtGenPng(path);
            if (!pixels) {
                return std::unexpected(pixels.error());
            }
            const auto pixelsWidth = static_cast<int32_t>(pixels->width);
            const auto pixelsHeight = static_cast<int32_t>(pixels->height);
            if (width == 0) {
                width = pixelsWidth;
                height = pixelsHeight;
            } else if (pixelsWidth != width || pixelsHeight != height) {
                return std::unexpected(
                    Format("'{}' is {} x {}, the others are {} x {}: one frame cannot cut pictures of different sizes", path.generic_string(), pixelsWidth, pixelsHeight, width, height));
            }
            if (const Optional<ArtGenIntRect> drawn = DrawnBounds(*pixels)) {
                bounds.push_back(*drawn);
            }
        }
        if (bounds.empty()) {
            return std::unexpected(String("nothing drawn in any of the pictures: they are empty"));
        }
        return MakeArtGenFrame(Span<const ArtGenIntRect>(bounds.data(), bounds.size()), width, height, settings);
    }

    std::expected<void, ControlError> KeepForControl(StringView kind, StringView name, int64_t seed, StringView date) {
        auto fail = [](const String& message) { return std::unexpected(ControlError{String("artgen_accept_failed"), message}); };
        const bool image = kind == kKindImage;
        if (!image && kind != kKindAudio) {
            return fail(Format("'{}' is not a kind of art (image or audio)", kind));
        }
        if (!IsValidSpriteName(name)) {
            return fail(Format("'{}' is not a sprite or sound name", name));
        }
        const std::filesystem::path assetsDir = CoreManager::GetFileSystem().GetMountedPath(MountPoints::assets);
        if (assetsDir.empty()) {
            return fail(String("the game has no assets folder"));
        }
        const std::filesystem::path media =
            assetsDir / ToStd(kArtGenAssetsFolder) / PathOf(Format("{}{}", name, image ? kPngExtension : kWavExtension));  //-NoStd: std::filesystem::path takes a std::string
        if (auto marked = MarkProvenanceKept(ProvenanceFileOf(media), seed, date); !marked) {
            return fail(marked.error());
        }
        return {};
    }

    std::expected<ArtGenAcceptAnswer, ControlError> AcceptForControl(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan, SceneNode* node) {
        auto fail = [](const String& message) { return std::unexpected(ControlError{String("artgen_accept_failed"), message}); };
        auto accepted = AcceptVariant(variant, sprite, plan);
        if (!accepted) {
            return fail(accepted.error());
        }
        ArtGenAcceptAnswer reply;
        reply.sprite = String(sprite);
        reply.texturePath = accepted->copied.texturePath;
        reply.png = String(Format("{}", accepted->copied.pngFile.generic_string()));
        reply.width = accepted->copied.width;
        reply.height = accepted->copied.height;
        reply.processing = MakeArtGenProcessingAnswer(accepted->copied);
        reply.reloadedSprites = accepted->reloadedSprites;
        AddAtlasKeysToReply(reply, StringView(accepted->atlasGroup), accepted->repacked);
        if (node != nullptr) {
            if (auto used = UseSpriteOn(*node, PoolString::Intern(sprite)); !used) {
                return fail(Format("accepted as '{}', but: {}", sprite, used.error()));
            }
            reply.usedOn = String(node->GetName().ToStringView());
            SceneEditedEvent::Emit(CoreManager::GetSceneManager().GetActiveScene());
        }
        return reply;
    }

}  // namespace BECore
