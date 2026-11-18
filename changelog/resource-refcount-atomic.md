# Resources: IResource has a non-atomic reference count but is shared across threads; async loads keep a StringView

- Version: 0.4.0
- Entry: resource-refcount-atomic
- Date: 2026-11-18
- Bump: minor

## Changed
- `IResource` derives from `RefCountedAtomic`, so loader threads and the main thread can share a resource safely. `New<T>` of a resource gives an `IntrusivePtrAtomic<T>`, which converts to the `IntrusivePtr<T>` that holders keep.
- `ResourceManager::LoadAsync`, `LoadSerializedAsync` and `LoadSerializedPartAsync` copy the path before they return, so a temporary string is safe as the argument.
- `ResourceHandle::operator->` no longer reads the clock on every access.

## Breaking
- `ResourceHandle::GetLastAccessTime` is removed. Adapt: a game that needs the time of the last use keeps it next to the handle.
- `SerializedResource<T>` and its header `BECore/Resource/SerializedResource.h` are removed. Adapt: load the object with `ResourceManager::LoadSerialized<T>` and keep the value it returns.
