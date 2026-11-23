# IntrusivePtr and RefCounted: fix conversions, merge the atomic and non-atomic pointers, add tests

- Version: 0.11.0
- Entry: intrusive-ptr-cleanup
- Date: 2026-11-23
- Bump: minor

## Added
- `IntrusivePtr<T>` has `StaticPointerCast` / `DynamicPointerCast`, a hash, `swap`, `Detach()` (give up ownership without a release) and an adopt constructor; `RefCounted` asserts its count in debug builds.

## Changed
- `IntrusivePtrAtomic<T>` and `IntrusivePtrNonAtomic<T>` are deprecated aliases of the one `IntrusivePtr<T>` (the counter is the class's: `RefCounted` or `RefCountedAtomic`), and `MakeIntrusive(args...)` is a deprecated forwarder of `New<T>(args...)`; both stay for one merge. Adapt: spell `IntrusivePtr<T>` and `New<T>`.

## Breaking
- `BECore/RefCounted/Delete.h` is gone, and `RefCounted::ReleaseRef()` returns `void` (it returned whether the object was deleted). Adapt: drop the include; code that used the result of `ReleaseRef` holds an `IntrusivePtr` instead.
- Conversions between `IntrusivePtr` types compile only from a derived to a base type (implicitly) or through `StaticPointerCast` / `DynamicPointerCast`. Adapt: replace a conversion the compiler now refuses with the matching cast.
- The meta generator now sees a factory base in a reflected field of type `IntrusivePtr<Base>` too (before only under the two long names), so such a field is read and written through the factory. Adapt: a field that must stay a plain pointer field gets `BE_FIELD_ATTRS(no_factory)`; re-run `cmake --preset` to regenerate the reflection code.
