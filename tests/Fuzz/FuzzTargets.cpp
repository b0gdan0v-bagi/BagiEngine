#include "FuzzTargets.h"

#include <BECore/Reflection/BinaryFormat.h>
#include <cstdio>
#include <cstdlib>

namespace BECore::Fuzz {

    Span<const FuzzTarget> FuzzTargetList() {
        static const FuzzTarget targets[] = {
            {"BinaryDeserializer", &FuzzBinaryDeserializer, "binary", 1},
            {"BinaryContent", &FuzzBinaryContent, "binary_content", 1},
            {"XmlDeserializer", &FuzzXmlDeserializer, "xml", 2},
            {"JsonDeserializer", &FuzzJsonDeserializer, "json", 5},
            {"SaveEnvelope", &FuzzSaveEnvelope, "save", 10},
            {"SceneNode", &FuzzSceneNode, "scene", 1},
            {"WebSocketFrame", &FuzzWebSocketFrame, "ws_frame", 1},
            {"Handshake", &FuzzHandshake, "ws_handshake", 1},
            {"RoomProtocol", &FuzzRoomProtocol, "room_protocol", 1},
            {"RoomHub", &FuzzRoomHub, "room_hub", 15},
            {"ControlRequest", &FuzzControlRequest, "control_request", 1},
            {"WebUi", &FuzzWebUi, "web_ui", 15},
            {"UiTables", &FuzzUiTables, "ui_tables", 2},
            {"PanelAsset", &FuzzPanelAsset, "panel_asset", 4},
            {"UiGeometry", &FuzzUiGeometry, "ui_geometry", 8},
            {"AudioWav", &FuzzAudioWav, "audio_wav", 1},
            {"AudioFlac", &FuzzAudioFlac, "audio_flac", 1},
        };
        return targets;
    }

    Vector<uint8_t> WithTrueBinaryHeader(const uint8_t* data, size_t size) {
        if (size < BinaryFormat::HeaderSize) {
            return {};
        }
        Vector<uint8_t> repaired(data, data + size);
        std::memcpy(repaired.data(), BinaryFormat::Magic, sizeof(BinaryFormat::Magic));
        // An input of a version the reader knows keeps it, so its seeds keep exercising that layout under a true header
        const uint16_t version = BinaryFormat::Load<uint16_t>(data + sizeof(BinaryFormat::Magic));
        const bool known = version == BinaryFormat::LegacyVersion || version == BinaryFormat::TabledVersion || version == BinaryFormat::PooledVersion;
        BinaryFormat::Store<uint16_t>(repaired.data() + sizeof(BinaryFormat::Magic), known ? version : BinaryFormat::CurrentVersion);
        BinaryFormat::Store<uint32_t>(repaired.data() + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(size - BinaryFormat::HeaderSize));
        return repaired;
    }

    void FuzzCheckFailed(const char* condition, const char* file, int line) {
        std::fprintf(stderr, "FUZZ_CHECK failed: %s (%s:%d)\n", condition, file, line);
        std::fflush(stderr);
        std::abort();
    }

}  // namespace BECore::Fuzz
