---
name: cpp-senior-programmer
description: Provides expert-level C++23 guidance for BagiEngine game engine development. Covers code quality, performance optimization, modern C++ idioms, and architecture. Use when writing C++ code, reviewing code, optimizing performance, designing APIs, or asking about C++ best practices.
---

# C++ Senior Programmer

Expert guidance for modern C++23 game engine development with emphasis on performance, clean code, and maintainability.

## Project-Specific Requirements (MUST FOLLOW)

| Requirement | Details |
|-------------|---------|
| **Smart Pointers** | Use `IntrusivePtr<T>` with `BECore::New<T>()`. NEVER use `std::unique_ptr` or `std::shared_ptr` |
| **Containers** | Use EASTL (`eastl::vector`, `eastl::hash_map`, `eastl::string`) instead of `std::` containers |
| **Strings** | Use `PoolString` for frequently used strings, `eastl::string` otherwise |
| **Casts** | C-style casts are **FORBIDDEN**. Use `static_cast`, `reinterpret_cast`, `std::bit_cast`. `dynamic_cast` only as last resort |
| **Type Aliases** | Always `using` aliases, NEVER `typedef` |
| **Assertions** | Always `BE_ASSERT()`, NEVER `assert()` |
| **Static Polymorphism** | Use **deducing this (C++23)** instead of CRTP |
| **Object Creation** | Use `BECore::New<T>()` instead of `new`. Use `BECore::NewAtomic<T>()` for thread-safe ref counting |
| **Exceptions** | **FORBIDDEN** - use `std::expected`, error codes, or `BE_ASSERT` |

## Core Principles

### 1. Modern C++23 First

Prefer modern constructs: `IntrusivePtr<T>`, EASTL containers, `PoolString`, deducing this over CRTP, `constexpr`/`consteval`, concepts, `std::expected` for errors.

### 2. Zero-Cost Abstractions

Use deducing this for static polymorphism, concepts for constraints, `constexpr` for compile-time computation. See [patterns.md](patterns.md) for examples.

### 3. Performance by Default

- **Data-oriented design**: Prefer SoA over AoS for cache efficiency
- **Avoid virtual dispatch** in hot paths (use deducing this, `eastl::variant`)
- **Pre-allocate containers**: `reserve()` before loops
- **Move semantics**: Pass by value and move, or use `&&` explicitly
- **Avoid heap allocations**: Use stack, arenas, or pool allocators

## Memory Management

**Ownership hierarchy**:
1. **Automatic storage** (stack) - default choice
2. **`IntrusivePtr<T>`** - heap-allocated objects with ref counting
3. **Raw pointers** - non-owning observers only

```cpp
// Correct usage
class MyObject : public RefCounted { /* ... */ };

IntrusivePtr<MyObject> obj = BECore::New<MyObject>();  // Single-threaded
IntrusivePtr<MyObject> atomicObj = BECore::NewAtomic<MyObject>();  // Thread-safe

eastl::vector<IntrusivePtr<Entity>> entities;
eastl::hash_map<PoolString, IntrusivePtr<Resource>> resources;
```

**NEVER use**: `std::unique_ptr`, `std::shared_ptr`, naked `new`/`delete`, `std::` containers.

## Error Handling

BagiEngine is built **without exceptions** (no-exceptions policy). Use:

1. **Compile-time errors**: `static_assert`, concepts, SFINAE
2. **Expected failures**: `std::expected<T, E>` (C++23) — I/O, loading, TaskSystem results
3. **Programming errors**: `BE_ASSERT()` (NEVER use `assert()`)
4. **Critical failures**: `FATALERROR()` — unrecoverable errors

**Exceptions are FORBIDDEN** — use `std::expected`, error codes, or `BE_ASSERT` instead.

```cpp
auto LoadTexture(std::string_view path) -> std::expected<Texture, LoadError> {
    if (!FileExists(path)) 
        return std::unexpected(LoadError::FileNotFound);
    return texture;
}

void ProcessEntity(Entity* entity) {
    BE_ASSERT(entity != nullptr);  // Correct
}
```

## C++23 Features

### Deducing This (Preferred over CRTP)

```cpp
class Widget {
public:
    template<typename Self>
    auto& GetChild(this Self&& self, size_t index) {
        return std::forward<Self>(self)._children[index];
    }
    
    template<typename Self>
    void UpdateRecursive(this Self&& self) {
        self.Update();
        for (auto& child : self._children) {
            child.UpdateRecursive();
        }
    }
private:
    eastl::vector<Widget> _children;
};
```

For more deducing this patterns, see [patterns.md](patterns.md).

### Other Key Features

- **`if consteval`**: Compile-time vs runtime paths
- **Multidimensional subscript**: `auto& operator[](size_t x, size_t y)`
- **`std::unreachable()`**: Mark unreachable code paths
- **`std::expected`**: Error handling without exceptions
- **`std::ranges`**: Works with EASTL containers
- **`std::mdspan`**: Multidimensional array views

## Game Development Patterns

### Entity Component System (ECS)

```cpp
struct Transform { Vec3 position, rotation, scale; };
struct Velocity { Vec3 linear, angular; };

void PhysicsSystem(entt::registry& registry) {
    auto view = registry.view<Transform, Velocity>();
    for (auto [entity, transform, velocity] : view.each()) {
        transform.position += velocity.linear * deltaTime;
    }
}
```

### Resource Management

Use handle-based resources (avoid raw pointers to GPU objects):

```cpp
using TextureHandle = StrongTypedef<uint32_t, struct TextureTag>;

class TextureManager {
    eastl::hash_map<TextureHandle, IntrusivePtr<Texture>> _textures;
public:
    TextureHandle Create(const TextureDesc& desc);
    Texture* Get(TextureHandle handle);  // Returns nullptr if invalid
};
```

### Event-Driven Architecture

Prefer event-driven design for loose coupling between systems. Use events instead of direct function calls for cross-system communication.

**Principles**:
- **Decouple systems**: Systems communicate through events, not direct dependencies
- **One source, many listeners**: One system emits, multiple systems can react
- **Asynchronous processing**: Use `Enqueue()` for deferred event processing
- **Type safety**: Events are strongly typed via `EventBase<T>` CRTP pattern

**Basic Usage**:
```cpp
// Define event
struct PlayerDiedEvent : EventBase<PlayerDiedEvent> {
    EntityId playerId;
    Vec3 position;
};

// Subscribe to event
class GameManager {
public:
    void Initialize() {
        PlayerDiedEvent::Subscribe<&GameManager::OnPlayerDied>(this);
    }
    
private:
    void OnPlayerDied(const PlayerDiedEvent& event) {
        // Handle player death
    }
};

// Emit event (immediate dispatch)
PlayerDiedEvent::Emit({playerId, position});

// Enqueue event (deferred processing)
PlayerDiedEvent::Enqueue({playerId, position});
```

**When to Use Events**:
- Cross-system communication (physics → audio, gameplay → UI)
- Decoupling unrelated systems
- Broadcasting state changes to multiple listeners
- Deferred processing (queue events for later)

**When NOT to Use Events**:
- Direct parent-child relationships (use method calls)
- Performance-critical hot paths (use direct calls)
- Simple data flow within same system

See `src/Modules/Events/` for implementation details.

## Performance Guidelines

### Hot Path Optimization

**Avoid**: Virtual calls, heap allocations, cache misses, branching in tight loops.

**Prefer**: Inline functions/deducing this, contiguous memory (`eastl::vector`), SIMD, branch-free algorithms.

### Memory Access Patterns

**Bad (AoS)**: `eastl::vector<Entity>` where `Entity` has mixed data types.

**Good (SoA)**: Separate vectors for each component type:
```cpp
struct EntityData {
    eastl::vector<Vec3> positions;
    eastl::vector<Quaternion> rotations;
    eastl::vector<Mesh*> meshes;
};
```

### Compile-Time Computation

```cpp
constexpr auto SinTable = []() {
    std::array<float, 360> table{};
    for (int i = 0; i < 360; ++i)
        table[i] = std::sin(i * std::numbers::pi / 180.0f);
    return table;
}();
```

## Code Review Checklist

- [ ] **IntrusivePtr**: Using `IntrusivePtr<T>`, not `std::unique_ptr`/`std::shared_ptr`
- [ ] **EASTL**: Using EASTL containers, not `std::` containers
- [ ] **No C-style casts**: Using `static_cast`, `reinterpret_cast`, `std::bit_cast`
- [ ] **BE_ASSERT**: Using `BE_ASSERT()`, not `assert()`
- [ ] **using aliases**: No `typedef`, only `using`
- [ ] **Deducing this**: Prefer over CRTP for static polymorphism
- [ ] **RAII**: Resources managed by constructors/destructors
- [ ] **Const correctness**: `const` used appropriately
- [ ] **Move semantics**: Unnecessary copies avoided
- [ ] **Error handling**: Failures handled gracefully (no exceptions)
- [ ] **Performance**: No obvious bottlenecks in hot paths

## Anti-Patterns to Avoid

### Forbidden Patterns

- `std::unique_ptr`, `std::shared_ptr` → use `IntrusivePtr<T>`
- `std::vector`, `std::map`, `std::string` → use EASTL equivalents
- C-style casts `(Type)value` → use `static_cast<Type>(value)`
- `typedef` → use `using`
- `assert()` → use `BE_ASSERT()`
- CRTP → use deducing this (C++23)
- Exceptions → use `std::expected` or error codes

### Memory Anti-Patterns

- Raw `new`/`delete` without RAII wrapper
- Circular `IntrusivePtr` references (use weak references)
- Returning references to temporaries
- `std::move` on const objects

### Performance Anti-Patterns

- Virtual calls in tight loops
- String operations in hot paths
- Repeated container reallocations
- Cache-unfriendly access patterns

## Additional Resources

- **Design patterns**: See [patterns.md](patterns.md) for detailed pattern implementations
- **Module-specific rules**: See `.cursor/rules/` directory:
  - Core module: `core-module.mdc`
  - Event system: `events-module.mdc`
  - Widget system: `widgets-module.mdc`
  - SDL integration: `sdl-module.mdc`
