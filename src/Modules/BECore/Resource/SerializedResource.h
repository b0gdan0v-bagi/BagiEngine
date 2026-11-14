#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Resource/IResource.h>
#include <cstdint>
#include <utility>

namespace BECore {

    /**
     * @brief A reflected object T held as a resource.
     *
     * Nothing fills it today: its friend SerializedResourceLoader does not exist, so Load<SerializedResource<T>>
     * finds no loader. ResourceManager::LoadSerialized returns the object by value instead.
     */
    template <HasReflection T>
    class SerializedResource : public IResource {
        // No BE_CLASS: a template cannot take it (name conflicts), and a template needs no reflection.
    public:
        SerializedResource() = default;
        ~SerializedResource() override = default;

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
            static PoolString typeName = PoolString::Intern(Format("SerializedResource<{}>", ReflectionTraits<T>::name));
            return typeName;
        }

        T& Get() {
            return _data;
        }

        const T& Get() const {
            return _data;
        }

    private:
        friend class SerializedResourceLoader;

        void SetLoaded(PoolString path, T data) {
            _path = path;
            _data = std::move(data);
            _state = ResourceState::Loaded;
        }

        void SetFailed(PoolString path) {
            _path = path;
            _state = ResourceState::Failed;
        }

        PoolString _path;
        T _data;
        ResourceState _state = ResourceState::Unloaded;
    };

}  // namespace BECore
