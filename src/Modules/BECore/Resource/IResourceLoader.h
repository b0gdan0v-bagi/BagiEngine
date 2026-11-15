#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/IResource.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/Task.h>

namespace BECore {

    /**
     * @brief Loads one kind of file into an IResource; ResourceManager picks it by the file name's suffix.
     *
     * XmlResourceLoader is the reference implementation.
     */
    class IResourceLoader : public RefCounted {
        BE_CLASS(IResourceLoader, FACTORY_BASE)
    public:
        virtual ~IResourceLoader() = default;

        /** @param extension A suffix of the file name with its dot, e.g. ".xml" or ".ui.xml". */
        virtual bool CanLoad(StringView extension) const = 0;

        /**
         * @brief Must not block the calling thread.
         * @return The loaded resource, or nullptr on failure.
         */
        virtual Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) = 0;

        /** @return The loaded resource, or nullptr on failure. */
        virtual IntrusivePtr<IResource> LoadSync(PoolString path) = 0;
    };

}  // namespace BECore
