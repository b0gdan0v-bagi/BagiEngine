#pragma once

#include <BECore/Types/Types.h>

/**
 * @file ReflectionMarkers.h
 * @brief The markers meta_generator reads from the source text: BE_CLASS, BE_EVENT, BE_REFLECT_FIELD, BE_FIELD_ATTRS, BE_FUNCTION.
 *
 * The generated `Generated/<Stem>.gen.hpp` is included by the class's own .cpp only: it holds non-template definitions,
 * so other callers of Serialize / Deserialize / WriteJson link against them without including it. `WriteJson` writes the object
 * `Serialize` saves, keys sorted; a `BE_CLASS` also has `WriteJsonMembers`, which a class with a reflected parent
 * merges with the members of the classes above it (Control/JsonWriteReflect.h).
 */

namespace BECore {

    // The default argument is given here only: repeating it in the definition (TypeTraits.h) is ill-formed
    template <typename T, typename = void>
    struct ReflectionTraits;

    struct ClassMeta;

    class ISerializer;
    class IDeserializer;
    class JsonWriter;

    class IPropertyVisitor;

}  // namespace BECore

namespace BECore::JsonWrite {

    class MemberMerge;

}  // namespace BECore::JsonWrite

// =============================================================================
// BE_CLASS - inside the class body; declares what the generated .gen.hpp defines
// =============================================================================
//
// Usage:
//   BE_CLASS(ClassName)                                 - Standard reflection
//   BE_CLASS(ClassName, FACTORY_BASE)                   - Mark as factory base for enum generation
//   BE_CLASS(ClassName, FACTORY_BASE, ELEMENT=sink)     - Options combine; ELEMENT names the XML element of the base's arrays
//   BE_CLASS(ClassName, FACTORY_BASE, REGISTER_SELF)    - A concrete base registers itself beside its derived classes
//   BE_CLASS(ClassName, ICON=image)                     - Lucide icon name for the editor, read into ClassMeta::icon
//                                                         (kebab case [a-z0-9-]+; other values warn and are ignored)
//   BE_CLASS(ClassName, PURPOSE=visual)                 - Part of an asset the class belongs to (sim, visual or editor),
//                                                         read into ClassMeta::purpose; without it the class is Common,
//                                                         which every part may hold (other values warn and are ignored)
//
// @param ClassName The name of the containing class (required for static methods)
// @param ...       Options, read by meta_generator, not by the preprocessor (an unknown one is ignored without a warning)
// =============================================================================

#define BE_CLASS(ClassName, ...)                                                                                                                                                                       \
public:                                                                                                                                                                                                \
    /** @brief Get the type name as string_view */                                                                                                                                                     \
    static BECore::StringView GetStaticTypeName();                                                                                                                                                     \
    /** @brief Get the number of reflected fields */                                                                                                                                                   \
    static size_t GetStaticFieldCount();                                                                                                                                                               \
    /** @brief Iterate over all reflected fields (mutable) */                                                                                                                                          \
    template <typename Func>                                                                                                                                                                           \
    static constexpr void ForEachFieldStatic(ClassName& obj, Func&& func);                                                                                                                             \
    /** @brief Iterate over all reflected fields (const) */                                                                                                                                            \
    template <typename Func>                                                                                                                                                                           \
    static constexpr void ForEachFieldStatic(const ClassName& obj, Func&& func);                                                                                                                       \
    /** @brief Get static ClassMeta for this type (defined in generated code) */                                                                                                                       \
    static const ::BECore::ClassMeta& GetStaticTypeMeta();                                                                                                                                             \
    /** @brief Get runtime ClassMeta for this instance (virtual -- correct type returned through base pointer) */                                                                                      \
    virtual const ::BECore::ClassMeta& GetTypeMeta() const {                                                                                                                                           \
        return GetStaticTypeMeta();                                                                                                                                                                    \
    }                                                                                                                                                                                                  \
    /** @brief Check if this instance is of type T */                                                                                                                                                  \
    template <typename T>                                                                                                                                                                              \
    bool Is() const {                                                                                                                                                                                  \
        return GetTypeMeta() == T::GetStaticTypeMeta();                                                                                                                                                \
    }                                                                                                                                                                                                  \
    /** @brief Cast to type T, returns nullptr if type mismatch */                                                                                                                                     \
    template <typename T>                                                                                                                                                                              \
    T* Cast() {                                                                                                                                                                                        \
        return Is<T>() ? static_cast<T*>(this) : nullptr;                                                                                                                                              \
    }                                                                                                                                                                                                  \
    /** @brief Cast to type T (const), returns nullptr if type mismatch */                                                                                                                             \
    template <typename T>                                                                                                                                                                              \
    const T* Cast() const {                                                                                                                                                                            \
        return Is<T>() ? static_cast<const T*>(this) : nullptr;                                                                                                                                        \
    }                                                                                                                                                                                                  \
    /** @brief Serialize all reflected fields to ISerializer (write/save) */                                                                                                                           \
    virtual void Serialize(::BECore::ISerializer& s) const;                                                                                                                                            \
    /** @brief Deserialize all reflected fields from IDeserializer (read/load) */                                                                                                                      \
    virtual void Deserialize(::BECore::IDeserializer& d);                                                                                                                                              \
    /** @brief Write the reflected fields as one JSON object (keys sorted, as `Serialize` saves them) */                                                                                               \
    virtual void WriteJson(::BECore::JsonWriter& w) const;                                                                                                                                             \
    /** @brief Write the members of this class into the object a JsonWriter has open, merged by key with @p outer, the classes below it (JsonWriteReflect.h) */                                        \
    virtual void WriteJsonMembers(::BECore::JsonWriter& w, ::BECore::JsonWrite::MemberMerge* outer) const;                                                                                             \
    /** @brief Visit all reflected fields for in-place editing (e.g. ImGui inspector) */                                                                                                               \
    virtual bool AcceptPropertyVisitor(::BECore::IPropertyVisitor& v);                                                                                                                                 \
                                                                                                                                                                                                       \
private:                                                                                                                                                                                               \
    template <typename, typename>                                                                                                                                                                      \
    friend struct ::BECore::ReflectionTraits;                                                                                                                                                          \
                                                                                                                                                                                                       \
public:

// =============================================================================
// BE_EVENT - BE_CLASS without virtual functions (no vptr): an event is never used through a base pointer, so it has
// no GetTypeMeta(), Is<T>() or Cast<T>() and no AcceptPropertyVisitor.
// =============================================================================

#define BE_EVENT(EventName)                                                                                                                                                                            \
public:                                                                                                                                                                                                \
    /** @brief Get the event type name as string_view */                                                                                                                                               \
    static BECore::StringView GetStaticTypeName();                                                                                                                                                     \
    /** @brief Get static ClassMeta for this event type (defined in generated code) */                                                                                                                 \
    static const ::BECore::ClassMeta& GetStaticTypeMeta();                                                                                                                                             \
    /** @brief Get event type hash for efficient dispatch */                                                                                                                                           \
    static uint64_t GetStaticTypeHash();                                                                                                                                                               \
    /** @brief Get the number of reflected fields */                                                                                                                                                   \
    static size_t GetStaticFieldCount();                                                                                                                                                               \
    /** @brief Iterate over all reflected fields (const) */                                                                                                                                            \
    template <typename Func>                                                                                                                                                                           \
    static constexpr void ForEachFieldStatic(const EventName& obj, Func&& func);                                                                                                                       \
    /** @brief Serialize all reflected fields to ISerializer (write/save) */                                                                                                                           \
    void Serialize(::BECore::ISerializer& s) const;                                                                                                                                                    \
    /** @brief Deserialize all reflected fields from IDeserializer (read/load) */                                                                                                                      \
    void Deserialize(::BECore::IDeserializer& d);                                                                                                                                                      \
    /** @brief Write the reflected fields as one JSON object (keys sorted, as `Serialize` saves them) */                                                                                               \
    void WriteJson(::BECore::JsonWriter& w) const;                                                                                                                                                     \
                                                                                                                                                                                                       \
private:                                                                                                                                                                                               \
    template <typename, typename>                                                                                                                                                                      \
    friend struct ::BECore::ReflectionTraits;                                                                                                                                                          \
                                                                                                                                                                                                       \
public:

#if defined(__clang__) || defined(__GNUC__)
/// Marks a field for ReflectionTraits<T>::fields; written on the same line as the declaration.
#define BE_REFLECT_FIELD [[clang::annotate("reflect_field")]]

/**
 * @brief Marks a method for ReflectionTraits<T>::methods: it is listed in the type's schema and, when `MethodDesc::callable`, found
 * and called by name from outside (`CallMethod`); `InvokeMethod` calls it by name with arguments known at compile time.
 *
 * A method nothing outside may call (a lifecycle override the loader calls through its base) is not marked.
 *
 * @example
 *   class MyClass {
 *       BE_CLASS(MyClass)
 *   public:
 *       BE_FUNCTION void Update();
 *       BE_FUNCTION bool Initialize(IDeserializer& deserializer);
 *   };
 */
#define BE_FUNCTION [[clang::annotate("reflect_function")]]
#else
// MSVC has no [[clang::annotate]]: the markers expand to nothing; meta_generator reads their names from the source text either way
#define BE_REFLECT_FIELD /* BE_REFLECT_FIELD */
#define BE_FUNCTION      /* BE_FUNCTION */
#endif

/**
 * @brief Attributes of a reflected field, written right after `BE_REFLECT_FIELD`
 *
 * Expands to nothing on every compiler: only the meta generator reads it, from the source text, and writes the values into
 * `ReflectionTraits<T>::fields` as a `FieldAttributes` (`Reflection/FieldAttributes.h`). It is not an argument of
 * `BE_REFLECT_FIELD` so that the marker keeps its one form everywhere.
 *
 * Attributes (comma separated, each at most once): `range(min, max)`, `step(s)`, `tooltip("text")`, `readonly`, `hidden`, `required`,
 * `asset(Texture | Font | Sound | UiDocument)`. `range` and `step` need a numeric field, `asset` a string one. An attribute the generator cannot use is a reflection warning.
 * `no_factory` is the generator's own flag, not a `FieldAttributes` member: the `IntrusivePtr<Base>` of the field is not a factory field, so the
 * generated file inlines no `FactoryTraits<Base>` for it (the scene tree: `Scene::_nodes`, `SceneNode::_children`).
 *
 * @example
 *   BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1), step(0.05), tooltip("Share of the screen")) float _opacity = 1.0f;
 */
#define BE_FIELD_ATTRS(...)
