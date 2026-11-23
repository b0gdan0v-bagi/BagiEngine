# RefCounted

Intrusive reference counting: the counters (`RefCounted.h`), `IntrusivePtr<T>` (`IntrusivePtr.h`) and `New<T>` (`New.h`).

- **One pointer type, the counter decides the thread rule.** `IntrusivePtr<T>` serves both; whether a count is atomic depends on the base of `T`: `RefCountedAtomic` (the last holder may be any thread; resources, connections) or `RefCountedNonAtomic` (one thread). `RefCounted` is the non-atomic one. Do not share a non-atomic object across threads.
- **`New<T>(args...)` is the one way to make an object.** It returns the first owner. `IntrusivePtr<T>(rawPointer)` takes a first count on a pointer that has none yet, for a place that cannot call `New`.
- **The count starts at 0.** A fresh object has no owner: an `IntrusivePtr(this)` made and dropped inside a constructor, or before the first owner exists, frees the object under its own constructor.
- **A conversion to a base is implicit; a downcast does not compile.** Use `StaticPointerCast` or `DynamicPointerCast` (the rvalue form takes the source's count, the lvalue form copies it).
- **`Detach()` and the adopt constructor** (`IntrusivePtr(ptr, kAdoptRef)`) are a pair for handing a count through a `void*` or a C callback; nothing else makes an adopting pointer.
- **A destructor may use its holder:** every release moves the pointer out first and drops the count afterwards, so a destructor that reads the pointer that held it sees null. Assigning one pointer from another leaves the new value in the holder while the old object dies.
- `IntrusivePtrAtomic<T>` and `IntrusivePtrNonAtomic<T>` are deprecated aliases of `IntrusivePtr<T>`, kept for one merge for games; `MakeIntrusive(args...)` is the deprecated forwarder of `New<T>`. The meta generator still finds a factory base under the old names.
- Reflected factory fields are `Vector<IntrusivePtr<Base>>` (`.claude/rules/reflection-module.md`, "Factory Generation (FACTORY_BASE)").
