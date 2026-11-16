#include "Context.h"

#include <BECore/Reflection/CompiledPath.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/PropertyPath.h>

namespace BECore {

    namespace {

        /// A list in a message is cut here, so a type with a hundred fields does not bury the line
        constexpr size_t kMaxListedNames = 24;

        /// The start of every key: a mask with no level has this key, whatever the counters do
        constexpr uint64_t kFoldSeed = 0x9e3779b97f4a7c15ULL;

        /// The finalizer of splitmix64: a bijection of 64 bits in which every input bit moves about half of the output bits
        constexpr uint64_t MixBits(uint64_t value) {
            constexpr uint64_t kFirstMultiplier = 0xbf58476d1ce4e5b9ULL;
            constexpr uint64_t kSecondMultiplier = 0x94d049bb133111ebULL;
            constexpr unsigned kFirstShift = 30;
            constexpr unsigned kSecondShift = 27;
            constexpr unsigned kLastShift = 31;
            value = (value ^ (value >> kFirstShift)) * kFirstMultiplier;
            value = (value ^ (value >> kSecondShift)) * kSecondMultiplier;
            return value ^ (value >> kLastShift);
        }

        std::unexpected<LinkError> Refuse(LinkErrorCode code, String message) {
            return std::unexpected(LinkError{code, std::move(message)});
        }

        String JoinNames(const Vector<String>& names) {
            String text;
            const size_t listed = std::min(names.size(), kMaxListedNames);
            for (size_t i = 0; i < listed; ++i) {
                text += i == 0 ? "" : ", ";
                text += names[i];
            }
            if (names.size() > listed) {
                text += ", ...";
            }
            return text;
        }

        /// The type as a message says it: the name of a class or of an opaque type, the values of an enum, the kind of the rest
        String DescribeForMessage(const TypeDesc& type) {
            switch (type.kind) {
                case TypeKind::Object:
                case TypeKind::Pointer:
                    return String(type.ObjectName());
                case TypeKind::Opaque:
                    return String(type.opaqueName);
                case TypeKind::Enum: {
                    Vector<String> values;
                    for (const StringView value : type.names) {
                        values.push_back(String(value));
                    }
                    return Format("enum {{{}}}", JoinNames(values));
                }
                case TypeKind::List:
                    return Format("list of {}", type.element != nullptr ? DescribeForMessage(*type.element) : String("?"));
                default:
                    return String(TypeKindName(type.kind));
            }
        }

        Vector<String> FieldNames(const TypeInfo& type, const TypeRegistry& registry) {
            Vector<String> names;
            for (const FieldRef& field : registry.AllFields(type)) {
                names.push_back(String(field.field->name));
            }
            return names;
        }

        String LevelNames(const ContextDecl& declaration) {
            Vector<String> names;
            for (const ContextLevel& level : declaration.Levels()) {
                names.push_back(level.name);
            }
            return names.empty() ? String("none") : JoinNames(names);
        }

        /// The message of a path that CompilePath found no field for: which type was asked, what it has, the nearest name
        LinkError UnknownFieldError(const ContextLevel& level, StringView path, const PropertyPath& rest, const TypeRegistry& registry, const PropertyError& original) {
            // The longest prefix that compiles is the owner of the segment that failed
            const TypeInfo* owner = level.type;
            String ownerPath(level.name);
            for (size_t i = 0; i < rest.segments.size(); ++i) {
                const PathSegment& segment = rest.segments[i];
                if (segment.kind != PathSegmentKind::Field || owner == nullptr) {
                    break;
                }
                const Vector<String> names = FieldNames(*owner, registry);
                if (std::find(names.begin(), names.end(), segment.text) == names.end()) {
                    const String hint = IDeserializer::MakeDidYouMeanHint(segment.text, {&names});
                    return LinkError{LinkErrorCode::unknown_field, Format("path '{}': the type '{}' ({}) has no field '{}'{}; its fields are: {}", path, owner->Name(),
                                                                          i == 0 ? Format("level '{}'", level.name) : Format("'{}'", ownerPath), segment.text, hint, JoinNames(names))};
                }
                const auto prefix = CompilePath(*level.type, FormatPropertyPath(rest.segments, i + 1), registry);
                if (!prefix.has_value() || prefix->type == nullptr) {
                    break;
                }
                ownerPath = Format("{}.{}", ownerPath, segment.text);
                owner = prefix->type->kind == TypeKind::Object ? registry.Find(prefix->type->ObjectHash()) : nullptr;
            }
            return LinkError{LinkErrorCode::unknown_field, Format("path '{}': {}", path, original.message)};
        }

    }  // namespace

    std::expected<LevelIndex, LinkError> ContextDecl::Add(StringView name, StringView typeName, const TypeRegistry& registry) {
        if (name.empty()) {
            return Refuse(LinkErrorCode::invalid_path, "a context level needs a name");
        }
        if (IndexOf(name).has_value()) {
            return Refuse(LinkErrorCode::duplicate_level, Format("the context already has a level '{}'", name));
        }
        if (_levels.size() >= kMaxContextLevels) {
            return Refuse(LinkErrorCode::too_many_levels, Format("the level '{}' is one too many: a context has at most {} levels ({})", name, kMaxContextLevels, LevelNames(*this)));
        }
        const TypeInfo* type = registry.Find(typeName);
        if (type == nullptr) {
            Vector<String> typeNames;
            typeNames.reserve(registry.All().size());
            for (const TypeInfo* known : registry.All()) {
                typeNames.push_back(String(known->Name()));
            }
            return Refuse(LinkErrorCode::unknown_type,
                          Format("the level '{}' names the type '{}', which is not a registered reflected type{}", name, typeName, IDeserializer::MakeDidYouMeanHint(typeName, {&typeNames})));
        }
        _levels.push_back(ContextLevel{String(name), type});
        return static_cast<LevelIndex>(_levels.size() - 1);
    }

    Optional<LevelIndex> ContextDecl::IndexOf(StringView name) const {
        for (size_t i = 0; i < _levels.size(); ++i) {
            if (_levels[i].name == name) {
                return static_cast<LevelIndex>(i);
            }
        }
        return std::nullopt;
    }

    void Context::Store(LevelIndex level, const void* object, const uint64_t* version) {
        _objects[level] = object;
        _versions[level] = version;
        ++_sets[level];
    }

    uint64_t Context::VersionKey(LevelMask levels) const {
        uint64_t key = kFoldSeed;
        for (LevelIndex level = 0; level < kMaxContextLevels; ++level) {
            if ((levels & LevelBit(level)) == 0) {
                continue;
            }
            // Each part goes through the mix on its own, so two parts of one level cannot cancel each other and the levels keep their place
            key = MixBits(key ^ level);
            key = MixBits(key ^ Version(level));
            key = MixBits(key ^ _sets[level]);
        }
        return key;
    }

    void Context::SetObject(LevelIndex level, const void* object, const ClassMeta& type, const uint64_t* version) {
        ASSERT(level < kMaxContextLevels);
        if (level >= kMaxContextLevels) {
            return;
        }
        if (_declaration == nullptr) {
            Store(level, object, version);
            return;
        }
        ASSERT(level < _declaration->Size(), "Context::Set: the level is not declared");
        if (level >= _declaration->Size()) {
            return;
        }
        const TypeInfo& declared = *_declaration->Levels()[level].type;
        if (declared.Hash() == type.typeHash) {
            Store(level, object, version);
            return;
        }
        // An object of a derived type is stored at the subobject of the declared type, which is where the offsets of the declared type apply
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* actual = registry.Find(type.typeHash);
        const Optional<size_t> offset = actual != nullptr ? registry.OffsetOfBase(*actual, declared) : std::nullopt;
        ASSERT(offset.has_value(), "Context::Set: the object is not of the type the level declares, nor derived from it");
        Store(level, offset.has_value() ? static_cast<const char*>(object) + *offset : nullptr, version);
    }

    std::expected<SlotRef, LinkError> LinkSlot(const ContextDecl& declaration, StringView path, const TypeDesc& expected, const TypeRegistry& registry) {
        const auto parsed = ParsePropertyPath(path);
        if (!parsed.has_value()) {
            return Refuse(LinkErrorCode::invalid_path, Format("path '{}': {} (at byte {})", path, parsed.error().message, parsed.error().position));
        }
        const Vector<PathSegment>& segments = parsed->segments;
        const String levelName = segments.front().text;

        const Optional<LevelIndex> index = declaration.IndexOf(levelName);
        if (!index.has_value()) {
            Vector<String> names;
            for (const ContextLevel& level : declaration.Levels()) {
                names.push_back(level.name);
            }
            const String hint = IDeserializer::MakeDidYouMeanHint(levelName, {&names});
            return Refuse(LinkErrorCode::unknown_level, Format("path '{}': unknown level '{}'{}; the declared levels are: {}", path, levelName, hint, LevelNames(declaration)));
        }
        const ContextLevel& level = declaration.Levels()[*index];

        if (segments.size() == 1) {
            return Refuse(LinkErrorCode::invalid_path, Format("path '{}': '{}' is a level, a path names a field of it (level.field)", path, levelName));
        }
        if (segments[1].kind != PathSegmentKind::Field) {
            return Refuse(LinkErrorCode::invalid_path, Format("path '{}': the level '{}' is an object, it has no elements", path, levelName));
        }

        PropertyPath rest;
        for (size_t i = 1; i < segments.size(); ++i) {
            rest.segments.push_back(segments[i]);
        }
        const auto compiled = CompilePath(*level.type, FormatPropertyPath(rest.segments, rest.segments.size()), registry);
        if (!compiled.has_value()) {
            if (compiled.error().code == PropertyErrorCode::not_found) {
                return std::unexpected(UnknownFieldError(level, path, rest, registry, compiled.error()));
            }
            return Refuse(LinkErrorCode::invalid_path, Format("path '{}': {}", path, compiled.error().message));
        }
        if (!compiled->IsFixed()) {
            return Refuse(
                LinkErrorCode::not_fixed,
                Format("path '{}': the part '{}' needs the live object (a list element, a map value, an optional or a pointee), so a value cannot read it from the context", path, compiled->rest));
        }
        if (!SameType(expected, *compiled->type)) {
            return Refuse(LinkErrorCode::type_mismatch, Format("path '{}': the field '{}' of '{}' is a {}, but a value of {} is asked for", path, rest.segments.back().text, level.type->Name(),
                                                               DescribeForMessage(*compiled->type), DescribeForMessage(expected)));
        }
        if (compiled->offset > std::numeric_limits<uint32_t>::max()) {
            return Refuse(LinkErrorCode::too_far, Format("path '{}': the field is {} bytes into '{}', more than a slot can address", path, compiled->offset, level.type->Name()));
        }
        return SlotRef{*index, static_cast<uint32_t>(compiled->offset)};
    }

}  // namespace BECore
