#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ClassMeta.h>

namespace BECore {

    /**
     * @brief Abstract factory base for type hierarchies
     *
     * Generated factories inherit from this and implement:
     * - Create(ClassMeta) - creates instance by type metadata
     * - Enum-based Create(EnumType) as static convenience method
     *
     * @tparam BaseType The base class type (e.g., ILogSink)
     *
     * @example
     * // Generated factory inherits from AbstractFactory
     * class LogSinkFactory : public AbstractFactory<ILogSink> {
     * public:
     *     BasePtr Create(const ClassMeta& meta) const override {
     *         if (meta == GetClassMeta<ConsoleSink>()) return New<ConsoleSink>();
     *         // ...
     *     }
     * };
     *
     * // Usage
     * auto meta = LogSinkFactory::ToMeta(LogSinkType::Console);
     * auto sink = LogSinkFactory::GetInstance().Create(meta);
     */
    template <typename BaseType>
    class AbstractFactory {
    public:
        using BasePtr = GetIntrusivePtrType<BaseType>::type;

        virtual ~AbstractFactory() = default;

        /**
         * @brief Create instance by ClassMeta
         *
         * @param meta Type metadata identifying the concrete class
         * @return IntrusivePtr to created instance, or empty if unknown type
         */
        virtual BasePtr Create(const ClassMeta& meta) const = 0;
    };

}  // namespace BECore
