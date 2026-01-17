#include <BECore/Reflection/BinaryArchive.h>
#include <fstream>
#include <cstring>

namespace BECore {

    BinaryArchive::BinaryArchive(Mode mode)
        : _mode(mode)
        , _readPos(0)
    {
        if (_mode == Mode::Write) {
            _buffer.reserve(1024);  // Pre-allocate some space
        }
    }

    bool BinaryArchive::LoadFromFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        _buffer.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(_buffer.data()), size)) {
            _buffer.clear();
            return false;
        }

        _readPos = 0;
        return true;
    }

    bool BinaryArchive::LoadFromBuffer(const void* data, size_t size) {
        _buffer.resize(size);
        std::memcpy(_buffer.data(), data, size);
        _readPos = 0;
        return true;
    }

    bool BinaryArchive::SaveToFile(const std::filesystem::path& path) const {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        file.write(reinterpret_cast<const char*>(_buffer.data()), 
                   static_cast<std::streamsize>(_buffer.size()));
        return file.good();
    }

    void BinaryArchive::WriteBytes(const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        _buffer.insert(_buffer.end(), bytes, bytes + size);
    }

    bool BinaryArchive::ReadBytes(void* data, size_t size) {
        if (_readPos + size > _buffer.size()) {
            return false;
        }
        std::memcpy(data, _buffer.data() + _readPos, size);
        _readPos += size;
        return true;
    }

    // =============================================================================
    // Primitive serialization implementations
    // =============================================================================

    void BinaryArchive::Serialize(eastl::string_view /*name*/, bool& value) {
        if (_mode == Mode::Write) {
            uint8_t byte = value ? 1 : 0;
            WritePOD(byte);
        } else {
            uint8_t byte = 0;
            if (ReadPOD(byte)) {
                value = byte != 0;
            }
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, int8_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, uint8_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, int16_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, uint16_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, int32_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, uint32_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, int64_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, uint64_t& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, float& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, double& value) {
        if (_mode == Mode::Write) {
            WritePOD(value);
        } else {
            ReadPOD(value);
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, eastl::string& value) {
        if (_mode == Mode::Write) {
            // Write length + data
            uint32_t length = static_cast<uint32_t>(value.size());
            WritePOD(length);
            if (length > 0) {
                WriteBytes(value.data(), length);
            }
        } else {
            // Read length + data
            uint32_t length = 0;
            if (ReadPOD(length)) {
                if (length > 0) {
                    value.resize(length);
                    ReadBytes(value.data(), length);
                } else {
                    value.clear();
                }
            }
        }
    }

    void BinaryArchive::Serialize(eastl::string_view /*name*/, PoolString& value) {
        if (_mode == Mode::Write) {
            eastl::string_view sv = value.ToStringView();
            uint32_t length = static_cast<uint32_t>(sv.size());
            WritePOD(length);
            if (length > 0) {
                WriteBytes(sv.data(), length);
            }
        } else {
            uint32_t length = 0;
            if (ReadPOD(length)) {
                if (length > 0) {
                    eastl::string temp;
                    temp.resize(length);
                    if (ReadBytes(temp.data(), length)) {
                        value = PoolString::Intern(eastl::string_view{temp.data(), temp.size()});
                    }
                } else {
                    value = PoolString{};
                }
            }
        }
    }

    // =============================================================================
    // Object nesting (no-op for binary format)
    // =============================================================================

    bool BinaryArchive::BeginObject(eastl::string_view /*name*/) {
        // Binary format doesn't need object markers
        // Fields are serialized sequentially
        return true;
    }

    void BinaryArchive::EndObject() {
        // No-op for binary format
    }

    // =============================================================================
    // Array support
    // =============================================================================

    bool BinaryArchive::BeginArray(eastl::string_view /*name*/, size_t& count) {
        if (_mode == Mode::Write) {
            uint32_t size = static_cast<uint32_t>(count);
            WritePOD(size);
        } else {
            uint32_t size = 0;
            if (ReadPOD(size)) {
                count = static_cast<size_t>(size);
            } else {
                return false;
            }
        }
        return true;
    }

    void BinaryArchive::EndArray() {
        // No-op for binary format
    }

}  // namespace BECore
