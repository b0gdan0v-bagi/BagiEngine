#pragma once

#include <BECore/Resource/IResource.h>
#include <BECore/Reflection/TypeTraits.h>

namespace BECore {

    /**
     * @brief Resource containing a deserialized reflected object
     * 
     * Template resource for any type with BE_CLASS and BE_REFLECT_FIELD.
     * Automatically deserializes from XML or binary archives.
     * 
     * @tparam T Reflected type to deserialize (must satisfy HasReflection)
     * 
     * @example
     * // Assuming Player has BE_CLASS and BE_REFLECT_FIELD
     * struct Player {
     *     BE_CLASS(Player)
     *     BE_REFLECT_FIELD int32_t health = 100;
     *     BE_REFLECT_FIELD float speed = 5.0f;
     * };
     * 
     * auto handle = resourceManager.Load<SerializedResource<Player>>("saves/player.xml");
     * if (handle) {
     *     Player& player = handle->Get();
     *     LOG_INFO("Player health: {}", player.health);
     * }
     */
    template<HasReflection T>
    class SerializedResource : public IResource {
        // Note: BE_CLASS cannot be used in templates due to naming conflicts
        // This type doesn't need reflection since it's a template
    public:
        SerializedResource() = default;
        ~SerializedResource() override = default;
        
        // IResource interface
        ResourceState GetState() const override {
            return _state;
        }
        
        PoolString GetPath() const override {
            return _path;
        }
        
        uint64_t GetMemoryUsage() const override {
            return sizeof(*this);
        }
        
        PoolString GetTypeName() const override {
            static PoolString typeName = PoolString::Intern(
                fmt::format("SerializedResource<{}>", ReflectionTraits<T>::name)
            );
            return typeName;
        }
        
        /**
         * @brief Get reference to deserialized object
         * @return Reference to contained object
         */
        T& Get() {
            return _data;
        }
        
        /**
         * @brief Get const reference to deserialized object
         * @return Const reference to contained object
         */
        const T& Get() const {
            return _data;
        }

    private:
        friend class SerializedResourceLoader;
        
        /**
         * @brief Set the loaded data
         * @param path Virtual path to the resource
         * @param data Deserialized object
         */
        void SetLoaded(PoolString path, T data) {
            _path = path;
            _data = std::move(data);
            _state = ResourceState::Loaded;
        }
        
        /**
         * @brief Mark resource as failed
         * @param path Virtual path to the resource
         */
        void SetFailed(PoolString path) {
            _path = path;
            _state = ResourceState::Failed;
        }
        
        PoolString _path;
        T _data;
        ResourceState _state = ResourceState::Unloaded;
    };

}  // namespace BECore
