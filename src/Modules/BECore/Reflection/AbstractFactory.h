#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief Runtime self-registration factory for type hierarchies.
     *
     * A per-base-type Meyers singleton that derived classes register into from their own .cpp (code in the
     * generated .gen.hpp). Callers create instances by ClassMeta, StringView or PoolString without including
     * any derived-class header.
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
         * Called from static initializers in generated .gen.hpp files. A second registration of the same type
         * replaces the creator silently (last write wins), but its meta is listed again by GetRegisteredTypes().
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
            return FindAndCreate(meta.typeHash);
        }

        /**
         * @brief Create an instance identified by class name.
         *
         * Hashes @p name at call time with the algorithm of ClassMeta::typeHash; no heap allocation.
         *
         * @return New instance, or empty pointer if type is not registered.
         */
        BasePtr Create(StringView name) const {
            return FindAndCreate(StringUtils::GetHash(name));
        }

        /**
         * @brief Create an instance identified by an interned PoolString.
         *
         * Uses the hash stored in the PoolString entry; no heap allocation.
         *
         * @return New instance, or empty pointer if type is not registered.
         */
        BasePtr Create(const PoolString& name) const {
            return FindAndCreate(name.HashValue());
        }

        /**
         * @brief The ClassMeta registered under class name @p name, or null when no such type is registered.
         *
         * Lets a loader read a type's purpose before it creates an instance.
         */
        [[nodiscard]] const ClassMeta* FindMeta(StringView name) const {
            const uint64_t hash = StringUtils::GetHash(name);
            for (const ClassMeta& meta : _metas) {
                if (meta.typeHash == hash) {
                    return &meta;
                }
            }
            return nullptr;
        }

        /**
         * @brief All registered ClassMeta entries (e.g. to instantiate every resource loader at startup).
         */
        [[nodiscard]] const Vector<ClassMeta>& GetRegisteredTypes() const {
            return _metas;
        }

    private:
        static constexpr StringView ResolveDebugName() {
            if constexpr (HasReflection<BaseType>) {
                return ReflectionTraits<BaseType>::name;
            } else {
                return {};
            }
        }

        AbstractFactory() = default;

        BasePtr FindAndCreate(uint64_t hash) const {
            auto it = _creators.find(hash);
            if (it != _creators.end())
                return it->second();
            return {};
        }

        StringView _debugName = ResolveDebugName();
        HashMap<uint64_t, CreatorFunc> _creators;
        Vector<ClassMeta> _metas;
    };

}  // namespace BECore
