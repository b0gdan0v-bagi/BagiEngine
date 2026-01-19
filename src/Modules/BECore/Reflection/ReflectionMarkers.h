#pragma once

/**
 * @file ReflectionMarkers.h
 * @brief Macros for marking classes, fields, and enums for reflection
 *
 * These macros are used by the reflection parser (reflector.py) to identify
 * types that need reflection metadata generation. The generated code provides
 * ReflectionTraits<T> specializations and static class methods.
 *
 * Usage:
 *   struct Player {
 *       BE_CLASS(Player)
 *
 *       BE_REFLECT_FIELD int32_t health = 100;
 *       BE_REFLECT_FIELD float speed = 5.0f;
 *   };
 *
 * Include generated file in .cpp:
 *   #include <Generated/Player.gen.hpp>
 */

namespace BECore {

    // Forward declaration for friend access
    // Default argument specified here; must not be repeated in TypeTraits.h definition
    template <typename T, typename = void>
    struct ReflectionTraits;

    // Forward declaration for embedded type identification
    struct ClassMeta;

}  // namespace BECore

// =============================================================================
// BE_CLASS - Main reflection macro for classes/structs
// =============================================================================
// Place inside class body. Declares static reflection methods that are
// defined in the generated .gen.hpp file.
//
// Usage:
//   BE_CLASS(ClassName)              - Standard reflection
//   BE_CLASS(ClassName, FACTORY_BASE) - Mark as factory base for enum generation
//
// @param ClassName The name of the containing class (required for static methods)
// @param Options   Optional: FACTORY_BASE to enable factory/enum generation
// =============================================================================

// Helper macros for argument counting and dispatch
#define BE_CLASS_EXPAND(x) x
#define BE_CLASS_GET_MACRO(_1, _2, NAME, ...) NAME
#define BE_CLASS(...) BE_CLASS_EXPAND(BE_CLASS_GET_MACRO(__VA_ARGS__, BE_CLASS_2, BE_CLASS_1)(__VA_ARGS__))

// BE_CLASS(ClassName) - standard reflection only
#define BE_CLASS_1(ClassName)                                                                                                                                                                          \
public:                                                                                                                                                                                                \
    /** @brief Get the type name as string_view */                                                                                                                                                     \
    static constexpr eastl::string_view GetStaticTypeName();                                                                                                                                           \
    /** @brief Get the number of reflected fields */                                                                                                                                                   \
    static constexpr size_t GetStaticFieldCount();                                                                                                                                                     \
    /** @brief Iterate over all reflected fields (mutable) */                                                                                                                                          \
    template <typename Func>                                                                                                                                                                           \
    static constexpr void ForEachFieldStatic(ClassName& obj, Func&& func);                                                                                                                             \
    /** @brief Iterate over all reflected fields (const) */                                                                                                                                            \
    template <typename Func>                                                                                                                                                                           \
    static constexpr void ForEachFieldStatic(const ClassName& obj, Func&& func);                                                                                                                       \
    /** @brief Get static ClassMeta for this type (defined in generated code) */                                                                                                                       \
    static constexpr const ::BECore::ClassMeta& GetStaticTypeMeta();                                                                                                                                   \
    /** @brief Get runtime ClassMeta for this instance */                                                                                                                                              \
    constexpr const ::BECore::ClassMeta& GetTypeMeta() const { return *_typeMeta; }                                                                                                                    \
    /** @brief Check if this instance is of type T */                                                                                                                                                  \
    template <typename T>                                                                                                                                                                              \
    constexpr bool Is() const;                                                                                                                                                                         \
    /** @brief Cast to type T, returns nullptr if type mismatch */                                                                                                                                     \
    template <typename T>                                                                                                                                                                              \
    constexpr T* Cast();                                                                                                                                                                               \
    /** @brief Cast to type T (const), returns nullptr if type mismatch */                                                                                                                             \
    template <typename T>                                                                                                                                                                              \
    constexpr const T* Cast() const;                                                                                                                                                                   \
                                                                                                                                                                                                       \
private:                                                                                                                                                                                               \
    template <typename, typename>                                                                                                                                                                      \
    friend struct ::BECore::ReflectionTraits;                                                                                                                                                          \
    /** @brief Pointer to type metadata (points to static meta of actual derived type) */                                                                                                              \
    const ::BECore::ClassMeta* _typeMeta = &ClassName::GetStaticTypeMeta();                                                                                                                            \
                                                                                                                                                                                                       \
public:

// BE_CLASS(ClassName, FACTORY_BASE) - reflection + factory base marker
// The FACTORY_BASE parameter is parsed by reflector.py to generate:
//   - CORE_ENUM({ClassName}Type, ...) with all derived classes
//   - {ClassName}Factory class with Create() method
#define BE_CLASS_2(ClassName, Options)                                                                                                                                                                 \
public:                                                                                                                                                                                                \
    /** @brief Get the type name as string_view */                                                                                                                                                     \
    static constexpr eastl::string_view GetStaticTypeName();                                                                                                                                           \
    /** @brief Get the number of reflected fields */                                                                                                                                                   \
    static constexpr size_t GetStaticFieldCount();                                                                                                                                                     \
    /** @brief Iterate over all reflected fields (mutable) */                                                                                                                                          \
    template <typename Func>                                                                                                                                                                           \
    static constexpr void ForEachFieldStatic(ClassName& obj, Func&& func);                                                                                                                             \
    /** @brief Iterate over all reflected fields (const) */                                                                                                                                            \
    template <typename Func>                                                                                                                                                                           \
    static constexpr void ForEachFieldStatic(const ClassName& obj, Func&& func);                                                                                                                       \
    /** @brief Get static ClassMeta for this type (defined in generated code) */                                                                                                                       \
    static constexpr const ::BECore::ClassMeta& GetStaticTypeMeta();                                                                                                                                   \
    /** @brief Get runtime ClassMeta for this instance */                                                                                                                                              \
    constexpr const ::BECore::ClassMeta& GetTypeMeta() const { return *_typeMeta; }                                                                                                                    \
    /** @brief Check if this instance is of type T */                                                                                                                                                  \
    template <typename T>                                                                                                                                                                              \
    constexpr bool Is() const;                                                                                                                                                                         \
    /** @brief Cast to type T, returns nullptr if type mismatch */                                                                                                                                     \
    template <typename T>                                                                                                                                                                              \
    constexpr T* Cast();                                                                                                                                                                               \
    /** @brief Cast to type T (const), returns nullptr if type mismatch */                                                                                                                             \
    template <typename T>                                                                                                                                                                              \
    constexpr const T* Cast() const;                                                                                                                                                                   \
                                                                                                                                                                                                       \
private:                                                                                                                                                                                               \
    template <typename, typename>                                                                                                                                                                      \
    friend struct ::BECore::ReflectionTraits;                                                                                                                                                          \
    /** @brief Pointer to type metadata (points to static meta of actual derived type) */                                                                                                              \
    const ::BECore::ClassMeta* _typeMeta = &ClassName::GetStaticTypeMeta();                                                                                                                            \
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
 * @brief Mark an enum for reflection
 *
 * The reflection parser will generate EnumTokenizerPtr compatible with
 * EnumUtils<T>, providing ToString/FromString functionality.
 */
#define BE_REFLECT_ENUM [[clang::annotate("reflect_enum")]]

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
#define BE_REFLECT_ENUM  /* BE_REFLECT_ENUM */
#define BE_FUNCTION      /* BE_FUNCTION */
#endif

