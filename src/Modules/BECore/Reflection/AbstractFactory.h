#pragma once

#include <BECore/Reflection/ClassMeta.h>

namespace BECore {

    /**
     * @brief Runtime self-registration factory for type hierarchies.
     *
     * A per-base-type Meyers singleton that derived classes register themselves into
     * from their own .cpp via code generated in the reflection .gen.hpp file.
     * Callers create instances by ClassMeta, eastl::string_view, or PoolString
     * without including any derived-class headers.
     *
     * Registration happens via static initializers:
     * @code
     * // ConsoleSink.gen.hpp (included only from ConsoleSink.cpp)
     * namespace {
     * const bool _reg = [] {
     *     AbstractFactory<ILogSink>::GetInstance().Register(
     *         ConsoleSink::GetStaticTypeMeta(), [] { return New<ConsoleSink>(); });
     *     return true;
     * }();
     * }
     * @endcode
     *
     * @tparam BaseType  The abstract base class (e.g. ILogSink, IWidget).
     */
    template <typename BaseType>
    class AbstractFactory {
    public:
        using BasePtr = typename GetIntrusivePtrType<BaseType>::type;
        using CreatorFunc = BasePtr (*)();

        static AbstractFactory& GetInstance() {
            static AbstractFactory instance;
            return instance;
        }

        /**
         * @brief Register a derived type with its creator function.
         *
         * Called from static initializers in generated .gen.hpp files.
         * Duplicate registrations overwrite silently (last-write-wins).
         *
         * @param meta    ClassMeta of the concrete derived type.
         * @param creator Zero-argument factory function returning BasePtr.
         */
        void Register(const ClassMeta& meta, CreatorFunc creator) {
            _creators[meta.typeHash] = creator;
            _metas.push_back(meta);
        }

        /**
         * @brief Create an instance identified by ClassMeta.
         * @return New instance, or empty pointer if type is not registered.
         */
        BasePtr Create(const ClassMeta& meta) const {
            return _findAndCreate(meta.typeHash);
        }

        /**
         * @brief Create an instance identified by class name string.
         *
         * Computes FNV-1a hash of @p name at call-time (same algorithm as
         * ClassMeta::typeHash) and performs a single hash_map lookup.
         * No heap allocation.
         *
         * @return New instance, or empty pointer if type is not registered.
         */
        BasePtr Create(eastl::string_view name) const {
            return _findAndCreate(String::GetHash(name));
        }

        /**
         * @brief Create an instance identified by an interned PoolString.
         *
         * Uses the hash already stored inside the PoolString entry — zero
         * computation, single hash_map lookup, no heap allocation.
         *
         * @return New instance, or empty pointer if type is not registered.
         */
        BasePtr Create(const PoolString& name) const {
            return _findAndCreate(name.HashValue());
        }

        /**
         * @brief Return all registered ClassMeta entries.
         *
         * Useful for iterating over all registered types, e.g. to
         * instantiate all resource loaders at startup.
         */
        [[nodiscard]] const eastl::vector<ClassMeta>& GetRegisteredTypes() const {
            return _metas;
        }

    private:
        static constexpr eastl::string_view _resolveDebugName() {
            if constexpr (HasReflection<BaseType>) {
                return ReflectionTraits<BaseType>::name;
            } else {
                return {};
            }
        }

        AbstractFactory() = default;

        BasePtr _findAndCreate(uint64_t hash) const {
            auto it = _creators.find(hash);
            if (it != _creators.end()) return it->second();
            return {};
        }

        eastl::string_view _debugName = _resolveDebugName();
        eastl::hash_map<uint64_t, CreatorFunc> _creators;
        eastl::vector<ClassMeta> _metas;
    };

}  // namespace BECore
