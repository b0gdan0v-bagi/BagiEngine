#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <EASTL/string_view.h>
#include <type_traits>

namespace BECore::Tests {

    class ITest : public RefCounted {
        BE_CLASS(ITest, FACTORY_BASE)
    public:
        ITest() = default;
        ~ITest() override = default;

        virtual bool Run() = 0;
        virtual eastl::string_view GetName() = 0;
    };

    /**
     * @brief Concept to validate that a type is a valid test class
     *
     * A valid test must:
     * - Have a Run() method returning bool
     * - Have GetStaticTypeName() returning eastl::string_view (from BE_CLASS)
     * - Be derived from ITest
     *
     * @example
     * static_assert(ValidTest<EnumUtilsTest>);
     * static_assert(ValidTest<ReflectionTest>);
     */
    template <typename T>
    concept ValidTest = requires(T t) {
        { t.Run() } -> std::same_as<bool>;
        { T::GetStaticTypeName() } -> std::convertible_to<eastl::string_view>;
        requires std::is_base_of_v<ITest, T>;
    };

}  // namespace BECore::Tests
