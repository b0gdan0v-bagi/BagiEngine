#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Logger/Logger.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Resource/AssetParts.h>
#include <BECore/Resource/IResource.h>
#include <BECore/Resource/IResourceLoader.h>
#include <BECore/Resource/ResourceCache.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Resource/SerializedResource.h>
#include <BECore/Resource/XmlResource.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/Task.h>
#include <concepts>

namespace BECore {

    /**
     * @brief Loads resources through the registered loaders and caches them by path.
     *
     * Reached through CoreManager::GetResourceManager(). Loaders are registered at startup, before any load.
     */
    class ResourceManager {
    public:
        ResourceManager() = default;
        ~ResourceManager() = default;

        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;
        ResourceManager(ResourceManager&&) = delete;
        ResourceManager& operator=(ResourceManager&&) = delete;

        /** @brief Creates and registers every loader in AbstractFactory<IResourceLoader>; call once at startup. */
        void Initialize();

        /**
         * @brief Returns the cached resource or loads it through the loader of the path.
         *
         * A cached resource of another type than T is loaded again and replaces the cached one.
         * The handle is empty when no loader accepts the path, the load fails or the result is not a T.
         */
        template <typename T>
        requires std::derived_from<T, IResource> Task<ResourceHandle<T>> LoadAsync(StringView path);

        /** @brief The blocking form of LoadAsync, with the same cache and failure rules. */
        template <typename T>
        requires std::derived_from<T, IResource> ResourceHandle<T> Load(StringView path);

        /**
         * @brief Deserializes T from a file opened with OpenDeserializer() (the extension picks XML or binary).
         *
         * Not cached. A default-constructed T when the file does not open.
         */
        template <HasReflection T>
        Task<T> LoadSerializedAsync(StringView path);

        /** @brief The blocking form of LoadSerializedAsync. */
        template <HasReflection T>
        T LoadSerialized(StringView path);

        /**
         * @brief The object @p T of one part of an asset file (`gem.asset.xml`: `<sim>`, `<visual>`, `<editor>`), reading that part only
         *
         * The split file of the part is read when it is fresh (OpenAssetPart); no object of another part is created or filled, and a
         * reflected type the part does not allow is an error at its path and stays default. Empty when the file does not open or the
         * part or the object is absent; failures are logged like LoadSerialized.
         */
        template <HasReflection T>
        Optional<T> LoadSerializedPart(StringView path, AssetPurpose part);

        /// LoadSerializedPart on a background thread (the path is copied)
        template <HasReflection T>
        Task<Optional<T>> LoadSerializedPartAsync(StringView path, AssetPurpose part);

        /** @brief Loaders are asked in registration order; a null loader is refused. */
        void RegisterLoader(IntrusivePtr<IResourceLoader> loader);

        void ClearCache();

        /**
         * @brief Drops one path from the cache, so the next Load reads the file again (a file rewritten on disk, a texture the editor no longer shows).
         *
         * A handle already given out keeps the old resource; whoever holds one loads the path again to see the new file.
         * @return Whether the path was cached
         */
        bool Evict(StringView path);

        const ResourceCache& GetCache() const;

    private:
        IResourceLoader* FindLoader(StringView extension) const;

        /**
         * @brief The loader of the longest suffix of the file name that some loader accepts.
         *
         * "ui/hud.ui.xml" asks for ".ui.xml" first and ".xml" next, so a format inside XML files can have its own loader.
         */
        IResourceLoader* FindLoaderForPath(StringView path) const;

        template <HasReflection T>
        static Optional<T> ReadPartObject(StringView path, AssetPurpose part);

        ResourceCache _cache;
        Vector<IntrusivePtr<IResourceLoader>> _loaders;
    };

    template <typename T>
    requires std::derived_from<T, IResource> Task<ResourceHandle<T>> ResourceManager::LoadAsync(StringView path) {
        PoolString pathKey = PoolString::Intern(path);

        if (auto cached = _cache.Get(pathKey)) {
            if (auto typed = dynamic_cast<T*>(cached.Get())) {
                co_return ResourceHandle<T>(IntrusivePtr<T>(typed));
            }
        }

        auto* loader = FindLoaderForPath(path);
        if (!loader) {
            LOG_ERROR(Format("No loader found for: {}", path).c_str());
            co_return ResourceHandle<T>();
        }

        auto resourceResult = co_await loader->LoadAsync(pathKey);
        if (!resourceResult.has_value() || !resourceResult.value()) {
            LOG_ERROR(Format("Failed to load resource: {}", path).c_str());
            co_return ResourceHandle<T>();
        }

        auto resource = resourceResult.value();

        _cache.Put(pathKey, resource);

        if (auto typed = dynamic_cast<T*>(resource.Get())) {
            co_return ResourceHandle<T>(IntrusivePtr<T>(typed));
        }

        LOG_ERROR(Format("Resource type mismatch: {}", path).c_str());
        co_return ResourceHandle<T>();
    }

    template <typename T>
    requires std::derived_from<T, IResource> ResourceHandle<T> ResourceManager::Load(StringView path) {
        PoolString pathKey = PoolString::Intern(path);

        if (auto cached = _cache.Get(pathKey)) {
            if (auto typed = dynamic_cast<T*>(cached.Get())) {
                return ResourceHandle<T>(IntrusivePtr<T>(typed));
            }
        }

        auto* loader = FindLoaderForPath(path);
        if (!loader) {
            LOG_ERROR(Format("No loader found for: {}", path).c_str());
            return ResourceHandle<T>();
        }

        auto resource = loader->LoadSync(pathKey);
        if (!resource) {
            LOG_ERROR(Format("Failed to load resource: {}", path).c_str());
            return ResourceHandle<T>();
        }

        _cache.Put(pathKey, resource);

        if (auto typed = dynamic_cast<T*>(resource.Get())) {
            return ResourceHandle<T>(IntrusivePtr<T>(typed));
        }

        LOG_ERROR(Format("Resource type mismatch: {}", path).c_str());
        return ResourceHandle<T>();
    }

    template <HasReflection T>
    Task<T> ResourceManager::LoadSerializedAsync(StringView path) {
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load serialized resource"));

        auto opened = OpenDeserializer(path);
        if (!opened) {
            LOG_ERROR(Format("Failed to load file for deserialization: {}", opened.error()).c_str());
            co_return T{};
        }
        IDeserializer& deserializer = **opened;

        T result{};
        if (deserializer.BeginObject(ReflectionTraits<T>::name)) {
            result.Deserialize(deserializer);
            deserializer.EndObject();
        }
        LogDeserializeErrors(path, deserializer);

        co_return result;
    }

    template <HasReflection T>
    T ResourceManager::LoadSerialized(StringView path) {
        auto opened = OpenDeserializer(path);
        if (!opened) {
            LOG_ERROR(Format("Failed to load file for deserialization: {}", opened.error()).c_str());
            return T{};
        }
        IDeserializer& deserializer = **opened;

        T result{};
        if (deserializer.BeginObject(ReflectionTraits<T>::name)) {
            result.Deserialize(deserializer);
            deserializer.EndObject();
        }
        LogDeserializeErrors(path, deserializer);

        return result;
    }

    template <HasReflection T>
    Optional<T> ResourceManager::ReadPartObject(StringView path, AssetPurpose part) {
        auto opened = OpenAssetPart(path, part);
        if (!opened) {
            LOG_ERROR(Format("Failed to load file for deserialization: {}", opened.error()).c_str());
            return {};
        }
        if (!*opened) {
            return {};
        }
        IDeserializer& deserializer = **opened;

        Optional<T> result;
        if (deserializer.BeginObject(ReflectionTraits<T>::name)) {
            result.emplace();
            result->Deserialize(deserializer);
            deserializer.EndObject();
        }
        LogDeserializeErrors(path, deserializer);
        return result;
    }

    template <HasReflection T>
    Task<Optional<T>> ResourceManager::LoadSerializedPartAsync(StringView path, AssetPurpose part) {
        // The caller's view may be gone by the time the task resumes on the background thread
        const String ownedPath(path);
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load serialized part"));
        co_return ReadPartObject<T>(ownedPath, part);
    }

    template <HasReflection T>
    Optional<T> ResourceManager::LoadSerializedPart(StringView path, AssetPurpose part) {
        return ReadPartObject<T>(path, part);
    }

}  // namespace BECore
