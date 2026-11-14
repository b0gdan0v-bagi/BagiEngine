#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /** @brief Where a resource is in its loading lifecycle. */
    CORE_ENUM(ResourceState, uint8_t,
              Unloaded,  ///< Resource not yet loaded
              Loading,   ///< Resource is currently loading
              Loaded,    ///< Resource successfully loaded and ready to use
              Failed     ///< Resource loading failed
    )

    /**
     * @brief Base of every resource ResourceManager caches (held by IntrusivePtr).
     *
     * A resource is immutable after loading, so concurrent reads are safe.
     */
    class IResource : public RefCounted {
        BE_CLASS(IResource, FACTORY_BASE)
    public:
        virtual ~IResource() = default;

        virtual ResourceState GetState() const = 0;

        virtual PoolString GetPath() const = 0;

        /** @brief Bytes owned by the resource, for cache statistics; include all owned data. */
        virtual uint64_t GetMemoryUsage() const = 0;

        virtual PoolString GetTypeName() const = 0;
    };

}  // namespace BECore
