#include "SchemaHash.h"

#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <climits>

namespace BECore {

    namespace {

        // Section markers of the byte stream: a field list, a type, a revisit... never confusable with the data that follows
        constexpr uint64_t kTagFields = 1;
        constexpr uint64_t kTagField = 2;
        constexpr uint64_t kTagDesc = 3;
        constexpr uint64_t kTagAbsent = 4;
        constexpr uint64_t kTagObject = 5;
        constexpr uint64_t kTagRevisit = 6;
        constexpr uint64_t kTagPointer = 7;
        constexpr uint64_t kTagUnregistered = 8;
        constexpr uint64_t kTagDerived = 9;
        constexpr uint64_t kTagNames = 10;
        constexpr uint64_t kTagAlternatives = 11;

        constexpr size_t kBytesPerWord = sizeof(uint64_t);

        /// FNV-1a over a canonical stream: every number as 8 little-endian bytes, every text with its length in front
        class LayoutHasher {
        public:
            explicit LayoutHasher(const TypeRegistry& registry) : _registry(registry) {}

            void Layout(const TypeInfo& info) {
                _visited.insert(info.Hash());
                const Vector<FieldRef> fields = _registry.AllFields(info);
                Mix(kTagFields);
                Mix(fields.size());
                for (const FieldRef& ref : fields) {
                    Mix(kTagField);
                    Mix(ref.field->name);
                    Desc(ref.field->type);
                }
            }

            [[nodiscard]] uint64_t Result() const {
                return _hash;
            }

        private:
            void Mix(uint64_t number) {
                for (size_t byte = 0; byte < kBytesPerWord; ++byte) {
                    MixByte(static_cast<uint8_t>(number >> (byte * CHAR_BIT)));
                }
            }

            void Mix(StringView text) {
                Mix(text.size());
                for (const char c : text) {
                    MixByte(static_cast<uint8_t>(c));
                }
            }

            void MixByte(uint8_t byte) {
                _hash ^= byte;
                _hash *= StringUtils::hashPrime;
            }

            void Desc(const TypeDesc* desc) {
                if (desc == nullptr) {
                    Mix(kTagAbsent);
                    return;
                }
                Mix(kTagDesc);
                Mix(TypeKindName(desc->kind));
                Mix(static_cast<uint64_t>(desc->resizable));
                Mix(desc->capacity);
                Mix(static_cast<uint64_t>(desc->interns));
                Mix(desc->enumBytes);
                Mix(kTagNames);
                Mix(desc->names.size());
                for (const StringView name : desc->names) {
                    Mix(name);
                }
                Desc(desc->element);
                Desc(desc->key);
                Mix(kTagAlternatives);
                Mix(desc->alternatives.size());
                for (const TypeDesc* alternative : desc->alternatives) {
                    Desc(alternative);
                }
                if (desc->kind == TypeKind::Object) {
                    Object(*desc);
                } else if (desc->kind == TypeKind::Pointer) {
                    Pointer(*desc);
                }
            }

            /// A type held by value: its layout the first time, its name after
            void Object(const TypeDesc& desc) {
                Mix(kTagObject);
                const TypeInfo* info = _registry.Find(desc.ObjectHash());
                if (info == nullptr) {
                    Mix(kTagUnregistered);
                    Mix(desc.ObjectName());
                    return;
                }
                Visit(*info);
            }

            /// The pointee and every type derived from it: a file names the type of a pointer's object, so the names are part of the layout
            void Pointer(const TypeDesc& desc) {
                Mix(kTagPointer);
                const TypeInfo* info = _registry.Find(desc.ObjectHash());
                if (info == nullptr) {
                    Mix(kTagUnregistered);
                    Mix(desc.ObjectName());
                    return;
                }
                Mix(info->Name());
                if (!_expanded.insert(info->Hash()).second) {
                    return;
                }
                Visit(*info);
                for (const TypeInfo* derived : _registry.Derived(*info, true)) {
                    Mix(kTagDerived);
                    Mix(derived->Name());
                    Visit(*derived);
                }
            }

            void Visit(const TypeInfo& info) {
                if (_visited.contains(info.Hash())) {
                    Mix(kTagRevisit);
                    Mix(info.Name());
                    return;
                }
                Layout(info);
            }

            const TypeRegistry& _registry;
            uint64_t _hash = StringUtils::hashOffset;
            HashSet<uint64_t> _visited;
            HashSet<uint64_t> _expanded;
        };

    }  // namespace

    uint64_t ComputeLayoutHash(const TypeRegistry& registry, const TypeInfo& root) {
        LayoutHasher hasher(registry);
        hasher.Layout(root);
        return hasher.Result();
    }

    Optional<uint64_t> ComputeLayoutHash(const TypeRegistry& registry, StringView rootTypeName) {
        const TypeInfo* root = registry.Find(rootTypeName);
        if (root == nullptr) {
            return std::nullopt;
        }
        return ComputeLayoutHash(registry, *root);
    }

    uint64_t ComputeLayoutHash(const TypeInfo& root) {
        return ComputeLayoutHash(TypeRegistry::GetInstance(), root);
    }

    Optional<uint64_t> ComputeLayoutHash(StringView rootTypeName) {
        return ComputeLayoutHash(TypeRegistry::GetInstance(), rootTypeName);
    }

    Optional<uint64_t> LayoutHashCache::Find(StringView rootTypeName) {
        const TypeInfo* root = _registry.Find(rootTypeName);
        if (root == nullptr) {
            return std::nullopt;
        }
        LockGuard lock(_mutex);
        const size_t registered = _registry.RegisteredCount();
        if (registered != _registeredCount) {
            _hashes.clear();
            _registeredCount = registered;
        }
        const auto found = _hashes.find(root);
        if (found != _hashes.end()) {
            return found->second;
        }
        ++_computations;
        const uint64_t layout = ComputeLayoutHash(_registry, *root);
        _hashes.emplace(root, layout);
        return layout;
    }

    size_t LayoutHashCache::Computations() const {
        LockGuard lock(_mutex);
        return _computations;
    }

    namespace {

        constexpr StringView kRootKey = "root";
        constexpr StringView kLayoutKey = "layout";

        constexpr StringView kCookAgain = "cook the content again with this build";

    }  // namespace

    bool WriteContentStamp(ISerializer& writer, StringView rootTypeName) {
        const Optional<uint64_t> layout = ComputeLayoutHash(rootTypeName);
        if (!layout.has_value()) {
            return false;
        }
        if (!writer.BeginObject(BinaryFormat::ContentStampName)) {
            return false;
        }
        writer.Write(kRootKey, rootTypeName);
        writer.Write(kLayoutKey, *layout);
        writer.EndObject();
        return true;
    }

    std::expected<ContentStamp, String> ReadContentStamp(IDeserializer& reader) {
        // A read of the wrong type records "Type mismatch" in binary; the stamp is a question, not a load, so it takes back what it caused
        const bool hadErrors = reader.HasErrors();
        const auto result = [&reader]() -> std::expected<ContentStamp, String> {
            if (!reader.BeginObject(BinaryFormat::ContentStampName)) {
                return std::unexpected(reader.HasKey(BinaryFormat::ContentStampName) ? String("the content stamp is not an object") : String("no content stamp"));
            }
            ContentStamp stamp;
            const bool hasRoot = reader.ReadAttribute(kRootKey, stamp.rootType);
            const bool hasLayout = reader.ReadAttribute(kLayoutKey, stamp.layout);
            reader.EndObject();
            if (!hasRoot || !hasLayout) {
                return std::unexpected(String("the content stamp needs 'root' as a text and 'layout' as a UInt64"));
            }
            return stamp;
        }();
        if (!hadErrors && reader.HasErrors()) {
            reader.ClearErrors();
        }
        return result;
    }

    std::expected<ContentStamp, String> CheckContentStamp(IDeserializer& reader, StringView sourceName) {
        // One cache for the program's registry, which is complete once `main` runs
        static LayoutHashCache programLayouts(TypeRegistry::GetInstance());
        return CheckContentStamp(reader, sourceName, programLayouts);
    }

    std::expected<ContentStamp, String> CheckContentStamp(IDeserializer& reader, StringView sourceName, LayoutHashCache& layouts) {
        std::expected<ContentStamp, String> stamp = ReadContentStamp(reader);
        if (!stamp.has_value()) {
            return std::unexpected(Format("{}: {} ({})", sourceName, stamp.error(), kCookAgain));
        }
        const Optional<uint64_t> layout = layouts.Find(stamp->rootType);
        if (!layout.has_value()) {
            return std::unexpected(Format("{}: the content stamp names the type '{}' (layout {:016x}), which this program does not have ({})", sourceName, stamp->rootType, stamp->layout, kCookAgain));
        }
        if (*layout != stamp->layout) {
            return std::unexpected(Format("{}: the layout of '{}' is {:016x} here and {:016x} in the file ({})", sourceName, stamp->rootType, *layout, stamp->layout, kCookAgain));
        }
        return stamp;
    }

}  // namespace BECore
