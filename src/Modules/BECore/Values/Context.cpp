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

        /// Where a path starts, for the messages: a level of the declaration (`caster`) or the element of a list, which has no name of its own
        struct PathOrigin {
            const TypeInfo* type = nullptr;
            /// `level 'caster'`; empty for an element
            String label;
            /// What a nested owner is called after the start: `caster` for a level, empty for an element
            String start;
        };

        /// The message of a path that CompilePath found no field for: which type was asked, what it has, the nearest name
        LinkError UnknownFieldError(const PathOrigin& origin, StringView path, const PropertyPath& rest, const TypeRegistry& registry, const PropertyError& original) {
            // The longest prefix that compiles is the owner of the segment that failed
            const TypeInfo* owner = origin.type;
            String ownerPath = origin.start;
            for (size_t i = 0; i < rest.segments.size(); ++i) {
                const PathSegment& segment = rest.segments[i];
                if (segment.kind != PathSegmentKind::Field || owner == nullptr) {
                    break;
                }
                const Vector<String> names = FieldNames(*owner, registry);
                if (std::find(names.begin(), names.end(), segment.text) == names.end()) {
                    const String hint = IDeserializer::MakeDidYouMeanHint(segment.text, {&names});
                    const String where = i == 0 ? origin.label : Format("'{}'", ownerPath);
                    return LinkError{LinkErrorCode::unknown_field, Format("path '{}': the type '{}'{} has no field '{}'{}; its fields are: {}", path, owner->Name(),
                                                                          where.empty() ? String() : Format(" ({})", where), segment.text, hint, JoinNames(names))};
                }
                const auto prefix = CompilePath(*origin.type, FormatPropertyPath(rest.segments, i + 1), registry);
                if (!prefix.has_value() || prefix->type == nullptr) {
                    break;
                }
                ownerPath = ownerPath.empty() ? segment.text : Format("{}.{}", ownerPath, segment.text);
                owner = prefix->type->kind == TypeKind::Object ? registry.Find(prefix->type->ObjectHash()) : nullptr;
            }
            return LinkError{LinkErrorCode::unknown_field, Format("path '{}': {}", path, original.message)};
        }

        /// The fields of @p rest compiled against the origin's type, which must name one fixed place: the unknown field and the not fixed refusals of every link function
        std::expected<CompiledPath, LinkError> CompileFixed(const PathOrigin& origin, StringView path, const PropertyPath& rest, const TypeRegistry& registry) {
            const auto compiled = CompilePath(*origin.type, FormatPropertyPath(rest.segments, rest.segments.size()), registry);
            if (!compiled.has_value()) {
                if (compiled.error().code == PropertyErrorCode::not_found) {
                    return std::unexpected(UnknownFieldError(origin, path, rest, registry, compiled.error()));
                }
                return Refuse(LinkErrorCode::invalid_path, Format("path '{}': {}", path, compiled.error().message));
            }
            if (!compiled->IsFixed()) {
                return Refuse(
                    LinkErrorCode::not_fixed,
                    Format("path '{}': the part '{}' needs the live object (a list element, a map value, an optional or a pointee), so a value cannot read it from the context", path, compiled->rest));
            }
            return *compiled;
        }

        /// A path split at its level: the level of the declaration and the fields after it
        struct LevelPath {
            LevelIndex index = 0;
            const ContextLevel* level = nullptr;
            PropertyPath rest;
        };

        std::expected<LevelPath, LinkError> SplitLevel(const ContextDecl& declaration, StringView path) {
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
            if (segments.size() == 1) {
                return Refuse(LinkErrorCode::invalid_path, Format("path '{}': '{}' is a level, a path names a field of it (level.field)", path, levelName));
            }
            if (segments[1].kind != PathSegmentKind::Field) {
                return Refuse(LinkErrorCode::invalid_path, Format("path '{}': the level '{}' is an object, it has no elements", path, levelName));
            }
            LevelPath split;
            split.index = *index;
            split.level = &declaration.Levels()[*index];
            for (size_t i = 1; i < segments.size(); ++i) {
                split.rest.segments.push_back(segments[i]);
            }
            return split;
        }

        PathOrigin OriginOf(const ContextLevel& level) {
            return PathOrigin{level.type, Format("level '{}'", level.name), level.name};
        }

        std::expected<uint32_t, LinkError> OffsetOf(const CompiledPath& compiled, StringView path, const TypeInfo& owner) {
            if (compiled.offset > std::numeric_limits<uint32_t>::max()) {
                return Refuse(LinkErrorCode::too_far, Format("path '{}': the field is {} bytes into '{}', more than a slot can address", path, compiled.offset, owner.Name()));
            }
            return static_cast<uint32_t>(compiled.offset);
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

    void Context::Store(LevelIndex level, const void* object, const uint64_t* version, bool writable) {
        _objects[level] = object;
        _versions[level] = version;
        ++_sets[level];
        _mutable = writable ? static_cast<LevelMask>(_mutable | LevelBit(level)) : static_cast<LevelMask>(_mutable & ~LevelBit(level));
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
            key = MixBits(key ^ reinterpret_cast<uintptr_t>(_objects[level]));
            key = MixBits(key ^ reinterpret_cast<uintptr_t>(_versions[level]));
        }
        return key;
    }

    bool Context::IsVersioned(LevelMask levels) const {
        for (LevelIndex level = 0; level < kMaxContextLevels; ++level) {
            if ((levels & LevelBit(level)) != 0 && (_objects[level] == nullptr || _versions[level] == nullptr)) {
                return false;
            }
        }
        return true;
    }

    void Context::SetObject(LevelIndex level, const void* object, const ClassMeta& type, const uint64_t* version, bool writable) {
        ASSERT(level < kMaxContextLevels);
        if (level >= kMaxContextLevels) {
            return;
        }
        if (_declaration == nullptr) {
            Store(level, object, version, writable);
            return;
        }
        ASSERT(level < _declaration->Size(), "Context::Set: the level is not declared");
        if (level >= _declaration->Size()) {
            return;
        }
        const TypeInfo& declared = *_declaration->Levels()[level].type;
        if (declared.Hash() == type.typeHash) {
            Store(level, object, version, writable);
            return;
        }
        // An object of a derived type is stored at the subobject of the declared type, which is where the offsets of the declared type apply
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* actual = registry.Find(type.typeHash);
        const Optional<size_t> offset = actual != nullptr ? registry.OffsetOfBase(*actual, declared) : std::nullopt;
        ASSERT(offset.has_value(), "Context::Set: the object is not of the type the level declares, nor derived from it");
        Store(level, offset.has_value() ? static_cast<const char*>(object) + *offset : nullptr, version, writable && offset.has_value());
    }

    namespace {

        /// The one linking both entry points share; @p expected is null for `LinkField`, which takes the field's type as it is
        std::expected<LinkedField, LinkError> LinkPath(const ContextDecl& declaration, StringView path, const TypeDesc* expected, const TypeRegistry& registry) {
            const auto split = SplitLevel(declaration, path);
            if (!split.has_value()) {
                return std::unexpected(split.error());
            }
            const ContextLevel& level = *split->level;
            const auto compiled = CompileFixed(OriginOf(level), path, split->rest, registry);
            if (!compiled.has_value()) {
                return std::unexpected(compiled.error());
            }
            if (expected != nullptr && !SameType(*expected, *compiled->type)) {
                return Refuse(LinkErrorCode::type_mismatch, Format("path '{}': the field '{}' of '{}' is a {}, but a value of {} is asked for", path, split->rest.segments.back().text,
                                                                   level.type->Name(), DescribeForMessage(*compiled->type), DescribeForMessage(*expected)));
            }
            const auto offset = OffsetOf(*compiled, path, *level.type);
            if (!offset.has_value()) {
                return std::unexpected(offset.error());
            }
            return LinkedField{SlotRef{split->index, *offset}, compiled->type};
        }

    }  // namespace

    std::expected<LinkedField, LinkError> LinkField(const ContextDecl& declaration, StringView path, const TypeRegistry& registry) {
        return LinkPath(declaration, path, nullptr, registry);
    }

    std::expected<SlotRef, LinkError> LinkSlot(const ContextDecl& declaration, StringView path, const TypeDesc& expected, const TypeRegistry& registry) {
        const auto linked = LinkPath(declaration, path, &expected, registry);
        if (!linked.has_value()) {
            return std::unexpected(linked.error());
        }
        return linked->slot;
    }

    std::expected<ListLink, LinkError> LinkList(const ContextDecl& declaration, StringView path, const TypeRegistry& registry) {
        const auto split = SplitLevel(declaration, path);
        if (!split.has_value()) {
            return std::unexpected(split.error());
        }
        const ContextLevel& level = *split->level;
        const auto compiled = CompileFixed(OriginOf(level), path, split->rest, registry);
        if (!compiled.has_value()) {
            return std::unexpected(compiled.error());
        }
        if (compiled->type->kind != TypeKind::List) {
            return Refuse(LinkErrorCode::not_a_list, Format("path '{}': the field '{}' of '{}' is a {}, but a list field (an Array, Vector, SmallVector or FixedVector) is wanted", path,
                                                            split->rest.segments.back().text, level.type->Name(), DescribeForMessage(*compiled->type)));
        }
        const auto element = DescribeListElement(*compiled->type, registry);
        if (!element.has_value()) {
            return Refuse(element.error().code, Format("path '{}': {}", path, element.error().message));
        }
        const auto offset = OffsetOf(*compiled, path, *level.type);
        if (!offset.has_value()) {
            return std::unexpected(offset.error());
        }
        return ListLink{split->index, *offset, compiled->type};
    }

    std::expected<ElementLink, LinkError> LinkElementField(const TypeInfo& element, StringView path, const TypeRegistry& registry) {
        const auto parsed = ParsePropertyPath(path);
        if (!parsed.has_value()) {
            return Refuse(LinkErrorCode::invalid_path, Format("path '{}': {} (at byte {})", path, parsed.error().message, parsed.error().position));
        }
        if (parsed->segments.front().kind != PathSegmentKind::Field) {
            return Refuse(LinkErrorCode::invalid_path, Format("path '{}': an element is an object, it has no elements of its own; a path starts with a field", path));
        }
        const auto compiled = CompileFixed(PathOrigin{&element, {}, {}}, path, *parsed, registry);
        if (!compiled.has_value()) {
            return std::unexpected(compiled.error());
        }
        const auto offset = OffsetOf(*compiled, path, element);
        if (!offset.has_value()) {
            return std::unexpected(offset.error());
        }
        return ElementLink{*offset, compiled->type};
    }

    std::expected<const TypeInfo*, LinkError> DescribeListElement(const TypeDesc& list, const TypeRegistry& registry) {
        constexpr StringView kAllowed = "a list of scalars, enums, math types, PoolStrings or reflected objects";
        if (list.kind != TypeKind::List || list.element == nullptr) {
            return Refuse(LinkErrorCode::not_a_list, Format("the type is a {}, not a list", DescribeForMessage(list)));
        }
        const TypeDesc& element = *list.element;
        switch (element.kind) {
            case TypeKind::Bool:
            case TypeKind::Int8:
            case TypeKind::Int16:
            case TypeKind::Int32:
            case TypeKind::Int64:
            case TypeKind::UInt8:
            case TypeKind::UInt16:
            case TypeKind::UInt32:
            case TypeKind::UInt64:
            case TypeKind::Float:
            case TypeKind::Double:
            case TypeKind::Enum:
            case TypeKind::Opaque:
                return nullptr;
            case TypeKind::String:
                if (element.interns) {
                    return nullptr;
                }
                return Refuse(LinkErrorCode::bad_element,
                              Format("a list of strings that own their text cannot be read by a formula: {} is the most it may hold (a PoolString, not a String)", kAllowed));
            case TypeKind::Object: {
                const TypeInfo* type = registry.Find(element.ObjectHash());
                if (type == nullptr) {
                    return Refuse(LinkErrorCode::bad_element, Format("the element type '{}' is not registered with the reflection, so its fields are not known", element.ObjectName()));
                }
                return type;
            }
            default:
                return Refuse(LinkErrorCode::bad_element, Format("a list of {} cannot be read by a formula: it may be {}; not a list of pointers, maps, optionals, variants, pairs or lists",
                                                                 DescribeForMessage(element), kAllowed));
        }
    }

}  // namespace BECore
