#include <BECore/Reflection/BinaryDeserializer.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        /// Scopes of up to this many entries are checked for a repeated name on the stack
        constexpr size_t kInlineNameKeys = 16;

        /// A scope with more entries is not checked: it is taken as holding a repeated name, which only costs the old scan (the check is bounded in memory)
        constexpr size_t kMaxTrackedEntries = 65536;

        /// The first block of ValidateState::spill, so a file of ordinary wide scopes allocates once and not at every doubling
        constexpr size_t kSpillReserve = 64;

        constexpr uint64_t kNameClassSalt = 0x9E3779B97F4A7C15ULL;

        /// What validation collects on the way: the whole file's verdict on repeated names and the keys of the scopes that are being walked
        struct ValidateState {
            const uint8_t* data = nullptr;
            bool hasDuplicateNames = false;
            /// The keys of the scopes beyond kInlineNameKeys entries. A scope owns the tail from where it began and cuts it back when it ends,
            /// so the nested scopes that are walked in between do not mix with it
            Vector<uint64_t> spill;
        };

        /// Equal for the same name of the same kind of entry (scalar, object, array); different names or kinds collide only by chance,
        /// and a collision only sends the file to the slow lookup
        uint64_t NameKey(const BinaryFormat::EntryView& entry) {
            const uint64_t kind = BinaryFormat::IsScalarTag(entry.tag) ? 0 : (entry.tag == BinaryFormat::Tag::Object ? 1 : 2);
            return StringUtils::GetHash(entry.name) + kind * kNameClassSalt;
        }

        bool ValidateScope(ValidateState& state, size_t begin, size_t end, size_t depth);

        /// Validate the array payload: u32 size | u32 count | count x (u32 size | entries)
        bool ValidateArray(ValidateState& state, const BinaryFormat::EntryView& entry, size_t depth) {
            const size_t countOffset = entry.payloadBegin + sizeof(uint32_t);
            if (entry.end - countOffset < sizeof(uint32_t)) {
                return false;
            }

            const size_t count = BinaryFormat::Load<uint32_t>(state.data + countOffset);
            if (count > ReaderLimits::kMaxArrayElements) {
                return false;
            }
            size_t position = countOffset + sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                if (entry.end - position < sizeof(uint32_t)) {
                    return false;
                }
                const size_t elementSize = BinaryFormat::Load<uint32_t>(state.data + position);
                position += sizeof(uint32_t);
                if (elementSize > entry.end - position) {
                    return false;
                }
                if (!ValidateScope(state, position, position + elementSize, depth + 1)) {
                    return false;
                }
                position += elementSize;
            }
            return position == entry.end;
        }

        /// Check that [begin, end) is a sequence of well-formed entries, recursively, and note whether a name repeats in any scope
        bool ValidateScope(ValidateState& state, size_t begin, size_t end, size_t depth) {
            if (depth > BinaryFormat::MaxDepth) {
                return false;
            }

            uint64_t keys[kInlineNameKeys];
            size_t keyCount = 0;
            size_t entryCount = 0;
            const size_t spillBegin = state.spill.size();

            size_t position = begin;
            while (position < end) {
                BinaryFormat::EntryView entry;
                if (!BinaryFormat::ParseEntry(state.data, position, end, entry)) {
                    return false;
                }

                if (!state.hasDuplicateNames) {
                    const uint64_t key = NameKey(entry);
                    if (++entryCount > kMaxTrackedEntries) {
                        state.hasDuplicateNames = true;
                    } else if (keyCount < kInlineNameKeys) {
                        for (size_t i = 0; i < keyCount; ++i) {
                            state.hasDuplicateNames = state.hasDuplicateNames || keys[i] == key;
                        }
                        keys[keyCount++] = key;
                    } else {
                        if (state.spill.capacity() == 0) {
                            state.spill.reserve(kSpillReserve);
                        }
                        state.spill.push_back(key);
                    }
                }

                if (entry.tag == BinaryFormat::Tag::Object) {
                    if (!ValidateScope(state, entry.payloadBegin + sizeof(uint32_t), entry.end, depth + 1)) {
                        return false;
                    }
                } else if (entry.tag == BinaryFormat::Tag::Array) {
                    if (!ValidateArray(state, entry, depth)) {
                        return false;
                    }
                }
                position = entry.end;
            }

            if (state.spill.size() > spillBegin) {
                if (!state.hasDuplicateNames) {
                    // The inline keys joined the tail, so one sort finds a repeat among all of them
                    state.spill.insert(state.spill.end(), keys, keys + keyCount);
                    eastl::sort(state.spill.begin() + spillBegin, state.spill.end());
                    for (size_t i = spillBegin + 1; i < state.spill.size(); ++i) {
                        state.hasDuplicateNames = state.hasDuplicateNames || state.spill[i] == state.spill[i - 1];
                    }
                }
                state.spill.resize(spillBegin);
            }
            return true;
        }

    }  // namespace

    // =============================================================================
    // Loading
    // =============================================================================

    void BinaryDeserializer::ResetState() {
        _data = nullptr;
        _size = 0;
        _hasDuplicateNames = false;
        _owned.clear();
        _scopeStack.clear();
        _arrayStack.clear();
        ClearPathSegments();
    }

    bool BinaryDeserializer::Validate(const uint8_t* data, size_t size) {
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
        ValidateState state;
        state.data = data;
        if (!ValidateScope(state, BinaryFormat::HeaderSize, size, 0)) {
            AddError("", "Binary data is corrupt (or an array has more elements than the limit)", 0);
            return false;
        }
        _hasDuplicateNames = state.hasDuplicateNames;
        return true;
    }

    void BinaryDeserializer::Open(const uint8_t* data, size_t size) {
        _data = data;
        _size = size;

        _scopeStack.push_back(Scope{BinaryFormat::HeaderSize, size, BinaryFormat::HeaderSize});
    }

    bool BinaryDeserializer::LoadFromView(const uint8_t* data, size_t size) {
        ResetState();
        if (!Validate(data, size)) {
            return false;
        }
        Open(data, size);
        return true;
    }

    bool BinaryDeserializer::LoadFromBuffer(const uint8_t* data, size_t size) {
        ResetState();
        if (!Validate(data, size)) {
            return false;
        }
        _owned.assign(data, data + size);
        Open(_owned.data(), _owned.size());
        return true;
    }

    bool BinaryDeserializer::LoadFromBuffer(const Vector<uint8_t>& data) {
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

        _owned.resize(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(_owned.data()), static_cast<std::streamsize>(fileSize))) {
            _owned.clear();
            AddError("", "Failed to read binary file", 0);
            return false;
        }
        if (!Validate(_owned.data(), _owned.size())) {
            _owned.clear();
            return false;
        }
        Open(_owned.data(), _owned.size());
        return true;
    }

    void BinaryDeserializer::AddError(StringView path, StringView message, int32_t line) {
        StoreError(_errors, path, message, line);
    }

    // =============================================================================
    // Lookup
    // =============================================================================

    bool BinaryDeserializer::FindEntry(StringView name, TagPredicate accepts, bool moveCursor, BinaryFormat::EntryView& outEntry) const {
        if (_scopeStack.empty()) {
            return false;
        }

        const Scope& scope = _scopeStack.back();

        if (_hasDuplicateNames) {
            bool found = false;
            size_t position = scope.begin;
            while (position < scope.end) {
                BinaryFormat::EntryView entry;
                if (!BinaryFormat::ParseEntry(_data, position, scope.end, entry)) {
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

        // Every name is written once per kind, so the first match is the only one: look after the last match first, where a reader of the
        // fields in the order they were written finds the next one at once, then wrap round to the entries before it
        const auto scan = [&](size_t from, size_t to) {
            for (size_t position = from; position < to;) {
                BinaryFormat::EntryView entry;
                if (!BinaryFormat::ParseEntry(_data, position, scope.end, entry)) {
                    return false;  // cannot happen after validation
                }
                if (accepts(entry.tag) && entry.name == name) {
                    outEntry = entry;
                    return true;
                }
                position = entry.end;
            }
            return false;
        };
        if (!scan(scope.cursor, scope.end) && !scan(scope.begin, scope.cursor)) {
            return false;
        }
        if (moveCursor) {
            scope.cursor = outEntry.end;
        }
        return true;
    }

    bool BinaryDeserializer::HasKey(StringView name) const {
        BinaryFormat::EntryView entry;
        return FindEntry(name, [](uint8_t) { return true; }, false, entry);
    }

    bool BinaryDeserializer::FindScalar(StringView name, uint8_t expectedTag, bool reportMissing, BinaryFormat::EntryView& outEntry) {
        if (!FindEntry(name, &BinaryFormat::IsScalarTag, true, outEntry)) {
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

        const uint8_t* payload = _data + entry.payloadBegin;
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

        const size_t length = BinaryFormat::Load<uint32_t>(_data + entry.payloadBegin);
        outValue = StringView(reinterpret_cast<const char*>(_data + entry.payloadBegin + sizeof(uint32_t)), length);
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

    bool BinaryDeserializer::Read(StringView name, String& outValue) {
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
        if (!AcceptPoolStringLength(name, text.size())) {
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

    bool BinaryDeserializer::ReadAttribute(StringView name, String& outValue) {
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
        if (!AcceptPoolStringLength(name, text.size())) {
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
        if (!FindEntry(name, &BinaryFormat::IsObjectTag, true, entry)) {
            return false;
        }

        const size_t begin = entry.payloadBegin + sizeof(uint32_t);
        _scopeStack.push_back(Scope{begin, entry.end, begin});
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
        if (!FindEntry(name, &BinaryFormat::IsArrayTag, true, entry)) {
            return false;
        }

        // Layout (validated on load): u32 size | u32 count | count x (u32 size | entries)
        ArrayContext context;
        context.count = BinaryFormat::Load<uint32_t>(_data + entry.payloadBegin + sizeof(uint32_t));
        context.next = entry.payloadBegin + 2 * sizeof(uint32_t);
        count = context.count;
        _arrayStack.push_back(context);

        // The array itself holds no entries, only elements: an empty scope keeps stray lookups from reading them
        _scopeStack.push_back(Scope{entry.end, entry.end, entry.end});
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
        if (context.index >= context.count) {
            return false;
        }

        const size_t begin = context.next + sizeof(uint32_t);
        const size_t end = begin + BinaryFormat::Load<uint32_t>(_data + context.next);
        context.next = end;
        PushPathArrayIndex(context.index++);
        _scopeStack.push_back(Scope{begin, end, begin});
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

    const Vector<DeserializeError>& BinaryDeserializer::GetErrors() const {
        return _errors;
    }

    void BinaryDeserializer::ClearErrors() {
        _errors.clear();
    }

}  // namespace BECore
