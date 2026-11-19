#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/Json.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <Widgets/ArtGen/ArtGenSound.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace BECore {

    class SceneNode;
    struct ArtGenVariant;

    /// Where accepted images go under the game's assets: `assets/<kArtGenAssetsFolder>/<sprite>.png`
    inline constexpr StringView kArtGenAssetsFolder = "artgen";
    inline constexpr size_t kMaxSpriteNameLength = 64;

    /// A sprite name the panel accepts: 1 to kMaxSpriteNameLength of `A-Z a-z 0-9 _ -` (also the file name, so no path can be smuggled in)
    [[nodiscard]] bool IsValidSpriteName(StringView name);

    /// The virtual path Accept writes for @p name: `assets/artgen/<name>.png` for kind `image`, `.wav` for `audio`. The name is not checked here
    [[nodiscard]] String ArtGenGameFile(StringView name, StringView kind);

    /// The pixel size from the IHDR chunk of a PNG file, or empty when the file is not a PNG
    [[nodiscard]] Optional<Pair<uint32_t, uint32_t>> ReadPngSize(const std::filesystem::path& file);

    /// What CopyVariantIntoAssets wrote
    struct ArtGenCopied {
        String texturePath;  // the virtual path a TextureLibrary entry names: "assets/artgen/<sprite>.png"
        std::filesystem::path pngFile;
        std::filesystem::path provenanceFile;
        uint32_t width = 0;  // the written file's size
        uint32_t height = 0;
        ArtGenIntRect crop;  // the part of the source that was kept
        double scale = 1.0;  // written size over the crop's
        ArtGenImageSettings settings;
        bool fromSet = false;  // the crop came from the frame of a set, not from this image alone
    };

    /**
     * @brief Writes a generated PNG and its provenance file into @p assetsDir (the game's top `assets` layer) as `artgen/<sprite>.png` and
     * `artgen/<sprite>.artgen.json`, replacing an earlier accept of the same name. No services: the caller names the folder.
     *
     * With an identity plan both files are copied byte for byte. Otherwise the PNG is cut and scaled by the plan's frame (or, with none, by
     * the frame over this image's own drawn box), and the provenance is the generation's record plus `accepted`. A picture with nothing drawn,
     * or one the plan's frame would cut, is an error and nothing is written.
     *
     * A variant with no provenance file (@p provenance empty or missing) leaves none of an earlier accept of the name: the identity plan
     * removes it (a missing file is fine, a failed removal is an error), the other plans write a record with `accepted` only.
     */
    std::expected<ArtGenCopied, String> CopyVariantIntoAssets(const std::filesystem::path& png, const std::filesystem::path& provenance, const std::filesystem::path& assetsDir, StringView sprite,
                                                              const ArtGenImagePlan& plan);

    /**
     * @brief The cache step of an accept: calls @p evict with the picture's path and, for a repacked sprite, the atlas's path. @p evict is
     * `ResourceManager::Evict` in the editor; a holder of a texture (the Sprite Library, a canvas image) finds the evict with
     * `IsEvictedHandle`, and the test that ties the two is this function's.
     */
    void EvictAcceptedTextures(const ArtGenCopied& copied, const Optional<AtlasWritten>& repacked, FunctionRef<bool(StringView)> evict);

    /// Calls ReloadTexture on every SpriteRendererComponent under @p root that names @p sprite or @p texturePath; returns how many
    uint32_t ReloadSpritesUsing(SceneNode& root, PoolString sprite, PoolString texturePath);

    /**
     * @brief The entry Accept stores for @p sprite once its picture is the whole PNG at @p texturePath of @p width x @p height.
     *
     * A sprite with no entry or no `atlas` link gets the whole image. One in an atlas group keeps its link (group and pack settings):
     * unpacked, it points to the new file; packed, it keeps its place in the atlas (texture and rect) and only its `atlas.source` becomes
     * the new file, so the repack cuts the new picture. No files, no services.
     */
    [[nodiscard]] SpriteEntry AcceptedSpriteEntry(const TextureLibrary& library, PoolString sprite, PoolString texturePath, uint32_t width, uint32_t height);

    /**
     * @brief Puts @p entry into @p library and calls @p save once; an entry the library already holds is left alone and nothing is saved, so a
     * switch or re-accept of an unchanged sprite does not rewrite TextureLibrary.xml.
     *
     * @return Whether it saved. When @p save fails the library goes back to what it was and the answer is the reason.
     */
    std::expected<bool, String> StoreSpriteEntry(TextureLibrary& library, const SpriteEntry& entry, FunctionRef<bool()> save);

    /// What AcceptIntoLibrary did
    struct AcceptStored {
        bool saved = false;               // the library was saved (once for the entry, once more when the repack moved members)
        Optional<AtlasWritten> repacked;  // engaged when the sprite was packed and its group was repacked
    };

    /**
     * @brief The library side of Accept of the PNG @p copied as @p sprite: stores `AcceptedSpriteEntry` and, for a packed sprite, repacks its
     * group with the settings the group was packed with (`PackSettingsOf`) and saves again when the repack moved a member. No services.
     *
     * A sprite with no atlas link is only stored, as StoreSpriteEntry does. A repack that fails leaves the new `atlas.source` in the saved
     * library and the old atlas file as it was (the error says the PNG was already written over the game's file and Pack in the Sprite Library finishes it). When the
     * second save fails the members go back to what the library held before the repack; the atlas file is already replaced then, and the
     * error says so.
     */
    std::expected<AcceptStored, String> AcceptIntoLibrary(TextureLibrary& library, const ArtGenCopied& copied, StringView sprite, const AtlasIo& io, FunctionRef<bool()> save);

    /**
     * @brief Several accepts that owe the library one save, not one each (Accept all of a set of images).
     *
     * The constructor copies the library's entries. `Defer` is the `save` of AcceptIntoLibrary and StoreSpriteEntry: it notes that a save is
     * owed and says it went through, so the library is only changed in memory. `Commit` then saves once. When that save fails the library
     * goes back to the copy (entries the batch added are removed, entries it replaced or repacked come back): a snapshot, because a repack
     * changes several members and per-row undo would have to know all of them. No services: the caller gives the save.
     *
     * The library must outlive the batch and not be changed by anything but the accepts between the constructor and Commit.
     */
    class ArtGenLibraryBatch {
    public:
        explicit ArtGenLibraryBatch(TextureLibrary& library);

        /// @return true: the save is only owed
        bool Defer() {
            _owed = true;
            return true;
        }

        /// A row rewrote an atlas file on disk; a failed Commit then says the old member rects no longer match it (Pack in the Sprite Library repairs that)
        void NoteRepacked() {
            _repacked = true;
        }

        /// @return false and nothing saved when no accept changed the library; true after one successful @p save; else the reason, with the library as it was at construction
        std::expected<bool, String> Commit(FunctionRef<bool()> save);

    private:
        TextureLibrary& _library;
        Vector<SpriteEntry> _snapshot;
        bool _owed = false;
        bool _repacked = false;
    };

    /// What `artgen_accept` answers as `atlas`: `null` when no atlas was written, else `{name, texturePath, width, height, members}`
    [[nodiscard]] Json AtlasReplyToJson(const Optional<AtlasWritten>& repacked);

    /// What AcceptVariant did
    struct ArtGenAccepted {
        ArtGenCopied copied;
        uint32_t reloadedSprites = 0;
        Optional<AtlasWritten> repacked;  // the atlas of a packed sprite, written again
        String atlasGroup;                // the group the plan named (`ArtGenImagePlan::atlas`); empty: none
    };

    /**
     * @brief Accept of the Generate panel and of `artgen_accept`: the variant becomes the game's sprite @p sprite.
     *
     * Copies it into the game's assets (CopyVariantIntoAssets with @p plan), adds or replaces the TextureLibrary entry (the whole written image; a
     * sprite of an atlas group keeps its link, and a packed one has its atlas repacked, see AcceptIntoLibrary) and saves the library, drops the old
     * textures of the paths from the resource cache and reloads every sprite of the active scene that uses them, so the new picture shows at once. Fails without changing the library when the game has
     * no assets folder or no TextureLibrary.xml. While an accept batch is open (BeginAcceptBatch) the library is changed in memory only and
     * EndAcceptBatch saves it.
     *
     * With `plan.atlas` the sprite joins that group and the group is packed (`AcceptIntoGroup`; not packed with `plan.deferPack`, the caller
     * packs once later). A packed member of another group is refused before anything is written.
     */
    std::expected<ArtGenAccepted, String> AcceptVariant(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan);

    /**
     * @brief Opens the editor's accept batch: the AcceptVariant calls until EndAcceptBatch save the game's TextureLibrary.xml once, at the end.
     * A Begin while one is open is a bug (it asserts and changes nothing).
     *
     * The batch is one file-local object, not a parameter, because the session's acceptors are plain function pointers that carry no state;
     * the editor is single-threaded, so there is never more than one.
     */
    void BeginAcceptBatch();

    /**
     * @brief Closes the batch BeginAcceptBatch opened, saving the game's library once when an accept changed it; ok when none is open.
     *
     * A failed save puts the library back to what it was at Begin and is `artgen_accept_failed`: the PNGs of the batch are already in the
     * game's assets, the library keeps its old entries.
     */
    std::expected<void, ControlError> EndAcceptBatch();

    /// Puts the sprite on @p node's SpriteRendererComponent and reloads it; fails when the node has none
    std::expected<void, String> UseSpriteOn(SceneNode& node, PoolString sprite);

    /// Where an accepted sound goes: `assets/artgen/<name>.wav` (a sprite name's rules), or over the game's clip at `clip`, a fixed path its
    /// code plays (`assets/sounds/move.wav`); exactly one of them
    struct ArtGenSoundTarget {
        String name;
        String clip;
    };

    /// A clip path Accept may write: `assets/<folders>/<file>.wav`, each part of `A-Z a-z 0-9 _ - .` and none `..`, so nothing leaves the
    /// game's assets
    [[nodiscard]] bool IsValidClipPath(StringView clip);

    /// What WriteSoundIntoAssets wrote
    struct ArtGenSoundWritten {
        String clipPath;  // the virtual path a game plays: "assets/..."
        std::filesystem::path wavFile;
        std::filesystem::path provenanceFile;
        AudioFormat format;
        double seconds = 0.0;
        float peakDb = 0.0f;
    };

    /**
     * @brief Decodes the generated sound @p sourceFile, processes it (ProcessArtGenSound with @p settings) and writes it as a 16-bit WAV into
     * @p assetsDir (the game's top `assets` layer) at @p target, with `<file>.artgen.json`: the generation's provenance plus `accepted`
     * (the settings, the source file, the result). No services: the caller names the folder.
     */
    std::expected<ArtGenSoundWritten, String> WriteSoundIntoAssets(const std::filesystem::path& sourceFile, const std::filesystem::path& provenance, const std::filesystem::path& assetsDir,
                                                                   const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings);

    /// Calls ReloadClip on every AudioSourceComponent under @p root whose clip is @p clip; returns how many
    uint32_t ReloadAudioSourcesUsing(SceneNode& root, PoolString clip);

    /**
     * @brief The editor's ArtGenSession::SoundAcceptor: WriteSoundIntoAssets into the game's assets, the old clip of that path evicted from
     * the resource cache and every AudioSourceComponent of the active scene that names it reloaded; with @p node, the clip put on its
     * AudioSourceComponent (the scene editor is told the scene changed).
     *
     * Answers `{clip, wav, sampleRate, channels, seconds, peakDb, reloadedSources, usedOn}`; a failure is `artgen_accept_failed`.
     */
    std::expected<Json, ControlError> AcceptSoundForControl(const ArtGenVariant& variant, const ArtGenSoundTarget& target, const ArtGenSoundSettings& settings, SceneNode* node);

    /**
     * @brief The editor's ArtGenSession::Acceptor: AcceptVariant, then UseSpriteOn @p node when one is given (the scene editor is told the scene changed).
     *
     * Answers `{sprite, texturePath, png, width, height, processing, reloadedSprites, atlasGroup, atlas, usedOn}` (the size is the written file's;
     * `processing` is `{trim, padding, maxSize, crop, scale, size, frame}`; `atlasGroup` the group the sprite joined or null, `atlas` the packed
     * file or null, also while a pack is deferred); a failure is `artgen_accept_failed` with the reason.
     */
    std::expected<Json, ControlError> AcceptForControl(const ArtGenVariant& variant, StringView sprite, const ArtGenImagePlan& plan, SceneNode* node);

    /**
     * @brief The editor's ArtGenSession::FrameMeasurer: decodes each of @p pngFiles one at a time (a 1024 square is about 4 MB, freed before the
     * next) and makes the frame over the union of their drawn boxes with MakeArtGenFrame. A picture with nothing drawn adds nothing; an error
     * when none has anything drawn, a file cannot be read or the sizes differ (one frame cannot cut pictures of different sizes).
     */
    std::expected<ArtGenImageFrame, String> MeasureFrameForControl(const Vector<String>& pngFiles, const ArtGenImageSettings& settings);

    /// The provenance file beside a game's image or clip: the file's name with its extension replaced by `.artgen.json`
    [[nodiscard]] std::filesystem::path ProvenanceFileOf(const std::filesystem::path& mediaFile);

    /// Adds `"kept": {"seed", "date"}` to the provenance file @p file (replacing an earlier one), the rest of it as it was, written atomically;
    /// a missing or unreadable file is an error that names it
    std::expected<void, String> MarkProvenanceKept(const std::filesystem::path& file, int64_t seed, StringView date);

    /**
     * @brief The editor's ArtGenSession::Keeper: MarkProvenanceKept on the game's file of the sprite or clip @p name (@p kind `image` or
     * `audio`) under the top `assets` layer. A failure is `artgen_accept_failed` with the reason.
     */
    std::expected<void, ControlError> KeepForControl(StringView kind, StringView name, int64_t seed, StringView date);

}  // namespace BECore
