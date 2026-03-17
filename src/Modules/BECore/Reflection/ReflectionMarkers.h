#pragma once

/**
 * @file ReflectionMarkers.h
 * @brief Macros for marking classes, fields, and enums for reflection
 *
 * These macros are used by the reflection parser (meta_generator) to identify
 * types that need reflection metadata generation. The generated code provides
 * ReflectionTraits<T> specializations and static class methods, including
 * auto-generated Serialize() and Deserialize() methods.
 *
 * Usage:
 *   struct Player {
 *       BE_CLASS(Player)
 *
 *       BE_REFLECT_FIELD int32_t health = 100;
 *       BE_REFLECT_FIELD float speed = 5.0f;
 *       BE_REFLECT_FIELD BECore::Color _clearColor;  // XML name: clearColor
 *   };
 *
 * Include the generated file in the class's own .cpp only:
 *   #include <Generated/Player.gen.hpp>
 *
 * The generated code provides non-template definitions of:
 *   - void Serialize(ISerializer&) const  - for writing/saving
 *   - void Deserialize(IDeserializer&)    - for reading/loading
 *
 * Other .cpp files that call Serialize/Deserialize on this class do NOT need
 * to include the generated file — the linker resolves the definitions.
 *
 * Note: Underscore prefix in field names is automatically stripped for XML
 * element/attribute names (e.g., _clearColor → clearColor).
 */

namespace BECore {

    // Forward declaration for friend access
    // Default argument specified here; must not be repeated in TypeTraits.h definition
    template <typename T, typename = void>
    struct ReflectionTraits;

    // Forward declaration for embedded type identification
    struct ClassMeta;

    // Forward declarations for non-template Serialize/Deserialize signatures
    class ISerializer;
    class IDeserializer;

}  // namespace BECore

// =============================================================================
// BE_CLASS - Main reflection macro for classes/structs
// =============================================================================
// Place inside class body. Declares static reflection methods that are
// defined in the generated .gen.hpp file.
//
// Usage:
//   BE_CLASS(ClassName)                                 - Standard reflection
//   BE_CLASS(ClassName, FACTORY_BASE)                   - Mark as factory base for enum generation
//   BE_CLASS(ClassName, FACTORY_BASE, SERIALIZABLE)     - Multiple options
//
// @param ClassName The name of the containing class (required for static methods)
// @param ...       Optional: Comma-separated options (FACTORY_BASE, SERIALIZABLE, etc.)
//                  These options are parsed by reflector.py, not by C++ preprocessor
// =============================================================================

#define BE_CLASS(ClassName, ...)                                                                                                                                                                       \
public:                                                                                                                                                                                                \
    /** @brief Get the type name as string_view */                                                                                                                                                     \
    static eastl::string_view GetStaticTypeName();                                                                                                                                                     \
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
                                                                                                                                                                                                       \
private:                                                                                                                                                                                               \
    template <typename, typename>                                                                                                                                                                      \
    friend struct ::BECore::ReflectionTraits;                                                                                                                                                          \
                                                                                                                                                                                                       \
public:

// =============================================================================
// BE_EVENT - Lightweight reflection macro for event types
// =============================================================================
// Place inside event struct body. Similar to BE_CLASS but without virtual dispatch
// overhead (0 bytes per instance). Events don't need runtime polymorphism.
//
// Usage:
//   struct MyEvent {
//       BE_EVENT(MyEvent)
//       BE_REFLECT_FIELD int32_t value = 0;
//   };
//
// Key differences from BE_CLASS:
// - No virtual GetTypeMeta() / vptr overhead (events are never used through base pointers)
// - No GetTypeMeta() instance method (type always known statically)
// - No Is<T>()/Cast<T>() (events don't need polymorphism)
// - Supports BE_REFLECT_FIELD for data serialization/logging
//
// @param EventName The name of the containing event struct
// =============================================================================

#define BE_EVENT(EventName)                                                                                                                                                                            \
public:                                                                                                                                                                                                \
    /** @brief Get the event type name as string_view */                                                                                                                                               \
    static eastl::string_view GetStaticTypeName();                                                                                                                                                     \
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
                                                                                                                                                                                                       \
private:                                                                                                                                                                                               \
    template <typename, typename>                                                                                                                                                                      \
    friend struct ::BECore::ReflectionTraits;                                                                                                                                                          \
                                                                                                                                                                                                       \
public:

// =============================================================================
// Field and Enum markers
// =============================================================================

// Clang/GCC annotation macros for reflection parser
#if defined(__clang__) || defined(__GNUC__)
/**
 * @brief Mark a field for reflection
 *
 * Only fields marked with this macro will be included in the generated
 * ReflectionTraits<T>::fields tuple.
 */
#define BE_REFLECT_FIELD [[clang::annotate("reflect_field")]]

/**
 * @brief Mark a method for reflection
 *
 * Methods marked with this macro will be included in the generated
 * ReflectionTraits<T>::methods tuple and can be invoked via reflection.
 *
 * @example
 *   class MyClass {
 *       BE_CLASS(MyClass)
 *   public:
 *       BE_FUNCTION void Update();
 *       BE_FUNCTION bool Initialize(const XmlNode& node);
 *   };
 */
#define BE_FUNCTION [[clang::annotate("reflect_function")]]
#else
// MSVC: use comment markers as fallback since MSVC lacks [[annotate]]
// The reflection parser detects these via regex patterns
#define BE_REFLECT_FIELD /* BE_REFLECT_FIELD */
#define BE_FUNCTION      /* BE_FUNCTION */
#endif
