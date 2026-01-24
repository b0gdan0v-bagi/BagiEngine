#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Reflection/ReflectionMarkers.h>

namespace BECore {

    /**
     * @brief Resource loading state
     * 
     * Tracks the current state of a resource through its loading lifecycle.
     */
    CORE_ENUM(ResourceState, uint8_t, 
        Unloaded,   ///< Resource not yet loaded
        Loading,    ///< Resource is currently loading
        Loaded,     ///< Resource successfully loaded and ready to use
        Failed      ///< Resource loading failed
    )

    /**
     * @brief Base interface for all managed resources
     * 
     * All resource types inherit from IResource and are managed
     * by ResourceManager with reference counting via IntrusivePtr.
     * 
     * Resources are immutable after loading, making them thread-safe
     * for concurrent read access.
     * 
     * @example
     * class MyResource : public IResource {
     *     BE_CLASS(MyResource)
     * public:
     *     ResourceState GetState() const override { return _state; }
     *     PoolString GetPath() const override { return _path; }
     *     uint64_t GetMemoryUsage() const override { return sizeof(*this); }
     *     PoolString GetTypeName() const override { return "MyResource"; }
     * private:
     *     PoolString _path;
     *     ResourceState _state = ResourceState::Unloaded;
     * };
     */
    class IResource : public RefCounted {
        BE_CLASS(IResource, FACTORY_BASE)
    public:
        virtual ~IResource() = default;
        
        /**
         * @brief Get current loading state
         * @return ResourceState enum value
         */
        virtual ResourceState GetState() const = 0;
        
        /**
         * @brief Get virtual path to this resource
         * @return PoolString with the resource path
         */
        virtual PoolString GetPath() const = 0;
        
        /**
         * @brief Get estimated memory usage in bytes
         * 
         * Used for cache statistics and memory tracking.
         * Should include the size of all owned data.
         * 
         * @return Memory usage of this resource
         */
        virtual uint64_t GetMemoryUsage() const = 0;
        
        /**
         * @brief Get human-readable type name
         * @return PoolString with type name (e.g., "XmlResource")
         */
        virtual PoolString GetTypeName() const = 0;
    };

}  // namespace BECore
