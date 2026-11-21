#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <EASTL/numeric.h>
#include <EASTL/sort.h>

namespace BECore {

    bool BinarySerializer::SaveToFile(const std::filesystem::path& path) const {
        const Vector<uint8_t>& image = GetData();
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        return file.good();
    }

    const Vector<uint8_t>& BinarySerializer::GetData() const {
        if (!_imageIsStale) {
            return _image;
        }

        const size_t count = _texts.size();
        Vector<const String*> textOf(count, nullptr);
        size_t charBytes = 0;
        for (const auto& [text, firstUse] : _texts) {
            textOf[firstUse] = &text;
            charBytes += text.size();
        }

        // order[i] is the first-use index of the text that sorts i-th; finalIndex is its inverse
        Vector<uint32_t> order(count);
        eastl::iota(order.begin(), order.end(), uint32_t{0});
        eastl::sort(order.begin(), order.end(), [&textOf](uint32_t a, uint32_t b) { return BinaryFormat::CompareStrings(StringView(*textOf[a]), StringView(*textOf[b])) < 0; });
        Vector<uint32_t> finalIndex(count);
        for (size_t i = 0; i < count; ++i) {
            finalIndex[order[i]] = static_cast<uint32_t>(i);
        }

        const size_t endsOffset = BinaryFormat::HeaderSize + 2 * sizeof(uint32_t);
        const size_t charsOffset = endsOffset + count * sizeof(uint32_t);
        const size_t entriesOffset = charsOffset + charBytes;
        const size_t total = entriesOffset + _buffer.size();
        ASSERT(total - BinaryFormat::HeaderSize <= std::numeric_limits<uint32_t>::max(), "Binary data does not fit into 4 GiB");

        _image.resize(total);
        uint8_t* out = _image.data();
        std::memcpy(out, BinaryFormat::Magic, sizeof(BinaryFormat::Magic));
        BinaryFormat::Store<uint16_t>(out + sizeof(BinaryFormat::Magic), BinaryFormat::CurrentVersion);
        BinaryFormat::Store<uint32_t>(out + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(total - BinaryFormat::HeaderSize));
        BinaryFormat::Store<uint32_t>(out + BinaryFormat::HeaderSize, static_cast<uint32_t>(count));
        BinaryFormat::Store<uint32_t>(out + BinaryFormat::HeaderSize + sizeof(uint32_t), static_cast<uint32_t>(charBytes));

        size_t end = 0;
        for (size_t i = 0; i < count; ++i) {
            const String& text = *textOf[order[i]];
            std::memcpy(out + charsOffset + end, text.data(), text.size());
            end += text.size();
            BinaryFormat::Store<uint32_t>(out + endsOffset + i * sizeof(uint32_t), static_cast<uint32_t>(end));
        }

        if (!_buffer.empty()) {
            std::memcpy(out + entriesOffset, _buffer.data(), _buffer.size());
        }
        for (const size_t offset : _indexOffsets) {
            uint8_t* field = out + entriesOffset + offset;
            BinaryFormat::Store<uint32_t>(field, finalIndex[BinaryFormat::Load<uint32_t>(field)]);
        }

        _imageIsStale = false;
        return _image;
    }

    // =============================================================================
    // Low-level output
    // =============================================================================

    void BinarySerializer::Append(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        _buffer.insert(_buffer.end(), bytes, bytes + size);
        _imageIsStale = true;
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
        _imageIsStale = true;
    }

    BinarySerializer::BinarySerializer() {
        _buffer.reserve(256);
    }

    void BinarySerializer::AppendTextIndex(StringView text) {
        uint32_t index = 0;
        if (const auto found = _texts.find(text); found != _texts.end()) {
            index = found->second;
        } else {
            ASSERT(_texts.size() < std::numeric_limits<uint32_t>::max(), "Binary string table is full");
            index = static_cast<uint32_t>(_texts.size());
            _texts.emplace(String(text), index);
        }
        _indexOffsets.push_back(_buffer.size());
        AppendValue<uint32_t>(index);
    }

    bool BinarySerializer::BeginEntry(uint8_t tag, StringView name) {
        if (!_scopes.empty() && _scopes.back().isArray) {
            EXPECT(false, "Entries must be written inside BeginArrayElement, not directly into an array");
            return false;
        }

        ASSERT(name.size() <= std::numeric_limits<uint16_t>::max(), "Binary entry name is too long");

        AppendValue<uint8_t>(tag);
        AppendTextIndex(name);
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

    void BinarySerializer::WritePoolString(StringView name, StringView value) {
        if (!BeginEntry(BinaryFormat::Tag::PoolString, name)) {
            return;
        }
        AppendTextIndex(value);
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
        WritePoolString(name, value.ToStringView());
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
        WritePoolString(name, value.ToStringView());
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
