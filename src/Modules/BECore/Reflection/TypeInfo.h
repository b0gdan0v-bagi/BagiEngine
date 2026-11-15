#pragma once

#include <BECore/RefCounted/New.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Reflection/MethodInfo.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace BECore {

    /**
     * @brief An object made by `TypeInfo::create`: an erased pointer to the object of its own type, and the reference it owns
     *
     * Holds one reference of an intrusive pointer family (`RefCountedAtomic` or `RefCountedNonAtomic`) and gives it back with the
     * release thunk of the object's own type, so the object is deleted by its own class whatever the caller knows of it. `Get()` is
     * the address of the **made type** (the derived one); a base subobject is at `TypeInfo::parentOffset` from it.
     */
    class OwnedObject {
    public:
        OwnedObject() = default;

        OwnedObject(const OwnedObject&) = delete;
        OwnedObject& operator=(const OwnedObject&) = delete;

        OwnedObject(OwnedObject&& other) noexcept : _object(std::exchange(other._object, nullptr)), _release(std::exchange(other._release, nullptr)) {}

        OwnedObject& operator=(OwnedObject&& other) noexcept {
            if (this != &other) {
                Reset();
                _object = std::exchange(other._object, nullptr);
                _release = std::exchange(other._release, nullptr);
            }
            return *this;
        }

        ~OwnedObject() {
            Reset();
        }

        /// Takes over the reference @p pointer holds
        template <typename Ptr>
        requires requires(Ptr& pointer) {
            pointer.Get()->AddRef();
            pointer.Reset();
        }
        static OwnedObject Adopt(Ptr pointer) {
            using Object = std::remove_pointer_t<decltype(pointer.Get())>;
            OwnedObject owned;
            if (Object* object = pointer.Get(); object != nullptr) {
                object->AddRef();
                pointer.Reset();
                owned._object = object;
                owned._release = [](void* raw) { static_cast<const Object*>(raw)->ReleaseRef(); };
            }
            return owned;
        }

        [[nodiscard]] void* Get() const {
            return _object;
        }

        explicit operator bool() const {
            return _object != nullptr;
        }

        /// Gives the reference back; the object is deleted when it was the last one
        void Reset() {
            if (_object != nullptr) {
                _release(_object);
                _object = nullptr;
                _release = nullptr;
            }
        }

    private:
        void* _object = nullptr;
        void (*_release)(void*) = nullptr;
    };

    /// One parameter of a reflected method
    struct ParamDesc {
        StringView name;
        /// `Unknown` for a type that cannot be read from outside (an interface reference, a pointer)
        const TypeDesc* type = nullptr;
    };

    /**
     * @brief One reflected field of a type
     *
     * `offset` is the distance from the object's address to the field, taken once from the member pointer (`TypeInfo.h`,
     * `Detail::SubobjectOffset`); `address` finds the same field with well defined C++ and is the reference the offset is checked
     * against. A reader that needs speed uses the offset, nothing else may assume it.
     */
    struct FieldDesc {
        /// The name in a file: the C++ name without its leading underscore
        StringView name;
        StringView cppName;
        FieldAttributes attributes;
        const TypeDesc* type = nullptr;
        size_t offset = 0;
        void* (*address)(void* object) = nullptr;
    };

    /**
     * @brief One method marked `BE_FUNCTION`
     *
     * `callable`: every parameter can be default made and read through `Ser::Load` and the result is `void` or can be written through
     * `Ser::Save`. A method that is not (an `IDeserializer&`, a pointer, a non-const reference) is listed all the same.
     */
    struct MethodDesc {
        StringView name;
        bool isConst = false;
        bool callable = false;
        Span<const ParamDesc> params{};
        /// `Void` for no result, `Unknown` for a result that cannot be written
        const TypeDesc* result = nullptr;
    };

    /**
     * @brief What the engine knows of one reflected type (`BE_CLASS`, `BE_EVENT`) at run time
     *
     * Built once per type by `MakeTypeInfo<T>()`, static storage, found through `TypeRegistry`. It holds only the type's own fields
     * and methods; the parent is named by hash (registration order across translation units is not defined) and `TypeRegistry`
     * walks the chain. The thunks work on a pointer to the object of **this** type.
     */
    struct TypeInfo {
        ClassMeta meta;
        /// `ClassMeta::typeHash` of the reflected parent; 0 for a type with none
        uint64_t parentHash = 0;
        /// Where the parent subobject sits in this type (not 0 when another base comes first)
        size_t parentOffset = 0;
        /// `ClassMeta::typeHash` of the base of the `AbstractFactory` this type registers in; 0 when it registers in none
        uint64_t factoryBase = 0;
        bool isAbstract = false;
        Span<const FieldDesc> fields{};
        Span<const MethodDesc> methods{};
        /// Null for a type with no `AcceptPropertyVisitor` (a `BE_EVENT`)
        bool (*acceptVisitor)(void* object, IPropertyVisitor& visitor) = nullptr;
        void (*serialize)(const void* object, ISerializer& serializer) = nullptr;
        void (*deserialize)(void* object, IDeserializer& deserializer) = nullptr;
        /// Null for an abstract type and for a type that registers in no `AbstractFactory`
        OwnedObject (*create)() = nullptr;

        [[nodiscard]] StringView Name() const {
            return meta.typeName;
        }

        [[nodiscard]] uint64_t Hash() const {
            return meta.typeHash;
        }
    };

    namespace Detail {

        /**
         * @brief The distance from the address of a `T` to the part of it that @p project picks (a member, a base subobject)
         *
         * The one place of the reflection that is formally not defined behaviour: @p project is called on an aligned address where no
         * object lives and only forms the address of a part, it never reads. Every compiler lays a non-virtual base and a member
         * out at a fixed distance, which is the thing wanted; the well defined twin is `FieldDesc::address`, and a test compares the
         * two on live objects. **Non-virtual bases only**: the part of a virtual base is found through the object's vtable, so on
         * the dummy address this would crash (at static initialization, since the registration calls it).
         */
        template <typename T, typename Project>
        size_t SubobjectOffset(Project project) {
            // A multiple of any alignment a type can ask for
            constexpr uintptr_t kDummyAddress = 0x100000;
            const T* fake = reinterpret_cast<const T*>(kDummyAddress);
            const void* part = project(fake);
            return static_cast<size_t>(reinterpret_cast<uintptr_t>(part) - kDummyAddress);
        }

        template <typename P>
        struct MemberPointerTraits;

        template <typename C, typename M>
        struct MemberPointerTraits<M C::*> {
            using Class = C;
            using Member = M;
        };

        /// The name a field has in a file (`Ser::Save` key): without the leading underscore
        constexpr StringView SerializedFieldName(StringView cppName) {
            return !cppName.empty() && cppName[0] == '_' ? cppName.substr(1) : cppName;
        }

        template <typename T, auto Ptr>
        void* FieldAddress(void* object) {
            return std::addressof(static_cast<T*>(object)->*Ptr);
        }

        template <typename T, size_t I>
        FieldDesc MakeFieldDesc() {
            constexpr auto info = std::get<I>(ReflectionTraits<T>::fields);
            constexpr auto ptr = info.ptr;
            using Member = typename MemberPointerTraits<std::remove_cv_t<decltype(ptr)>>::Member;
            static_assert(Describable<Member>, "A reflected field needs a Ser::DataAccessor (Reflection/DataAccessor.h)");

            FieldDesc desc;
            desc.name = SerializedFieldName(info.name);
            desc.cppName = info.name;
            desc.attributes = info.attributes;
            desc.type = &DescribeType<Member>();
            desc.offset = SubobjectOffset<T>([](const T* object) { return static_cast<const void*>(std::addressof(object->*ptr)); });
            desc.address = &FieldAddress<T, ptr>;
            return desc;
        }

        /// One parameter or result as `Ser` handles it: made by default, read by `Ser::Load`; a non-const reference could not carry the change back
        template <typename P>
        concept LoadableParam = (!std::is_lvalue_reference_v<P> || std::is_const_v<std::remove_reference_t<P>>) && Describable<std::remove_cvref_t<P>> &&
                                Ser::HasDataAccessor<std::remove_cvref_t<P>> && std::default_initializable<std::remove_cvref_t<P>>;

        template <typename R>
        concept SavableResult = std::is_void_v<R> || (Describable<std::remove_cvref_t<R>> && Ser::HasDataAccessor<std::remove_cvref_t<R>>);

        /// What a method's declaration tells the registry; @p Const is the `const` of the member function
        template <bool Const, typename R, typename... A>
        struct MethodSignatureOf {
            using Result = R;
            static constexpr bool kConst = Const;
            static constexpr size_t kParamCount = sizeof...(A);
            static constexpr bool kCallable = (LoadableParam<A> && ...) && SavableResult<R>;
            template <size_t I>
            using Param = std::tuple_element_t<I, Tuple<A...>>;
        };

        template <typename Info>
        struct MethodSignature;

        template <typename C, typename R, typename... A>
        struct MethodSignature<MethodInfo<C, R, A...>> : MethodSignatureOf<false, R, A...> {};

        template <typename C, typename R, typename... A>
        struct MethodSignature<ConstMethodInfo<C, R, A...>> : MethodSignatureOf<true, R, A...> {};

        /// The @p index-th name of the comma separated @p names; empty when there is none
        constexpr StringView ParamNameAt(StringView names, size_t index) {
            size_t start = 0;
            size_t current = 0;
            for (size_t i = 0; i <= names.size(); ++i) {
                if (i == names.size() || names[i] == ',') {
                    if (current == index) {
                        return names.substr(start, i - start);
                    }
                    ++current;
                    start = i + 1;
                }
            }
            return {};
        }

        /// `arg<Index>`, in static storage
        template <size_t Index>
        struct GeneratedArgName {
            static constexpr StringView kPrefix = "arg";
            static constexpr size_t kDigits = [] {
                size_t digits = 1;
                for (size_t rest = Index; rest >= 10; rest /= 10) {
                    ++digits;
                }
                return digits;
            }();
            static constexpr size_t kLength = kPrefix.size() + kDigits;
            static constexpr Array<char, kLength + 1> kText = [] {
                Array<char, kLength + 1> text{};
                for (size_t i = 0; i < kPrefix.size(); ++i) {
                    text.data()[i] = kPrefix[i];
                }
                size_t rest = Index;
                for (size_t position = kLength; position > kPrefix.size(); --position) {
                    text.data()[position - 1] = static_cast<char>('0' + rest % 10);
                    rest /= 10;
                }
                return text;
            }();
            static constexpr StringView kView{kText.data(), kLength};
        };

        template <size_t Index>
        constexpr StringView ParamName(StringView names) {
            const StringView named = ParamNameAt(names, Index);
            return named.empty() ? GeneratedArgName<Index>::kView : named;
        }

        template <typename Signature, size_t... Is>
        constexpr Array<ParamDesc, sizeof...(Is)> BuildParams(StringView names, std::index_sequence<Is...> /*indices*/) {
            if constexpr (sizeof...(Is) == 0) {
                return {};
            } else {
                return {{ParamDesc{ParamName<Is>(names), &DescribeSignatureType<typename Signature::template Param<Is>>()}...}};
            }
        }

        template <typename T, size_t I>
        struct MethodTable {
            using Info = std::remove_cvref_t<std::tuple_element_t<I, std::remove_cvref_t<decltype(ReflectionTraits<T>::methods)>>>;
            using Signature = MethodSignature<Info>;
            static constexpr Array<ParamDesc, Signature::kParamCount> kParams =
                BuildParams<Signature>(std::get<I>(ReflectionTraits<T>::methods).paramNames, std::make_index_sequence<Signature::kParamCount>{});
        };

        template <typename T, size_t I>
        MethodDesc MakeMethodDesc() {
            using Table = MethodTable<T, I>;
            MethodDesc desc;
            desc.name = std::get<I>(ReflectionTraits<T>::methods).name;
            desc.isConst = Table::Signature::kConst;
            desc.callable = Table::Signature::kCallable;
            desc.params = Span<const ParamDesc>(Table::kParams.data(), Table::kParams.size());
            desc.result = &DescribeSignatureType<typename Table::Signature::Result>();
            return desc;
        }

        template <typename T>
        OwnedObject CreateObject() {
            return OwnedObject::Adopt(New<T>());
        }

        template <typename T>
        constexpr size_t FieldCountOf() {
            return std::tuple_size_v<std::remove_cvref_t<decltype(ReflectionTraits<T>::fields)>>;
        }

        template <typename T>
        constexpr size_t MethodCountOf() {
            if constexpr (HasMethodReflection<T>) {
                return std::tuple_size_v<std::remove_cvref_t<decltype(ReflectionTraits<T>::methods)>>;
            } else {
                return 0;
            }
        }

        template <typename T>
        concept HasAcceptPropertyVisitor = requires(T & object, IPropertyVisitor& visitor) {
            {object.AcceptPropertyVisitor(visitor)}->std::convertible_to<bool>;
        };

        /// The fields, the methods and the `TypeInfo` of one type, built in place once (the `TypeInfo` spans point into the arrays)
        template <typename T, typename Parent, typename FactoryBase>
        struct TypeStorage {
            static constexpr size_t kFieldCount = FieldCountOf<T>();
            static constexpr size_t kMethodCount = MethodCountOf<T>();

            Array<FieldDesc, kFieldCount> fields{};
            Array<MethodDesc, kMethodCount> methods{};
            TypeInfo info;

            TypeStorage() {
                FillFields(std::make_index_sequence<kFieldCount>{});
                FillMethods(std::make_index_sequence<kMethodCount>{});
                info.meta = T::GetStaticTypeMeta();
                info.isAbstract = std::is_abstract_v<T>;
                info.fields = Span<const FieldDesc>(fields.data(), fields.size());
                info.methods = Span<const MethodDesc>(methods.data(), methods.size());

                if constexpr (!std::is_void_v<Parent>) {
                    static_assert(std::is_base_of_v<Parent, T>, "The parent of a reflected type must be one of its bases");
                    info.parentHash = Parent::GetStaticTypeMeta().typeHash;
                    info.parentOffset = SubobjectOffset<T>([](const T* object) { return static_cast<const void*>(static_cast<const Parent*>(object)); });
                }
                if constexpr (HasAcceptPropertyVisitor<T>) {
                    info.acceptVisitor = [](void* object, IPropertyVisitor& visitor) -> bool { return static_cast<T*>(object)->AcceptPropertyVisitor(visitor); };
                }
                // The generator declares `Serialize` / `Deserialize` of a fieldless `BE_EVENT` and never defines them, so naming them would not link
                constexpr bool kDefinesSerialize = kFieldCount > 0 || HasAcceptPropertyVisitor<T>;
                if constexpr (kDefinesSerialize && requires(const T& object, ISerializer& serializer) { object.Serialize(serializer); }) {
                    info.serialize = [](const void* object, ISerializer& serializer) { static_cast<const T*>(object)->Serialize(serializer); };
                }
                if constexpr (kDefinesSerialize && requires(T & object, IDeserializer & deserializer) { object.Deserialize(deserializer); }) {
                    info.deserialize = [](void* object, IDeserializer& deserializer) { static_cast<T*>(object)->Deserialize(deserializer); };
                }
                if constexpr (!std::is_void_v<FactoryBase>) {
                    info.factoryBase = FactoryBase::GetStaticTypeMeta().typeHash;
                    if constexpr (!std::is_abstract_v<T>) {
                        info.create = &CreateObject<T>;
                    }
                }
            }

            TypeStorage(const TypeStorage&) = delete;
            TypeStorage& operator=(const TypeStorage&) = delete;

        private:
            template <size_t... Is>
            void FillFields(std::index_sequence<Is...> /*indices*/) {
                ((fields.data()[Is] = MakeFieldDesc<T, Is>()), ...);
            }

            template <size_t... Is>
            void FillMethods(std::index_sequence<Is...> /*indices*/) {
                ((methods.data()[Is] = MakeMethodDesc<T, Is>()), ...);
            }
        };

    }  // namespace Detail

    /**
     * @brief The `TypeInfo` of a reflected type, built at the first call (the generated registration block calls it from a static initializer)
     *
     * @tparam Parent the reflected parent the generator wrote (`ReflectionTraits<T>::Parent`), `void` for none; naming it by hand gives
     *         a second `TypeInfo` of the same type, so only a test does
     * @tparam FactoryBase the base of the `AbstractFactory` the type registers in, `void` for none: it gives `create` (when the type is
     *         not abstract) and `factoryBase`
     *
     * Needs the generated traits of @p T, so it is called where the type's `.gen.hpp` is included. Allocates nothing.
     */
    template <typename T, typename Parent = ReflectedParentOf<T>, typename FactoryBase = void>
    const TypeInfo& MakeTypeInfo() {
        static const Detail::TypeStorage<T, Parent, FactoryBase> storage;
        return storage.info;
    }

}  // namespace BECore
