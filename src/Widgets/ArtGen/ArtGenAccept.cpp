#include "ArtGenAccept.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <Widgets/ArtGen/ArtGenSession.h>
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
