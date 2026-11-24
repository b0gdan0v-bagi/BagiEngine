#include <BECore/File/File.h>
#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <EASTL/numeric.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {
        // Sized for a save or a config of a few hundred entries; a bigger document takes more chunks, a request over a chunk its own
        constexpr size_t kArenaChunkBytes = 16 * 1024;
        constexpr size_t kBufferReserveBytes = 256;
        constexpr size_t kScopeReserve = 16;
        constexpr size_t kTextReserve = 64;
    }  // namespace

    bool BinarySerializer::SaveToFile(const Path& path) const {
        const Vector<uint8_t>& image = GetData();
        return WriteFileAtomically(path, Span<const uint8_t>(image.data(), image.size())).has_value();
    }

    BinarySerializer::PoolSectionSize BinarySerializer::PoolSectionStats() const {
        return {_pool.size(), _poolBytes};
    }

    void BinarySerializer::ForEachPoolText(FunctionRef<void(StringView)> visit) const {
        for (const auto& [text, entry] : _pool) {
            visit(text);
        }
    }

    namespace {
        /// The sorted order of the texts of a table: order[i] is the first-use index of the text that sorts i-th, finalIndex is its inverse
        void SortTable(const ArenaVector<StringView>& textOf, ArenaVector<uint32_t>& order, ArenaVector<uint32_t>& finalIndex) {
            const size_t count = textOf.size();
            order.resize(count);
            finalIndex.resize(count);
            eastl::iota(order.begin(), order.end(), uint32_t{0});
            eastl::sort(order.begin(), order.end(), [&textOf](uint32_t a, uint32_t b) { return BinaryFormat::CompareStrings(textOf[a], textOf[b]) < 0; });
            for (size_t i = 0; i < count; ++i) {
                finalIndex[order[i]] = static_cast<uint32_t>(i);
            }
        }
    }  // namespace

    const Vector<uint8_t>& BinarySerializer::GetData() const {
        if (!_imageIsStale) {
            return _image;
        }

        const ScratchScope scratch;
        const size_t nameCount = _names.size();
        const size_t poolCount = _pool.size();
        ArenaVector<StringView> nameOf(nameCount);
        size_t nameCharBytes = 0;
        for (const auto& [name, firstUse] : _names) {
            nameOf[firstUse] = name;
            nameCharBytes += name.size();
        }
        ArenaVector<StringView> poolTextOf(poolCount);
        ArenaVector<uint64_t> poolHashOf(poolCount);
        for (const auto& [text, entry] : _pool) {
            poolTextOf[entry.index] = text;
            poolHashOf[entry.index] = entry.hash;
        }

        ArenaVector<uint32_t> nameOrder;
        ArenaVector<uint32_t> nameFinalIndex;
        SortTable(nameOf, nameOrder, nameFinalIndex);
        ArenaVector<uint32_t> poolOrder;
        ArenaVector<uint32_t> poolFinalIndex;
        SortTable(poolTextOf, poolOrder, poolFinalIndex);

        const size_t endsOffset = BinaryFormat::HeaderSize + 2 * sizeof(uint32_t);
        const size_t charsOffset = endsOffset + nameCount * sizeof(uint32_t);
        const size_t poolOffset = charsOffset + nameCharBytes;
        const size_t poolEntriesOffset = poolOffset + BinaryFormat::PoolSectionHeadSize;
        const size_t entriesOffset = poolEntriesOffset + _poolBytes;
        const size_t total = entriesOffset + _buffer.size();
        ASSERT(total - BinaryFormat::HeaderSize <= std::numeric_limits<uint32_t>::max(), "Binary data does not fit into 4 GiB");

        _image.resize(total);
        uint8_t* out = _image.data();
        std::memcpy(out, BinaryFormat::Magic, sizeof(BinaryFormat::Magic));
        BinaryFormat::Store<uint16_t>(out + sizeof(BinaryFormat::Magic), BinaryFormat::CurrentVersion);
        BinaryFormat::Store<uint32_t>(out + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(total - BinaryFormat::HeaderSize));
        BinaryFormat::Store<uint32_t>(out + BinaryFormat::HeaderSize, static_cast<uint32_t>(nameCount));
        BinaryFormat::Store<uint32_t>(out + BinaryFormat::HeaderSize + sizeof(uint32_t), static_cast<uint32_t>(nameCharBytes));

        size_t end = 0;
        for (size_t i = 0; i < nameCount; ++i) {
            const StringView name = nameOf[nameOrder[i]];
            std::memcpy(out + charsOffset + end, name.data(), name.size());
            end += name.size();
            BinaryFormat::Store<uint32_t>(out + endsOffset + i * sizeof(uint32_t), static_cast<uint32_t>(end));
        }

        // The entries are written field by field into the image: ConstructPoolEntry wants 8-aligned memory and the section starts at
        // any offset of the stream. The layout is the one of Details::PoolStringEntry; the padding is zeroed because the image may be
        // a rebuild over older bytes
        size_t entryAt = poolEntriesOffset;
        for (size_t i = 0; i < poolCount; ++i) {
            const StringView text = poolTextOf[poolOrder[i]];
            const size_t entryBytes = Details::PoolEntryBytes(text.size());
            std::memset(out + entryAt, 0, entryBytes);
            BinaryFormat::Store<uint64_t>(out + entryAt + offsetof(Details::PoolStringEntry, hash), poolHashOf[poolOrder[i]]);
            BinaryFormat::Store<uint32_t>(out + entryAt + offsetof(Details::PoolStringEntry, size), static_cast<uint32_t>(text.size()));
            std::memcpy(out + entryAt + offsetof(Details::PoolStringEntry, data), text.data(), text.size());
            entryAt += entryBytes;
        }
        ASSERT(entryAt == entriesOffset, "The pool section took other bytes than it counted");
        BinaryFormat::Store<uint32_t>(out + poolOffset, static_cast<uint32_t>(poolCount));
        BinaryFormat::Store<uint32_t>(out + poolOffset + sizeof(uint32_t), static_cast<uint32_t>(_poolBytes));
        BinaryFormat::Store<uint32_t>(out + poolOffset + 2 * sizeof(uint32_t), Details::PoolHashFunctionId());
        BinaryFormat::Store<uint32_t>(out + poolOffset + 3 * sizeof(uint32_t), 0);
        BinaryFormat::Store<uint64_t>(out + poolOffset + 4 * sizeof(uint32_t), BinaryFormat::PoolSectionChecksum(out + poolEntriesOffset, _poolBytes));

        if (!_buffer.empty()) {
            std::memcpy(out + entriesOffset, _buffer.data(), _buffer.size());
        }
        for (const size_t offset : _nameOffsets) {
            uint8_t* field = out + entriesOffset + offset;
            BinaryFormat::Store<uint32_t>(field, nameFinalIndex[BinaryFormat::Load<uint32_t>(field)]);
        }
        for (const size_t offset : _poolOffsets) {
            uint8_t* field = out + entriesOffset + offset;
            BinaryFormat::Store<uint32_t>(field, poolFinalIndex[BinaryFormat::Load<uint32_t>(field)]);
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

    BinarySerializer::BinarySerializer()
        : _arena("BinarySerializer", kArenaChunkBytes), _scopes(ArenaAllocator(_arena)), _names(ArenaAllocator(_arena)), _pool(ArenaAllocator(_arena)), _nameOffsets(ArenaAllocator(_arena)),
          _poolOffsets(ArenaAllocator(_arena)) {
        _buffer.reserve(kBufferReserveBytes);
        _scopes.reserve(kScopeReserve);
        _names.reserve(kTextReserve);
        _pool.reserve(kTextReserve);
        _nameOffsets.reserve(kTextReserve);
        _poolOffsets.reserve(kTextReserve);
    }

    namespace {
        /// A copy of @p text in @p arena, so a table can key on it after the caller's buffer is gone
        StringView CopyToArena(LinearArena& arena, StringView text) {
            char* copy = arena.Allocate<char>(text.size());
            if (!text.empty()) {
                std::memcpy(copy, text.data(), text.size());
            }
            return {copy, text.size()};
        }
    }  // namespace

    void BinarySerializer::AppendNameIndex(StringView name) {
        uint32_t index = 0;
        if (const auto found = _names.find(name); found != _names.end()) {
            index = found->second;
        } else {
            ASSERT(_names.size() < std::numeric_limits<uint32_t>::max(), "Binary name table is full");
            index = static_cast<uint32_t>(_names.size());
            _names.emplace(CopyToArena(_arena, name), index);
        }
        _nameOffsets.push_back(_buffer.size());
        AppendValue<uint32_t>(index);
    }

    void BinarySerializer::AppendPoolIndex(const PoolString& value) {
        const StringView text = value.ToStringView();
        uint32_t index = 0;
        if (const auto found = _pool.find(text); found != _pool.end()) {
            index = found->second.index;
        } else {
            ASSERT(_pool.size() < std::numeric_limits<uint32_t>::max(), "Binary pool section is full");
            index = static_cast<uint32_t>(_pool.size());
            _pool.emplace(CopyToArena(_arena, text), PoolText{index, value.HashValue()});
            _poolBytes += Details::PoolEntryBytes(text.size());
        }
        _poolOffsets.push_back(_buffer.size());
        AppendValue<uint32_t>(index);
    }

    bool BinarySerializer::BeginEntry(uint8_t tag, StringView name) {
        if (!_scopes.empty() && _scopes.back().isArray) {
            EXPECT(false, "Entries must be written inside BeginArrayElement, not directly into an array");
            return false;
        }

        ASSERT(name.size() <= std::numeric_limits<uint16_t>::max(), "Binary entry name is too long");

        AppendValue<uint8_t>(tag);
        AppendNameIndex(name);
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

    void BinarySerializer::WritePoolString(StringView name, const PoolString& value) {
        const StringView text = value.ToStringView();
        if (text.size() > ReaderLimits::kMaxPoolStringBytes) {
            WriteString(name, text);
            return;
        }
        if (!BeginEntry(BinaryFormat::Tag::PoolString, name)) {
            return;
        }
        AppendPoolIndex(value);
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
        WritePoolString(name, value);
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
        WritePoolString(name, value);
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
