#include <BECore/Reflection/BinaryDeserializer.h>

namespace BECore {

    namespace {

        bool ValidateScope(const uint8_t* data, size_t begin, size_t end, size_t depth);

        /// Validate the array payload: u32 size | u32 count | count x (u32 size | entries)
        bool ValidateArray(const uint8_t* data, const BinaryFormat::EntryView& entry, size_t depth) {
            const size_t countOffset = entry.payloadBegin + sizeof(uint32_t);
            if (entry.end - countOffset < sizeof(uint32_t)) {
                return false;
            }

            const size_t count = BinaryFormat::Load<uint32_t>(data + countOffset);
            if (count > ReaderLimits::kMaxArrayElements) {
                return false;
            }
            size_t position = countOffset + sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                if (entry.end - position < sizeof(uint32_t)) {
                    return false;
                }
                const size_t elementSize = BinaryFormat::Load<uint32_t>(data + position);
                position += sizeof(uint32_t);
                if (elementSize > entry.end - position) {
                    return false;
                }
                if (!ValidateScope(data, position, position + elementSize, depth + 1)) {
                    return false;
                }
                position += elementSize;
            }
            return position == entry.end;
        }

        /// Check that [begin, end) is a sequence of well-formed entries, recursively
        bool ValidateScope(const uint8_t* data, size_t begin, size_t end, size_t depth) {
            if (depth > BinaryFormat::MaxDepth) {
                return false;
            }

            size_t position = begin;
            while (position < end) {
                BinaryFormat::EntryView entry;
                if (!BinaryFormat::ParseEntry(data, position, end, entry)) {
                    return false;
                }
                if (entry.tag == BinaryFormat::Tag::Object) {
                    if (!ValidateScope(data, entry.payloadBegin + sizeof(uint32_t), entry.end, depth + 1)) {
                        return false;
                    }
                } else if (entry.tag == BinaryFormat::Tag::Array) {
                    if (!ValidateArray(data, entry, depth)) {
                        return false;
                    }
                }
                position = entry.end;
            }
            return true;
        }

    }  // namespace

    // =============================================================================
    // Loading
    // =============================================================================

    void BinaryDeserializer::ResetState() {
        _buffer.clear();
        _scopeStack.clear();
        _arrayStack.clear();
        ClearPathSegments();
    }

    bool BinaryDeserializer::LoadFromBuffer(const uint8_t* data, size_t size) {
        ResetState();

        if (data == nullptr || size < BinaryFormat::HeaderSize) {
            AddError("", "Binary data is too small to hold a header", 0);
            return false;
        }
        if (size > ReaderLimits::kMaxBinaryStreamBytes) {
            AddError("", "Binary data is larger than the limit", 0);
            return false;
        }
        if (std::memcmp(data, BinaryFormat::Magic, sizeof(BinaryFormat::Magic)) != 0) {
            AddError("", "Not a BagiEngine binary stream (bad magic)", 0);
            return false;
        }
        if (BinaryFormat::Load<uint16_t>(data + sizeof(BinaryFormat::Magic)) != BinaryFormat::CurrentVersion) {
            AddError("", "Unsupported binary format version", 0);
            return false;
        }
        if (BinaryFormat::Load<uint32_t>(data + BinaryFormat::PayloadSizeOffset) != size - BinaryFormat::HeaderSize) {
            AddError("", "Binary data is truncated or has trailing bytes", 0);
            return false;
        }
        if (!ValidateScope(data, BinaryFormat::HeaderSize, size, 0)) {
            AddError("", "Binary data is corrupt (or an array has more elements than the limit)", 0);
            return false;
        }

        _buffer.assign(data, data + size);

        Scope root;
        root.begin = BinaryFormat::HeaderSize;
        root.end = size;
        _scopeStack.push_back(root);
        return true;
    }

    bool BinaryDeserializer::LoadFromBuffer(const eastl::vector<uint8_t>& data) {
        return LoadFromBuffer(data.data(), data.size());
    }

    bool BinaryDeserializer::LoadFromFile(const std::filesystem::path& path) {
        ResetState();

        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            // A folder, a pipe or a device has no size to read (tellg() of it is -1)
            AddError("", std::filesystem::exists(path, error) ? "Binary path is not a regular file" : "Failed to open binary file", 0);
            return false;
        }
        const uintmax_t fileSize = std::filesystem::file_size(path, error);
        if (error || fileSize > ReaderLimits::kMaxBinaryStreamBytes) {
            AddError("", error ? "Failed to read the size of the binary file" : "Binary file is larger than the limit", 0);
            return false;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            AddError("", "Failed to open binary file", 0);
            return false;
        }

        eastl::vector<uint8_t> bytes;
        bytes.resize(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(fileSize))) {
            AddError("", "Failed to read binary file", 0);
            return false;
        }
        return LoadFromBuffer(bytes);
    }

    void BinaryDeserializer::AddError(StringView path, StringView message, int32_t line) {
        StoreError(_errors, path, message, line);
    }

    // =============================================================================
    // Lookup
    // =============================================================================

    bool BinaryDeserializer::FindEntry(StringView name, TagPredicate accepts, BinaryFormat::EntryView& outEntry) const {
        if (_scopeStack.empty()) {
            return false;
        }

        const Scope& scope = _scopeStack.back();
        bool found = false;
        size_t position = scope.begin;
        while (position < scope.end) {
            BinaryFormat::EntryView entry;
            if (!BinaryFormat::ParseEntry(_buffer.data(), position, scope.end, entry)) {
                break;  // cannot happen after validation
            }
            if (accepts(entry.tag) && entry.name == name) {
                outEntry = entry;
                found = true;  // keep scanning: the last write wins
            }
            position = entry.end;
        }
        return found;
    }

    bool BinaryDeserializer::FindScalar(StringView name, uint8_t expectedTag, bool reportMissing, BinaryFormat::EntryView& outEntry) {
        if (!FindEntry(name, &BinaryFormat::IsScalarTag, outEntry)) {
            if (reportMissing) {
                ReportError(name, "Element not found");
            }
            return false;
        }
        if (outEntry.tag != expectedTag) {
            ReportError(name, "Type mismatch");
            return false;
        }
        return true;
    }

    // =============================================================================
    // Read methods
    // =============================================================================

    template <typename T>
    bool BinaryDeserializer::ReadScalar(StringView name, T& outValue, bool reportMissing) {
        BinaryFormat::EntryView entry;
        if (!FindScalar(name, BinaryFormat::TagFor<T>(), reportMissing, entry)) {
            return false;
        }

        const uint8_t* payload = _buffer.data() + entry.payloadBegin;
        if constexpr (std::is_same_v<T, bool>) {
            outValue = (*payload != 0);
        } else {
            outValue = BinaryFormat::Load<T>(payload);
        }
        return true;
    }

    bool BinaryDeserializer::ReadStringView(StringView name, bool reportMissing, StringView& outValue) {
        BinaryFormat::EntryView entry;
        if (!FindScalar(name, BinaryFormat::Tag::String, reportMissing, entry)) {
            return false;
        }

        const size_t length = BinaryFormat::Load<uint32_t>(_buffer.data() + entry.payloadBegin);
        outValue = StringView(reinterpret_cast<const char*>(_buffer.data() + entry.payloadBegin + sizeof(uint32_t)), length);
        return true;
    }

    bool BinaryDeserializer::Read(StringView name, bool& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, int8_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, uint8_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, int16_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, uint16_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, int32_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, uint32_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, int64_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, uint64_t& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, float& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, double& outValue) {
        return ReadScalar(name, outValue, true);
    }

    bool BinaryDeserializer::Read(StringView name, eastl::string& outValue) {
        StringView text;
        if (!ReadStringView(name, true, text)) {
            return false;
        }
        outValue.assign(text.data(), text.size());
        return true;
    }

    bool BinaryDeserializer::Read(StringView name, PoolString& outValue) {
        StringView text;
        if (!ReadStringView(name, true, text)) {
            return false;
        }
        outValue = PoolString::Intern(text);
        return true;
    }

    // =============================================================================
    // ReadAttribute methods (a missing entry is not an error: the field may be optional)
    // =============================================================================

    bool BinaryDeserializer::ReadAttribute(StringView name, bool& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, int8_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, uint8_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, int16_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, uint16_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, int32_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, uint32_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, int64_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, uint64_t& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, float& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, double& outValue) {
        return ReadScalar(name, outValue, false);
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, eastl::string& outValue) {
        StringView text;
        if (!ReadStringView(name, false, text)) {
            return false;
        }
        outValue.assign(text.data(), text.size());
        return true;
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, PoolString& outValue) {
        StringView text;
        if (!ReadStringView(name, false, text)) {
            return false;
        }
        outValue = PoolString::Intern(text);
        return true;
    }

    // =============================================================================
    // Object nesting
    // =============================================================================

    bool BinaryDeserializer::BeginObject(StringView name) {
        BinaryFormat::EntryView entry;
        if (!FindEntry(name, &BinaryFormat::IsObjectTag, entry)) {
            return false;
        }

        Scope scope;
        scope.begin = entry.payloadBegin + sizeof(uint32_t);
        scope.end = entry.end;
        _scopeStack.push_back(scope);
        PushPathSegment(name);
        return true;
    }

    void BinaryDeserializer::EndObject() {
        if (_scopeStack.size() > 1) {
            _scopeStack.pop_back();
            PopPathSegment();
        }
    }

    // =============================================================================
    // Array support
    // =============================================================================

    bool BinaryDeserializer::BeginArray(StringView name, StringView /*elementName*/, size_t& count) {
        BinaryFormat::EntryView entry;
        if (!FindEntry(name, &BinaryFormat::IsArrayTag, entry)) {
            return false;
        }

        // Layout (validated on load): u32 size | u32 count | count x (u32 size | entries)
        const size_t elementCount = BinaryFormat::Load<uint32_t>(_buffer.data() + entry.payloadBegin + sizeof(uint32_t));

        ArrayContext context;
        context.elements.reserve(elementCount);
        size_t position = entry.payloadBegin + 2 * sizeof(uint32_t);
        for (size_t i = 0; i < elementCount; ++i) {
            const size_t elementSize = BinaryFormat::Load<uint32_t>(_buffer.data() + position);
            position += sizeof(uint32_t);

            Scope element;
            element.begin = position;
            element.end = position + elementSize;
            context.elements.push_back(element);
            position += elementSize;
        }

        count = context.elements.size();
        _arrayStack.push_back(eastl::move(context));

        // The array itself holds no entries, only elements: an empty scope keeps stray lookups from reading them
        Scope arrayScope;
        arrayScope.begin = entry.end;
        arrayScope.end = entry.end;
        _scopeStack.push_back(arrayScope);
        PushPathSegment(name);
        return true;
    }

    void BinaryDeserializer::EndArray() {
        if (!_arrayStack.empty()) {
            _arrayStack.pop_back();
        }
        if (_scopeStack.size() > 1) {
            _scopeStack.pop_back();
            PopPathSegment();
        }
    }

    bool BinaryDeserializer::BeginArrayElement() {
        if (_arrayStack.empty()) {
            return false;
        }
        ArrayContext& context = _arrayStack.back();
        if (context.currentIndex >= context.elements.size()) {
            return false;
        }
        PushPathArrayIndex(context.currentIndex);
        _scopeStack.push_back(context.elements[context.currentIndex++]);
        return true;
    }

    void BinaryDeserializer::EndArrayElement() {
        if (_scopeStack.size() > 1) {
            _scopeStack.pop_back();
            PopPathSegment();
        }
    }

    // =============================================================================
    // Error tracking
    // =============================================================================

    bool BinaryDeserializer::HasErrors() const {
        return !_errors.empty();
    }

    const eastl::vector<DeserializeError>& BinaryDeserializer::GetErrors() const {
        return _errors;
    }

    void BinaryDeserializer::ClearErrors() {
        _errors.clear();
    }

}  // namespace BECore
