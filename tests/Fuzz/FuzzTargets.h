#pragma once

#include <BECore/Save/SaveGame.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore::Fuzz {

    /// One fuzz target: untrusted bytes in, properties checked inside, a process that dies (FUZZ_CHECK, a sanitizer, an engine assertion) on a finding
    using FuzzFunction = void (*)(const uint8_t* data, size_t size);

    struct FuzzTarget {
        const char* name;          ///< The suffix of the libFuzzer executable (BagiFuzz<name>) and of the test case
        FuzzFunction function;     ///< Fuzz<name>
        const char* corpusFolder;  ///< The folder of tests/Fuzz/corpus with the seeds of this target
        uint32_t costWeight;       ///< How many times dearer than the binary reader one input is: the corpus test divides its default iteration count by it
    };

    /// Every target, in a fixed order; the corpus test and the libFuzzer wrappers (cmake/BagiFuzz.cmake) use the same functions
    Span<const FuzzTarget> FuzzTargetList();

    /// Prints the failed condition and aborts the process: the corpus test and a libFuzzer run both see a crash
    [[noreturn]] void FuzzCheckFailed(const char* condition, const char* file, int line);

    /// A property of a target. Not an engine ASSERT: it must hold in every build, asserts on or off
#define FUZZ_CHECK(condition)                                                                                                                                                                          \
    do {                                                                                                                                                                                               \
        if (!(condition)) {                                                                                                                                                                            \
            ::BECore::Fuzz::FuzzCheckFailed(#condition, __FILE__, __LINE__);                                                                                                                           \
        }                                                                                                                                                                                              \
    } while (false)

    /**
     * @brief The same bytes under a header that is true (magic, version, payload size)
     *
     * Any change of the length breaks the payload size of a binary stream, and the load stops at the header, before the checks that matter: a target
     * that reads a binary stream feeds the repaired bytes too. Empty when there is no room for a header.
     */
    Vector<uint8_t> WithTrueBinaryHeader(const uint8_t* data, size_t size);

    // =============================================================================
    // The targets (one file each, tests/Fuzz/Fuzz<Name>.cpp)
    // =============================================================================

    /// BinaryDeserializer::LoadFromBuffer, then every read over the whole document, then the same buffer again
    void FuzzBinaryDeserializer(const uint8_t* data, size_t size);

    /// XmlDeserializer::LoadFromString, lenient and strict: every read over the whole document, strict refusals against a grammar of the values, a reflected probe type
    void FuzzXmlDeserializer(const uint8_t* data, size_t size);

    /// JsonDeserializer over a JSON document: the probe types read under every mode of diagnostics, a round trip through JsonSerializer, and the document as the arguments of CallMethod
    void FuzzJsonDeserializer(const uint8_t* data, size_t size);

    /// SaveGameService on a folder of the process: the bytes as a slot file, Peek and Load of a reflected state
    void FuzzSaveEnvelope(const uint8_t* data, size_t size);

    /// The text of a scene node file and of a scene (split at the first NUL byte) through the expansion of node references and the deserialization of the components
    void FuzzSceneNode(const uint8_t* data, size_t size);

    /// WebSocketFrameParser over a stream of frames (the first byte picks the sender, the message limit and the chunking): chunked and whole agree, limits hold, frames re-encoded for the other role
    /// read back
    void FuzzWebSocketFrame(const uint8_t* data, size_t size);

    /// The opening handshake from both sides (the first byte picks the policy): a request head, a response head and a URL, with the properties that tie them together
    void FuzzHandshake(const uint8_t* data, size_t size);

    /// The messages of the room protocol, both directions: size limits, round trips through the builders, stable refusal codes, and the player name sanitizer
    void FuzzRoomProtocol(const uint8_t* data, size_t size);

    /// A RoomHub over the demo card game fed with a stream of operations (message, connect, disconnect, tick): every text it sends is judged (JSON, tokens, hidden cards), the counts stay in the
    /// limits
    void FuzzRoomHub(const uint8_t* data, size_t size);

    /// ControlService::Post of the bytes with the builtins and the game methods registered: one answer per request, with the request's id
    void FuzzControlRequest(const uint8_t* data, size_t size);

    /// The web-style UI front end: `html, NUL, css` through UIWebCompiler::Compile (twice), the report judged, the compiled XML read by XmlDeserializer and UIScreenContent
    void FuzzWebUi(const uint8_t* data, size_t size);

    /// UIStrings::LoadFromXml and UIThemes::LoadFromXml over an XML text (the root picks the table): the complaints, what the table holds, loading twice
    void FuzzUiTables(const uint8_t* data, size_t size);

    /// The numbers of a content file in the pure UI geometry (box mesh, nine-slice, rect and stack layout, canvas scale and layout), as raw float bits: bounded results, no NaN or infinity
    void FuzzUiGeometry(const uint8_t* data, size_t size);

    /// DecodeWav (BECore/Audio): every RIFF WAVE file that decodes has a sound the engine plays (channels, rate, finite samples), the same twice; a refusal says why
    void FuzzAudioWav(const uint8_t* data, size_t size);

    /// DecodeFlac (BECore/Audio, the vendored dr_flac) with a frame limit: a sound the engine plays or a refusal that says why, the same twice, the same
    /// samples read as a stream, and a lossless round trip through EncodeWav
    void FuzzAudioFlac(const uint8_t* data, size_t size);

    /// The schema FuzzSaveEnvelope loads with; the save seeds are named after its versions (current, oldest, one outside each end)
    inline constexpr SaveSchema kFuzzSaveSchema{"FuzzGame", 3, 2};

    /// A save file as the seeds make it: the bytes SaveGameService::Save writes for the probe state under the given game id and schema version
    Vector<uint8_t> MakeSaveFileBytes(StringView game, uint32_t version);

}  // namespace BECore::Fuzz
