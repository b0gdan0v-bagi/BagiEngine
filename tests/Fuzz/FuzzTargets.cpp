#include "FuzzTargets.h"

#include <BECore/Reflection/BinaryFormat.h>
#include <cstdio>
#include <cstdlib>

namespace BECore::Fuzz {

    std::span<const FuzzTarget> FuzzTargetList() {
        static const FuzzTarget targets[] = {
            {"BinaryDeserializer", &FuzzBinaryDeserializer, "binary", 1},
            {"XmlDeserializer", &FuzzXmlDeserializer, "xml", 2},
            {"SaveEnvelope", &FuzzSaveEnvelope, "save", 10},
            {"SceneNode", &FuzzSceneNode, "scene", 1},
            {"WebSocketFrame", &FuzzWebSocketFrame, "ws_frame", 1},
            {"Handshake", &FuzzHandshake, "ws_handshake", 1},
            {"RoomProtocol", &FuzzRoomProtocol, "room_protocol", 1},
            {"RoomHub", &FuzzRoomHub, "room_hub", 15},
            {"ControlRequest", &FuzzControlRequest, "control_request", 1},
            {"WebUi", &FuzzWebUi, "web_ui", 15},
            {"UiTables", &FuzzUiTables, "ui_tables", 2},
            {"UiGeometry", &FuzzUiGeometry, "ui_geometry", 8},
        };
        return targets;
    }

    Vector<uint8_t> WithTrueBinaryHeader(const uint8_t* data, size_t size) {
        if (size < BinaryFormat::HeaderSize) {
            return {};
        }
        Vector<uint8_t> repaired(data, data + size);
        std::memcpy(repaired.data(), BinaryFormat::Magic, sizeof(BinaryFormat::Magic));
        BinaryFormat::Store<uint16_t>(repaired.data() + sizeof(BinaryFormat::Magic), BinaryFormat::CurrentVersion);
        BinaryFormat::Store<uint32_t>(repaired.data() + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(size - BinaryFormat::HeaderSize));
        return repaired;
    }

    void FuzzCheckFailed(const char* condition, const char* file, int line) {
        std::fprintf(stderr, "FUZZ_CHECK failed: %s (%s:%d)\n", condition, file, line);
        std::fflush(stderr);
        std::abort();
    }

}  // namespace BECore::Fuzz
