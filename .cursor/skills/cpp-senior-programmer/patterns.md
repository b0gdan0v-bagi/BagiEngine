# C++ Design Patterns Reference

Extended patterns and idioms for BagiEngine game engine development.

**Project Requirements**: Use `IntrusivePtr<T>` with `BECore::New<T>()` (or `BECore::NewAtomic<T>()` for thread-safe), EASTL containers, `BE_ASSERT()`, deducing this over CRTP. Exceptions are **FORBIDDEN**. `dynamic_cast` only as last resort.

## Patterns Already in Codebase

These patterns are implemented in the project - see source code for details:

- **Singleton**: `src/Modules/BECore/Utils/Singleton.h` - Use `Singleton<T>` or `SingletonAtomic<T>`
- **RefCounted + IntrusivePtr**: `src/Modules/BECore/RefCounted/` - Use `RefCounted`/`RefCountedAtomic` with `IntrusivePtr<T>`
- **PassKey**: `src/Modules/BECore/Utils/PassKey.h` - Restrict method access to specific classes
- **Event System**: `src/Modules/Events/` - Use `EventBase<T>` with `Subscribe()`, `Emit()`, `Enqueue()`

## Creational Patterns

### Factory with Type Registration

```cpp
template<typename Base, typename... Args>
class Factory {
    using Creator = eastl::function<IntrusivePtr<Base>(Args...)>;
    eastl::hash_map<PoolString, Creator> _creators;
    
public:
    template<typename Derived>
    void Register(std::string_view name) {
        _creators[PoolString(name)] = [](Args... args) {
            return IntrusivePtr<Derived>(BECore::New<Derived>(std::forward<Args>(args)...));
        };
    }
    
    IntrusivePtr<Base> Create(std::string_view name, Args... args) {
        auto it = _creators.find(PoolString(name));
        return it != _creators.end() ? it->second(std::forward<Args>(args)...) : nullptr;
    }
};
```

### Object Pool

```cpp
template<typename T, size_t PoolSize = 1024>
class ObjectPool {
    std::array<std::aligned_storage_t<sizeof(T), alignof(T)>, PoolSize> _storage;
    eastl::vector<T*> _available;
    
public:
    ObjectPool() {
        _available.reserve(PoolSize);
        for (auto& slot : _storage) {
            _available.push_back(reinterpret_cast<T*>(&slot));
        }
    }
    
    template<typename... Args>
    T* Acquire(Args&&... args) {
        if (_available.empty()) return nullptr;
        T* obj = _available.back();
        _available.pop_back();
        return new (obj) T(std::forward<Args>(args)...);
    }
    
    void Release(T* obj) {
        BE_ASSERT(obj != nullptr);
        obj->~T();
        _available.push_back(obj);
    }
};
```

## Structural Patterns

### Deducing This (C++23) - Replaces CRTP

```cpp
// Static polymorphism with deducing this - zero runtime overhead
class Cloneable {
public:
    template<typename Self>
    auto Clone(this Self const& self) {
        return IntrusivePtr<std::remove_cvref_t<Self>>(
            BECore::New<std::remove_cvref_t<Self>>(self)
        );
    }
};

// Fluent interface pattern
class Builder {
public:
    template<typename Self>
    auto&& SetName(this Self&& self, PoolString name) {
        self._name = eastl::move(name);
        return std::forward<Self>(self);
    }
    
    template<typename Self>
    auto&& SetSize(this Self&& self, int width, int height) {
        self._width = width;
        self._height = height;
        return std::forward<Self>(self);
    }
private:
    PoolString _name;
    int _width = 0, _height = 0;
};
```

### Type Erasure

```cpp
// Runtime polymorphism without inheritance
class AnyDrawable {
    struct Concept {
        virtual ~Concept() = default;
        virtual void Draw() const = 0;
        virtual IntrusivePtr<Concept> Clone() const = 0;
    };
    
    template<typename T>
    struct Model : Concept, RefCounted {
        T _data;
        Model(T data) : _data(eastl::move(data)) {}
        void Draw() const override { _data.Draw(); }
        IntrusivePtr<Concept> Clone() const override {
            return IntrusivePtr<Model>(BECore::New<Model>(*this));
        }
    };
    
    IntrusivePtr<Concept> _impl;
    
public:
    template<typename T>
    AnyDrawable(T x) : _impl(BECore::New<Model<T>>(eastl::move(x))) {}
    
    void Draw() const { _impl->Draw(); }
};
```

## Behavioral Patterns

### Command with Undo

```cpp
class Command : public RefCounted {
public:
    virtual ~Command() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
};

class CommandHistory {
    eastl::vector<IntrusivePtr<Command>> _history;
    size_t _current = 0;
    
public:
    void Execute(IntrusivePtr<Command> cmd) {
        BE_ASSERT(cmd != nullptr);
        _history.resize(_current);
        cmd->Execute();
        _history.push_back(eastl::move(cmd));
        ++_current;
    }
    
    void Undo() {
        if (_current > 0) _history[--_current]->Undo();
    }
    
    void Redo() {
        if (_current < _history.size()) _history[_current++]->Execute();
    }
};
```

### Observer with IntrusivePtr

```cpp
template<typename... Args>
class Signal {
    using Slot = eastl::function<void(Args...)>;
    
    struct Observer : RefCounted {
        Slot callback;
        Observer(Slot cb) : callback(eastl::move(cb)) {}
    };
    
    eastl::vector<IntrusivePtr<Observer>> _observers;
    
public:
    using Handle = IntrusivePtr<Observer>;
    
    Handle Connect(Slot callback) {
        auto observer = IntrusivePtr<Observer>(BECore::New<Observer>(eastl::move(callback)));
        _observers.push_back(observer);
        return observer;
    }
    
    void Disconnect(const Handle& handle) {
        eastl::erase(_observers, handle);
    }
    
    void Emit(Args... args) {
        for (auto& observer : _observers) {
            observer->callback(std::forward<Args>(args)...);
        }
    }
};
```

### State Machine

```cpp
template<typename StateEnum>
class StateMachine {
    StateEnum _current;
    eastl::hash_map<StateEnum, eastl::function<void()>> _onEnter;
    eastl::hash_map<StateEnum, eastl::function<void()>> _onExit;
    eastl::hash_map<StateEnum, eastl::function<void(float)>> _onUpdate;
    
public:
    void SetState(StateEnum newState) {
        if (auto it = _onExit.find(_current); it != _onExit.end())
            it->second();
        _current = newState;
        if (auto it = _onEnter.find(_current); it != _onEnter.end())
            it->second();
    }
    
    void Update(float dt) {
        if (auto it = _onUpdate.find(_current); it != _onUpdate.end())
            it->second(dt);
    }
    
    void OnEnter(StateEnum state, eastl::function<void()> fn) { 
        _onEnter[state] = eastl::move(fn); 
    }
    void OnExit(StateEnum state, eastl::function<void()> fn) { 
        _onExit[state] = eastl::move(fn); 
    }
    void OnUpdate(StateEnum state, eastl::function<void(float)> fn) { 
        _onUpdate[state] = eastl::move(fn); 
    }
};
```

## Memory Patterns

### Arena Allocator

```cpp
class Arena {
    eastl::vector<std::byte> _buffer;
    size_t _offset = 0;
    
public:
    explicit Arena(size_t size) : _buffer(size) {}
    
    template<typename T, typename... Args>
    T* Alloc(Args&&... args) {
        size_t aligned = (_offset + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned + sizeof(T) > _buffer.size()) return nullptr;
        T* ptr = new (&_buffer[aligned]) T(std::forward<Args>(args)...);
        _offset = aligned + sizeof(T);
        return ptr;
    }
    
    void Reset() { _offset = 0; }  // Mass deallocation
};
```

## Concurrency Patterns

### Double-Buffering

```cpp
template<typename T>
class DoubleBuffer {
    std::array<T, 2> _buffers;
    std::atomic<int> _readIndex{0};
    
public:
    T& GetWriteBuffer() { return _buffers[1 - _readIndex.load()]; }
    const T& GetReadBuffer() const { return _buffers[_readIndex.load()]; }
    
    void Swap() {
        _readIndex.store(1 - _readIndex.load());
    }
};
```

### Lock-Free Ring Buffer

```cpp
template<typename T, size_t Size>
class SPSCQueue {  // Single Producer Single Consumer
    std::array<T, Size> _buffer;
    std::atomic<size_t> _head{0};
    std::atomic<size_t> _tail{0};
    
public:
    bool Push(const T& item) {
        size_t head = _head.load(std::memory_order_relaxed);
        size_t next = (head + 1) % Size;
        if (next == _tail.load(std::memory_order_acquire)) return false;
        _buffer[head] = item;
        _head.store(next, std::memory_order_release);
        return true;
    }
    
    bool Pop(T& item) {
        size_t tail = _tail.load(std::memory_order_relaxed);
        if (tail == _head.load(std::memory_order_acquire)) return false;
        item = _buffer[tail];
        _tail.store((tail + 1) % Size, std::memory_order_release);
        return true;
    }
};
```

## Deducing This Patterns (C++23)

### Recursive Visitor

```cpp
class TreeNode : public RefCounted {
    eastl::vector<IntrusivePtr<TreeNode>> _children;
    PoolString _name;
    
public:
    template<typename Self, typename Visitor>
    void Visit(this Self&& self, Visitor&& visitor) {
        visitor(std::forward<Self>(self));
        for (auto& child : self._children) {
            child->Visit(std::forward<Visitor>(visitor));
        }
    }
    
    template<typename Self, typename Predicate>
    auto* Find(this Self&& self, Predicate&& pred) 
        -> std::remove_reference_t<Self>* 
    {
        if (pred(self)) return &self;
        for (auto& child : self._children) {
            if (auto* found = child->Find(std::forward<Predicate>(pred))) {
                return found;
            }
        }
        return nullptr;
    }
};
```
