#pragma once

#include <BECore/Types/Types.h>
#include <concepts>

namespace BECore {

    /**
     * @brief Links a factory base type to its generated enum and XML element name.
     *
     * The primary template is left undefined. The meta-generator writes the specializations
     * (FactoryTraitsXxx.gen.hpp) for every type marked `BE_CLASS(Name, FACTORY_BASE)`.
     *
     * @tparam T The factory base type (e.g., ILogSink, IAssertHandler)
     *
     * @example Generated specialization:
     * @code
     * template<>
     * struct FactoryTraits<ILogSink> {
     *     using EnumType = LogSinkType;
     *     static constexpr eastl::string_view elementName = "sink";
     * };
     * @endcode
     */
    template <typename T>
    struct FactoryTraits;

    /**
     * @brief Whether FactoryTraits<T> is specialised with EnumType and elementName.
     *
     * Creation itself goes through AbstractFactory<T>::GetInstance().
     */
    template <typename T>
    concept HasFactory = requires {
        typename FactoryTraits<T>::EnumType;
        {FactoryTraits<T>::elementName}->std::convertible_to<StringView>;
    };

}  // namespace BECore
