#pragma once

#include <BECore/Reflection/TypeTraits.h>

namespace BECore {
    class IDeserializer;
}

namespace BECore::Tests {

    /// Factory base of the FactoryInitialize tests. Like IWidget it declares the hook (the loader looks at the base type)
    class InitProbe : public RefCounted {
        BE_CLASS(InitProbe, FACTORY_BASE, ELEMENT = probe)
    public:
        InitProbe() = default;
        ~InitProbe() override = default;

        virtual bool Initialize(IDeserializer& /*deserializer*/) {
            return true;
        }
        virtual int32_t GetExtra() const = 0;
    };

    /// Reads one more attribute of its own element in Initialize(IDeserializer&) and accepts
    class InitProbeReads : public InitProbe {
        BE_CLASS(InitProbeReads)
    public:
        InitProbeReads() = default;
        ~InitProbeReads() override = default;

        bool Initialize(IDeserializer& deserializer) override;
        int32_t GetExtra() const override {
            return _extra;
        }

        BE_REFLECT_FIELD int32_t id = 0;

    private:
        int32_t _extra = -1;
    };

    /// Initialize(IDeserializer&) returns false
    class InitProbeFails : public InitProbe {
        BE_CLASS(InitProbeFails)
    public:
        InitProbeFails() = default;
        ~InitProbeFails() override = default;

        bool Initialize(IDeserializer& deserializer) override;
        int32_t GetExtra() const override {
            return 0;
        }

        BE_REFLECT_FIELD int32_t id = 0;

        static inline int32_t initializeCalls = 0;
    };

    /// Keeps the base's Initialize (returns true)
    class InitProbePlain : public InitProbe {
        BE_CLASS(InitProbePlain)
    public:
        InitProbePlain() = default;
        ~InitProbePlain() override = default;

        int32_t GetExtra() const override {
            return 0;
        }

        BE_REFLECT_FIELD int32_t id = 0;
    };

    /// A factory base without any Initialize(IDeserializer&): its elements load as before the hook existed
    class NoHookProbe : public RefCounted {
        BE_CLASS(NoHookProbe, FACTORY_BASE, ELEMENT = nohook)
    public:
        NoHookProbe() = default;
        ~NoHookProbe() override = default;

        virtual int32_t GetValue() const = 0;
    };

    class NoHookProbeImpl : public NoHookProbe {
        BE_CLASS(NoHookProbeImpl)
    public:
        NoHookProbeImpl() = default;
        ~NoHookProbeImpl() override = default;

        int32_t GetValue() const override {
            return value;
        }

        BE_REFLECT_FIELD int32_t value = 0;
    };

    /// A holder with a factory vector, the shape MakeFromConfig fills
    struct InitProbeList : RefCounted {
        BE_CLASS(InitProbeList)

        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<InitProbe>> probes;
    };

    struct NoHookProbeList : RefCounted {
        BE_CLASS(NoHookProbeList)

        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<NoHookProbe>> items;
    };

}  // namespace BECore::Tests
