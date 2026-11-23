#include <BECore/File/File.h>
#include <BECore/File/FilePath.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <bit>

namespace BECore {

    namespace {

        /// Scopes of up to this many entries are checked for a repeated name on the stack
        constexpr size_t kInlineNameKeys = 16;

        /// A scope with more entries is not checked: it is taken as holding a repeated name, which only costs the old scan (the check is bounded in memory)
        constexpr size_t kMaxTrackedEntries = 65536;

        /// The first block of ValidateState::spill, so a file of ordinary wide scopes allocates once and not at every doubling
        constexpr size_t kSpillReserve = 64;

        /// The 64-bit golden-ratio constant: an odd multiplier that spreads the kind into the high bits of the name hash
        constexpr uint64_t kNameClassSalt = 0x9E3779B97F4A7C15ULL;

        constexpr uint64_t kNameKindScalar = 0;
        constexpr uint64_t kNameKindObject = 1;
        constexpr uint64_t kNameKindArray = 2;

        /// What validation collects on the way: the whole file's verdict on repeated names and the keys of the scopes that are being walked
        struct ValidateState {
            const uint8_t* data = nullptr;
            /// The string table of a version 2 stream; null for version 1
            const BinaryFormat::StringTable* table = nullptr;
            /// One bit per table string that a tag PoolString entry refers to, sized when the first one is met. On the heap: it is moved into
            /// the reader, which keeps it until the first PoolString read
            Vector<uint64_t> poolBits;
            bool hasDuplicateNames = false;
            /// The keys of the scopes beyond kInlineNameKeys entries. A scope owns the tail from where it began and cuts it back when it ends,
            /// so the nested scopes that are walked in between do not mix with it
            ArenaVector<uint64_t> spill;
        };

        constexpr unsigned kNameKindBits = 2;
        constexpr size_t kBitsPerWord = 64;

        /// Equal for the same name of the same kind of entry (scalar, object, array). Version 1 hashes the name, so different names or kinds
        /// collide only by chance, and a collision only sends the file to the slow lookup; in version 2 the table makes a name one index and the key is exact
        uint64_t NameKey(const ValidateState& state, const BinaryFormat::EntryView& entry) {
            const uint64_t kind = BinaryFormat::IsScalarTag(entry.tag) ? kNameKindScalar : entry.tag == BinaryFormat::Tag::Object ? kNameKindObject : kNameKindArray;
            if (state.table != nullptr) {
                return (static_cast<uint64_t>(entry.nameIndex) << kNameKindBits) | kind;
            }
            return StringUtils::GetHash(entry.name) + kind * kNameClassSalt;
        }

        bool ParseEntryOf(const ValidateState& state, size_t position, size_t end, BinaryFormat::EntryView& entry) {
            return state.table != nullptr ? BinaryFormat::ParseEntryV2(state.data, position, end, *state.table, entry) : BinaryFormat::ParseEntry(state.data, position, end, entry);
        }

        void MarkPooled(ValidateState& state, uint32_t stringIndex) {
            if (state.poolBits.empty()) {
                state.poolBits.resize((static_cast<size_t>(state.table->count) + kBitsPerWord - 1) / kBitsPerWord, 0);
            }
            state.poolBits[stringIndex / kBitsPerWord] |= uint64_t{1} << (stringIndex % kBitsPerWord);
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
                if (!ParseEntryOf(state, position, end, entry)) {
                    return false;
                }
                if (entry.tag == BinaryFormat::Tag::PoolString) {
                    MarkPooled(state, BinaryFormat::Load<uint32_t>(state.data + entry.payloadBegin));
                }

                if (!state.hasDuplicateNames) {
                    const uint64_t key = NameKey(state, entry);
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
        _tabled = false;
        _table = {};
        _poolBits.clear();
        _pooledIndices.clear();
        _pooled.clear();
        _pooledReady = false;
        _owned.clear();
        _scopeStack.clear();
        _arrayStack.clear();
        ClearPathSegments();
    }

    bool BinaryDeserializer::Validate(const uint8_t* data, size_t size, Checked& outChecked) {
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
        const uint16_t version = BinaryFormat::Load<uint16_t>(data + sizeof(BinaryFormat::Magic));
        if (version != BinaryFormat::LegacyVersion && version != BinaryFormat::TabledVersion) {
            AddError("", "Unsupported binary format version", 0);
            return false;
        }
        if (BinaryFormat::Load<uint32_t>(data + BinaryFormat::PayloadSizeOffset) != size - BinaryFormat::HeaderSize) {
            AddError("", "Binary data is truncated or has trailing bytes", 0);
            return false;
        }

        // The spill list dies with this call; the scope is made first so the state's default allocator binds to it
        ScratchScope scratch;
        ValidateState state;
        state.data = data;
        outChecked.version = version;
        outChecked.rootBegin = BinaryFormat::HeaderSize;
        if (version == BinaryFormat::TabledVersion) {
            if (!BinaryFormat::ParseStringTable(data, BinaryFormat::HeaderSize, size, outChecked.table, outChecked.rootBegin)) {
                AddError("", "Binary string table is truncated or corrupt", 0);
                return false;
            }
            state.table = &outChecked.table;
        }
        if (!ValidateScope(state, outChecked.rootBegin, size, 0)) {
            AddError("", "Binary data is corrupt (or an array has more elements than the limit)", 0);
            return false;
        }
        outChecked.hasDuplicateNames = state.hasDuplicateNames;
        outChecked.poolBits = eastl::move(state.poolBits);
        return true;
    }

    void BinaryDeserializer::Open(const uint8_t* data, size_t size, Checked&& checked) {
        _data = data;
        _size = size;
        _hasDuplicateNames = checked.hasDuplicateNames;
        _tabled = checked.version == BinaryFormat::TabledVersion;
        _table = checked.table.Rebase(data);
        _poolBits = eastl::move(checked.poolBits);

        _scopeStack.push_back(Scope{checked.rootBegin, size, checked.rootBegin});
    }

    bool BinaryDeserializer::LoadFromView(const uint8_t* data, size_t size) {
        ResetState();
        Checked checked;
        if (!Validate(data, size, checked)) {
            return false;
        }
        Open(data, size, eastl::move(checked));
        return true;
    }

    bool BinaryDeserializer::LoadFromBuffer(const uint8_t* data, size_t size) {
        ResetState();
        Checked checked;
        if (!Validate(data, size, checked)) {
            return false;
        }
        _owned.assign(data, data + size);
        Open(_owned.data(), _owned.size(), eastl::move(checked));
        return true;
    }

    bool BinaryDeserializer::LoadFromBuffer(const Vector<uint8_t>& data) {
        return LoadFromBuffer(data.data(), data.size());
    }

    std::expected<void, StringView> ReadBinaryDocumentFile(const std::filesystem::path& path, Vector<uint8_t>& out) {
        const String utf8Path = PathToUtf8(path);
        auto bytes = ReadFileBytes(utf8Path, ReaderLimits::kMaxBinaryStreamBytes);
        if (bytes) {
            out = std::move(*bytes);
            return {};
        }
        // The layer's error is a sentence for the log; the reader keeps its fixed messages, which say which limit or kind of path it was
        const Optional<FileInfo> info = GetFileInfo(utf8Path);
        if (!info) {
            return std::unexpected(StringView("Failed to open binary file"));
        }
        if (info->kind != FileKind::File) {
            return std::unexpected(StringView("Binary path is not a regular file"));
        }
        if (info->size > ReaderLimits::kMaxBinaryStreamBytes) {
            return std::unexpected(StringView("Binary file is larger than the limit"));
        }
        return std::unexpected(StringView("Failed to read binary file"));
    }

    bool BinaryDeserializer::LoadFromFile(const std::filesystem::path& path) {
        ResetState();

        const std::expected<void, StringView> read = ReadBinaryDocumentFile(path, _owned);
        if (!read.has_value()) {
            AddError("", read.error(), 0);
            return false;
        }
        Checked checked;
        if (!Validate(_owned.data(), _owned.size(), checked)) {
            _owned.clear();
            return false;
        }
        Open(_owned.data(), _owned.size(), eastl::move(checked));
        return true;
    }

    void BinaryDeserializer::AddError(StringView path, StringView message, int32_t line) {
        StoreError(_errors, path, message, line);
    }

    // =============================================================================
    // Lookup
    // =============================================================================

    bool BinaryDeserializer::ParseAt(size_t position, size_t scopeEnd, BinaryFormat::EntryView& outEntry) const {
        return _tabled ? BinaryFormat::ParseEntryV2(_data, position, scopeEnd, _table, outEntry) : BinaryFormat::ParseEntry(_data, position, scopeEnd, outEntry);
    }

    bool BinaryDeserializer::FindEntry(StringView name, TagPredicate accepts, bool moveCursor, BinaryFormat::EntryView& outEntry) const {
        if (_scopeStack.empty()) {
            return false;
        }

        const Scope& scope = _scopeStack.back();

        // The entry at the cursor is where a reader of the fields in the order they were written looks next: comparing its text is cheaper
        // than a search of a table that can hold thousands of strings, and without duplicates the first match from the cursor is the only one
        if (_tabled && !_hasDuplicateNames && scope.cursor < scope.end) {
            BinaryFormat::EntryView entry;
            if (ParseAt(scope.cursor, scope.end, entry) && accepts(entry.tag) && _table.Get(entry.nameIndex) == name) {
                outEntry = entry;
                if (moveCursor) {
                    scope.cursor = entry.end;
                }
                return true;
            }
        }

        // A name that is not in the table of the file is in no entry of it
        uint32_t nameIndex = 0;
        if (_tabled) {
            const Optional<uint32_t> found = _table.Find(name);
            if (!found.has_value()) {
                return false;
            }
            nameIndex = *found;
        }
        const auto isNamed = [&](const BinaryFormat::EntryView& entry) { return _tabled ? entry.nameIndex == nameIndex : entry.name == name; };

        if (_hasDuplicateNames) {
            bool found = false;
            size_t position = scope.begin;
            while (position < scope.end) {
                BinaryFormat::EntryView entry;
                if (!ParseAt(position, scope.end, entry)) {
                    break;  // cannot happen after validation
                }
                if (accepts(entry.tag) && isNamed(entry)) {
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
                if (!ParseAt(position, scope.end, entry)) {
                    return false;  // cannot happen after validation
                }
                if (accepts(entry.tag) && isNamed(entry)) {
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

    bool BinaryDeserializer::FindScalarEntry(StringView name, bool reportMissing, BinaryFormat::EntryView& outEntry) {
        if (!FindEntry(name, &BinaryFormat::IsScalarTag, true, outEntry)) {
            if (reportMissing) {
                ReportError(name, "Element not found");
            }
            return false;
        }
        return true;
    }

    bool BinaryDeserializer::FindScalar(StringView name, uint8_t expectedTag, bool reportMissing, BinaryFormat::EntryView& outEntry) {
        if (!FindScalarEntry(name, reportMissing, outEntry)) {
            return false;
        }
        if (outEntry.tag != expectedTag) {
            ReportError(name, "Type mismatch");
            return false;
        }
        return true;
    }

    bool BinaryDeserializer::FindText(StringView name, bool reportMissing, BinaryFormat::EntryView& outEntry, StringView& outText) {
        if (!FindScalarEntry(name, reportMissing, outEntry)) {
            return false;
        }
        if (!BinaryFormat::IsTextTag(outEntry.tag)) {
            ReportError(name, "Type mismatch");
            return false;
        }

        if (outEntry.tag == BinaryFormat::Tag::PoolString) {
            outText = _table.Get(BinaryFormat::Load<uint32_t>(_data + outEntry.payloadBegin));
        } else {
            const size_t length = BinaryFormat::Load<uint32_t>(_data + outEntry.payloadBegin);
            outText = StringView(reinterpret_cast<const char*>(_data + outEntry.payloadBegin + sizeof(uint32_t)), length);
        }
        return true;
    }

    void BinaryDeserializer::InternTableStrings() {
        _pooledReady = true;
        if (_poolBits.empty()) {
            return;
        }

        // The list dies with this call (InternBatch copies the texts into the pool); reserved, because an arena container leaves its old blocks on a growth
        ScratchScope scratch;
        size_t marked = 0;
        for (const uint64_t word : _poolBits) {
            marked += static_cast<size_t>(std::popcount(word));
        }
        ArenaVector<StringView> texts;
        texts.reserve(marked);
        for (size_t word = 0; word < _poolBits.size(); ++word) {
            for (size_t bit = 0; bit < kBitsPerWord; ++bit) {
                if (((_poolBits[word] >> bit) & 1) == 0) {
                    continue;
                }
                const size_t index = word * kBitsPerWord + bit;
                const StringView text = _table.Get(index);
                // A text over the limit stays out of the pool: its read reports it
                if (text.size() <= ReaderLimits::kMaxPoolStringBytes) {
                    _pooledIndices.push_back(static_cast<uint32_t>(index));
                    texts.push_back(text);
                }
            }
        }
        _poolBits.clear();

        _pooled.resize(texts.size());
        PoolString::InternBatch(Span<const StringView>(texts.data(), texts.size()), Span<PoolString>(_pooled.data(), _pooled.size()));
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
        BinaryFormat::EntryView entry;
        StringView text;
        if (!FindText(name, true, entry, text)) {
            return false;
        }
        outValue.assign(text.data(), text.size());
        return true;
    }

    bool BinaryDeserializer::ReadPoolString(StringView name, bool reportMissing, PoolString& outValue) {
        BinaryFormat::EntryView entry;
        StringView text;
        if (!FindText(name, reportMissing, entry, text)) {
            return false;
        }
        if (!AcceptPoolStringLength(name, text.size())) {
            return false;
        }

        if (entry.tag == BinaryFormat::Tag::PoolString) {
            if (!_pooledReady) {
                InternTableStrings();
            }
            const uint32_t index = BinaryFormat::Load<uint32_t>(_data + entry.payloadBegin);
            const auto* found = eastl::lower_bound(_pooledIndices.begin(), _pooledIndices.end(), index);
            if (found != _pooledIndices.end() && *found == index) {
                outValue = _pooled[static_cast<size_t>(found - _pooledIndices.begin())];
                return true;
            }
        }
        outValue = PoolString::Intern(text);
        return true;
    }

    bool BinaryDeserializer::Read(StringView name, PoolString& outValue) {
        return ReadPoolString(name, true, outValue);
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
        BinaryFormat::EntryView entry;
        StringView text;
        if (!FindText(name, false, entry, text)) {
            return false;
        }
        outValue.assign(text.data(), text.size());
        return true;
    }

    bool BinaryDeserializer::ReadAttribute(StringView name, PoolString& outValue) {
        return ReadPoolString(name, false, outValue);
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
