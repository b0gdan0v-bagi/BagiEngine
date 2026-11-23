#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <cstdint>

namespace BECore {

    // The answers of the `artgen_*` control methods, written by the generated `WriteJson` (`ControlCall::ReplyObject`). What stays a `Json` document by hand and why:
    // src/Widgets/ArtGen/CLAUDE.md, "The contract". An empty `Optional` writes no member, so a member that is absent never means anything else.

    /// `artgen_progress`, `artgen_image`, `artgen_done` and `artgen_setup_report`: `stale` is true when the report is not for the run or the setup in work
    struct ArtGenStaleAnswer {
        BE_CLASS(ArtGenStaleAnswer)

        BE_REFLECT_FIELD bool stale = false;
    };

    /// `artgen_generate`: the id of the request that was queued
    struct ArtGenQueuedAnswer {
        BE_CLASS(ArtGenQueuedAnswer)

        BE_REFLECT_FIELD uint32_t id = 0;
        BE_REFLECT_FIELD bool serviceConnected = false;
    };

    /// `artgen_setup`: `{}` for the cancel of the action in work, else the id of the action that was queued
    struct ArtGenSetupQueuedAnswer {
        BE_CLASS(ArtGenSetupQueuedAnswer)

        BE_REFLECT_FIELD Optional<uint32_t> id;
        BE_REFLECT_FIELD Optional<bool> serviceConnected;
    };

    /// `artgen_save_preset`
    struct ArtGenSavedAnswer {
        BE_CLASS(ArtGenSavedAnswer)

        BE_REFLECT_FIELD String path;
    };

    /// `artgen_cancel`: `{}` for the run, `{job, set, state}` for a job of a set and `{set, state}` for a whole set
    struct ArtGenCancelAnswer {
        BE_CLASS(ArtGenCancelAnswer)

        BE_REFLECT_FIELD Optional<String> job;
        BE_REFLECT_FIELD Optional<String> set;
        BE_REFLECT_FIELD Optional<String> state;
    };

    /// The reference picture of a run or a set: the same keys and values as `ArtGenReferenceToJson` writes into a preset or a manifest file (a test holds them equal);
    /// the file keeps its own writer because it must read back what it wrote and keeps `null` where this answer leaves a member out
    struct ArtGenReferenceAnswer {
        BE_CLASS(ArtGenReferenceAnswer)

        BE_REFLECT_FIELD String image;
        BE_REFLECT_FIELD String mode;
        BE_REFLECT_FIELD Optional<double> weight;
    };

    [[nodiscard]] ArtGenReferenceAnswer MakeArtGenReferenceAnswer(const ArtGenReference& reference);

    /// The atlas an accept packed: the `atlas` of `artgen_accept` and of `artgen_accept_set`, and the whole answer of the `AtlasPacker` seam
    struct ArtGenAtlasAnswer {
        BE_CLASS(ArtGenAtlasAnswer)

        BE_REFLECT_FIELD uint32_t height = 0;
        BE_REFLECT_FIELD uint64_t members = 0;
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String texturePath;
        BE_REFLECT_FIELD uint32_t width = 0;
    };

    /// The part of the source picture an accept kept
    struct ArtGenCropAnswer {
        BE_CLASS(ArtGenCropAnswer)

        BE_REFLECT_FIELD int32_t h = 0;
        BE_REFLECT_FIELD int32_t w = 0;
        BE_REFLECT_FIELD int32_t x = 0;
        BE_REFLECT_FIELD int32_t y = 0;
    };

    /// The `processing` of an image accept: the settings it was made with (`ArtGenImageSettingsToJson`'s keys) and the placement of the written
    /// picture (`crop`, `scale`, `size` as `[width, height]`, `frame`: `set` when the crop came from the frame of a set, else `sprite`). The files
    /// keep their writers (they read back what they wrote): a test holds the two equal
    struct ArtGenProcessingAnswer {
        BE_CLASS(ArtGenProcessingAnswer)

        BE_REFLECT_FIELD ArtGenCropAnswer crop;
        BE_REFLECT_FIELD String frame;
        BE_REFLECT_FIELD int32_t maxSize = 0;
        BE_REFLECT_FIELD int32_t padding = 0;
        BE_REFLECT_FIELD double scale = 1.0;
        BE_REFLECT_FIELD Vector<uint32_t> size;
        BE_REFLECT_FIELD bool trim = false;
    };

    /**
     * @brief What an accept of an image or a sound answered: `artgen_accept`, the `switched` of `artgen_pick` and, as the parent of a row,
     * `artgen_accept_set`. One flat struct, every member `Optional`, because the two kinds share nothing but `usedOn`: exactly one group is set.
     * An image has `sprite, texturePath, png, width, height, processing, reloadedSprites, atlasGroup, atlas`; a sound has `clip, wav, sampleRate,
     * channels, seconds, peakDb, reloadedSources`. `usedOn` (the node's name) is present only when the accept put it on a node.
     * `atlasGroup` is the group the sprite joined and `atlas` the file that was packed; `atlas` stays absent while a pack is deferred.
     */
    struct ArtGenAcceptAnswer {
        BE_CLASS(ArtGenAcceptAnswer)

        BE_REFLECT_FIELD Optional<ArtGenAtlasAnswer> atlas;
        BE_REFLECT_FIELD Optional<String> atlasGroup;
        BE_REFLECT_FIELD Optional<uint32_t> channels;
        BE_REFLECT_FIELD Optional<String> clip;
        BE_REFLECT_FIELD Optional<uint32_t> height;
        BE_REFLECT_FIELD Optional<double> peakDb;
        BE_REFLECT_FIELD Optional<String> png;
        BE_REFLECT_FIELD Optional<ArtGenProcessingAnswer> processing;
        BE_REFLECT_FIELD Optional<uint32_t> reloadedSources;
        BE_REFLECT_FIELD Optional<uint32_t> reloadedSprites;
        BE_REFLECT_FIELD Optional<uint32_t> sampleRate;
        BE_REFLECT_FIELD Optional<double> seconds;
        BE_REFLECT_FIELD Optional<String> sprite;
        BE_REFLECT_FIELD Optional<String> texturePath;
        BE_REFLECT_FIELD Optional<String> usedOn;
        BE_REFLECT_FIELD Optional<String> wav;
        BE_REFLECT_FIELD Optional<uint32_t> width;
    };

    /// A row of `artgen_accept_set`: `job` always. A row written has the acceptor's members, `as` (the name it was accepted as), `seed` and
    /// `alternatives`; a row that was left as it is has `kept` or `replacedBy` and none of those
    struct ArtGenAcceptedRow : ArtGenAcceptAnswer {
        BE_CLASS(ArtGenAcceptedRow)

        BE_REFLECT_FIELD Optional<int32_t> alternatives;
        BE_REFLECT_FIELD Optional<String> as;
        BE_REFLECT_FIELD String job;
        BE_REFLECT_FIELD Optional<bool> kept;
        BE_REFLECT_FIELD Optional<String> replacedBy;
        BE_REFLECT_FIELD Optional<int64_t> seed;
    };

    /// `artgen_accept_set`: `atlas` is the pack after the rows, absent when none was made
    struct ArtGenAcceptSetAnswer {
        BE_CLASS(ArtGenAcceptSetAnswer)

        BE_REFLECT_FIELD Vector<ArtGenAcceptedRow> accepted;
        BE_REFLECT_FIELD Optional<ArtGenAtlasAnswer> atlas;
        BE_REFLECT_FIELD String set;
    };

    /// `artgen_pick`: `switched` is the acceptor's answer when the pick switched a live row in the game
    struct ArtGenPickAnswer {
        BE_CLASS(ArtGenPickAnswer)

        BE_REFLECT_FIELD String job;
        BE_REFLECT_FIELD int64_t pick = 0;
        BE_REFLECT_FIELD String set;
        BE_REFLECT_FIELD Optional<ArtGenAcceptAnswer> switched;
    };

    /// A preset of the catalog the service sent; a broken one has its `error`
    struct ArtGenPresetRowAnswer {
        BE_CLASS(ArtGenPresetRowAnswer)

        BE_REFLECT_FIELD Optional<String> error;
        BE_REFLECT_FIELD String kind;
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String source;
        BE_REFLECT_FIELD String title;
    };

    /// A workflow of the catalog; a broken one has its `error`
    struct ArtGenWorkflowRowAnswer {
        BE_CLASS(ArtGenWorkflowRowAnswer)

        BE_REFLECT_FIELD Optional<String> error;
        BE_REFLECT_FIELD String kind;
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String source;
    };

    /// The `service` of `artgen_state`: what the generation service last said in a poll
    struct ArtGenServiceAnswer {
        BE_CLASS(ArtGenServiceAnswer)

        BE_REFLECT_FIELD bool connected = false;
        BE_REFLECT_FIELD String generator;
        BE_REFLECT_FIELD uint64_t polls = 0;
        BE_REFLECT_FIELD Vector<ArtGenPresetRowAnswer> presets;
        BE_REFLECT_FIELD String reason;
        BE_REFLECT_FIELD bool startedHere = false;
        BE_REFLECT_FIELD String state;
        BE_REFLECT_FIELD Vector<ArtGenWorkflowRowAnswer> workflows;
    };

    /// An image or a sound of the run in work
    struct ArtGenVariantAnswer {
        BE_CLASS(ArtGenVariantAnswer)

        BE_REFLECT_FIELD String kind;
        BE_REFLECT_FIELD String path;
        BE_REFLECT_FIELD String provenance;
        BE_REFLECT_FIELD int64_t seed = 0;
    };

    /// The `run` of `artgen_state`: no `seed` when the request drew none, no `reference` without one
    struct ArtGenRunAnswer {
        BE_CLASS(ArtGenRunAnswer)

        BE_REFLECT_FIELD bool cancelRequested = false;
        BE_REFLECT_FIELD int32_t count = 0;
        BE_REFLECT_FIELD bool draft = false;
        BE_REFLECT_FIELD String error;
        BE_REFLECT_FIELD uint32_t id = 0;
        BE_REFLECT_FIELD int32_t image = 0;
        BE_REFLECT_FIELD String preset;
        BE_REFLECT_FIELD Optional<ArtGenReferenceAnswer> reference;
        BE_REFLECT_FIELD Optional<int64_t> seed;
        BE_REFLECT_FIELD String state;
        BE_REFLECT_FIELD int32_t step = 0;
        BE_REFLECT_FIELD int32_t steps = 0;
        BE_REFLECT_FIELD String subject;
        BE_REFLECT_FIELD Vector<ArtGenVariantAnswer> variants;
    };

    /// A step of the plan of a setup action
    struct ArtGenSetupStepAnswer {
        BE_CLASS(ArtGenSetupStepAnswer)

        BE_REFLECT_FIELD String licence;
        BE_REFLECT_FIELD int64_t size = 0;
        BE_REFLECT_FIELD String state;
        BE_REFLECT_FIELD String what;
    };

    /// The `setup` of `artgen_state`: the last setup action of the generator and what the service reported for it
    struct ArtGenSetupAnswer {
        BE_CLASS(ArtGenSetupAnswer)

        BE_REFLECT_FIELD String action;
        BE_REFLECT_FIELD bool cancelRequested = false;
        BE_REFLECT_FIELD String current;
        BE_REFLECT_FIELD int64_t done = 0;
        BE_REFLECT_FIELD int64_t downloadBytes = 0;
        BE_REFLECT_FIELD String error;
        BE_REFLECT_FIELD uint32_t id = 0;
        BE_REFLECT_FIELD String log;
        BE_REFLECT_FIELD Vector<ArtGenSetupStepAnswer> plan;
        BE_REFLECT_FIELD String platform;
        BE_REFLECT_FIELD String root;
        BE_REFLECT_FIELD String state;
        BE_REFLECT_FIELD int64_t total = 0;
    };

    /// The jobs of a set by state
    struct ArtGenJobCountsAnswer {
        BE_CLASS(ArtGenJobCountsAnswer)

        BE_REFLECT_FIELD int32_t cancelled = 0;
        BE_REFLECT_FIELD int32_t done = 0;
        BE_REFLECT_FIELD int32_t failed = 0;
        BE_REFLECT_FIELD int32_t pending = 0;
        BE_REFLECT_FIELD int32_t running = 0;
    };

    /// A set of `artgen_state`: no `reference` without one, no `atlas` for loose PNGs
    struct ArtGenSetSummaryAnswer {
        BE_CLASS(ArtGenSetSummaryAnswer)

        BE_REFLECT_FIELD int32_t accepted = 0;
        BE_REFLECT_FIELD Optional<String> atlas;
        BE_REFLECT_FIELD int32_t count = 0;
        BE_REFLECT_FIELD ArtGenJobCountsAnswer jobs;
        BE_REFLECT_FIELD int32_t kept = 0;
        BE_REFLECT_FIELD String kind;
        BE_REFLECT_FIELD int32_t live = 0;
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD int32_t picked = 0;
        BE_REFLECT_FIELD String preset;
        BE_REFLECT_FIELD Optional<ArtGenReferenceAnswer> reference;
        BE_REFLECT_FIELD int64_t seed = 0;
        BE_REFLECT_FIELD String state;
    };

    /// `artgen_state`: the art generation service as the editor sees it, the run in work, the setup action and a summary of each set
    struct ArtGenStateAnswer {
        BE_CLASS(ArtGenStateAnswer)

        BE_REFLECT_FIELD String gameRoot;
        BE_REFLECT_FIELD ArtGenRunAnswer run;
        BE_REFLECT_FIELD ArtGenServiceAnswer service;
        BE_REFLECT_FIELD Vector<ArtGenSetSummaryAnswer> sets;
        BE_REFLECT_FIELD ArtGenSetupAnswer setup;
    };

}  // namespace BECore
