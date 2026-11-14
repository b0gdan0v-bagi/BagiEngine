#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/BinarySerializer.h>

namespace BECore {

    bool BinarySerializer::SaveToFile(const std::filesystem::path& path) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(_buffer.data()), static_cast<std::streamsize>(_buffer.size()));
        return file.good();
    }

    // =============================================================================
    // Low-level output
    // =============================================================================

    void BinarySerializer::Append(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        _buffer.insert(_buffer.end(), bytes, bytes + size);

        // The header always describes the bytes written so far
        if (_buffer.size() >= BinaryFormat::HeaderSize) {
            PatchSizeField(BinaryFormat::PayloadSizeOffset, _buffer.size() - BinaryFormat::HeaderSize);
        }
    }

    template <typename T>
    void BinarySerializer::AppendValue(T value) {
        uint8_t bytes[sizeof(T)];
        BinaryFormat::Store<T>(bytes, value);
        Append(bytes, sizeof(T));
    }

    void BinarySerializer::PatchSizeField(size_t offset, size_t value) {
        ASSERT(value <= std::numeric_limits<uint32_t>::max(), "Binary data does not fit into 4 GiB");
        BinaryFormat::Store<uint32_t>(_buffer.data() + offset, static_cast<uint32_t>(value));
    }

    BinarySerializer::BinarySerializer() {
        _buffer.reserve(256);
        Append(BinaryFormat::Magic, sizeof(BinaryFormat::Magic));
        AppendValue<uint16_t>(BinaryFormat::CurrentVersion);
        AppendValue<uint32_t>(0);  // payload size, kept up to date by Append()
    }

    bool BinarySerializer::BeginEntry(uint8_t tag, StringView name) {
        if (!_scopes.empty() && _scopes.back().isArray) {
            EXPECT(false, "Entries must be written inside BeginArrayElement, not directly into an array");
            return false;
        }

        ASSERT(name.size() <= std::numeric_limits<uint16_t>::max(), "Binary entry name is too long");
        const auto nameLength = static_cast<uint16_t>(std::min<size_t>(name.size(), std::numeric_limits<uint16_t>::max()));

        AppendValue<uint8_t>(tag);
        AppendValue<uint16_t>(nameLength);
        Append(name.data(), nameLength);
        return true;
    }

    // =============================================================================
    // Write methods
    // =============================================================================

    template <typename T>
    void BinarySerializer::WriteScalar(StringView name, T value) {
        if (!BeginEntry(BinaryFormat::TagFor<T>(), name)) {
            return;
        }
        if constexpr (std::is_same_v<T, bool>) {
            AppendValue<uint8_t>(value ? 1 : 0);
        } else {
            AppendValue<T>(value);
        }
    }

    void BinarySerializer::WriteString(StringView name, StringView value) {
        if (!BeginEntry(BinaryFormat::Tag::String, name)) {
            return;
        }
        ASSERT(value.size() <= std::numeric_limits<uint32_t>::max(), "Binary string is too long");
        AppendValue<uint32_t>(static_cast<uint32_t>(value.size()));
        Append(value.data(), value.size());
    }

    void BinarySerializer::Write(StringView name, bool value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, int8_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, uint8_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, int16_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, uint16_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, int32_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, uint32_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, int64_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, uint64_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, float value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, double value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::Write(StringView name, StringView value) {
        WriteString(name, value);
    }

    void BinarySerializer::Write(StringView name, const PoolString& value) {
        WriteString(name, value.ToStringView());
    }

    // =============================================================================
    // WriteAttribute methods (same entries as Write)
    // =============================================================================

    void BinarySerializer::WriteAttribute(StringView name, bool value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, int8_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, uint8_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, int16_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, uint16_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, int32_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, uint32_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, int64_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, uint64_t value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, float value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, double value) {
        WriteScalar(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, StringView value) {
        WriteString(name, value);
    }

    void BinarySerializer::WriteAttribute(StringView name, const PoolString& value) {
        WriteString(name, value.ToStringView());
    }

    // =============================================================================
    // Scopes (objects, arrays, array elements)
    // =============================================================================

    void BinarySerializer::OpenScope(bool isArray) {
        Scope scope;
        scope.sizeOffset = _buffer.size();
        scope.isArray = isArray;
        _scopes.push_back(scope);

        AppendValue<uint32_t>(0);  // size, patched in CloseScope
        if (isArray) {
            AppendValue<uint32_t>(0);  // element count, patched in CloseScope
        }
    }

    void BinarySerializer::CloseScope(bool isArray) {
        if (_scopes.empty() || _scopes.back().isArray != isArray) {
            EXPECT(false, "Unbalanced End call in BinarySerializer");
            return;
        }

        const Scope scope = _scopes.back();
        _scopes.pop_back();

        PatchSizeField(scope.sizeOffset, _buffer.size() - (scope.sizeOffset + sizeof(uint32_t)));
        if (isArray) {
            PatchSizeField(scope.sizeOffset + sizeof(uint32_t), scope.elementCount);
        }
    }

    bool BinarySerializer::BeginObject(StringView name) {
        if (!BeginEntry(BinaryFormat::Tag::Object, name)) {
            return false;
        }
        OpenScope(false);
        return true;
    }

    void BinarySerializer::EndObject() {
        CloseScope(false);
    }

    bool BinarySerializer::BeginArray(StringView name, StringView /*elementName*/, size_t& /*count*/) {
        if (!BeginEntry(BinaryFormat::Tag::Array, name)) {
            return false;
        }
        OpenScope(true);
        return true;
    }

    void BinarySerializer::EndArray() {
        CloseScope(true);
    }

    bool BinarySerializer::BeginArrayElement() {
        if (_scopes.empty() || !_scopes.back().isArray) {
            return false;
        }
        ++_scopes.back().elementCount;
        OpenScope(false);
        return true;
    }

    void BinarySerializer::EndArrayElement() {
        CloseScope(false);
    }

}  // namespace BECore
