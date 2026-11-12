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
        };
        return targets;
    }

    eastl::vector<uint8_t> WithTrueBinaryHeader(const uint8_t* data, size_t size) {
        if (size < BinaryFormat::HeaderSize) {
            return {};
        }
        eastl::vector<uint8_t> repaired(data, data + size);
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
