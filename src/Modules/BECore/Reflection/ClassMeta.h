#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/StringUtils.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The part of an asset a reflected type belongs to (`BE_CLASS(Name, PURPOSE=sim|visual|editor)`)
     *
     * `Common` is the default: plain data (ids, vectors, colours) that every part may hold. A part is named by its
     * purpose (`PurposeName`), and a loader of one part refuses the types of the others (`IsAllowedIn`).
     */
    CORE_ENUM(AssetPurpose, uint8_t,
              Common,  ///< allowed in every part
              Sim,     ///< game logic: loads without any visual class linked
              Visual,  ///< what the player sees and hears
              Editor   ///< data of the editor only
    )

    /// The lower-case name of a purpose and of its part in an asset file (`sim`, `visual`, `editor`, `common`).
    constexpr StringView PurposeName(AssetPurpose purpose) {
        switch (purpose) {
            case AssetPurpose::Common:
                return "common";
            case AssetPurpose::Sim:
                return "sim";
            case AssetPurpose::Visual:
                return "visual";
            case AssetPurpose::Editor:
                return "editor";
        }
        return "common";
    }

    /// The part named by `text` (`sim`, `visual`, `editor`); `Common` is no part and does not parse.
    constexpr Optional<AssetPurpose> TryParsePurpose(StringView text) {
        for (const AssetPurpose purpose : {AssetPurpose::Sim, AssetPurpose::Visual, AssetPurpose::Editor}) {
            if (text == PurposeName(purpose)) {
                return purpose;
            }
        }
        return {};
    }

    /// A type of purpose `typePurpose` may be stored in the part `part`.
    constexpr bool IsAllowedIn(AssetPurpose typePurpose, AssetPurpose part) {
        return typePurpose == AssetPurpose::Common || typePurpose == part;
    }

    /**
     * @brief Type identity of a reflected class: the FNV-1a hash of its name, so types compare and factories
     * look up without RTTI.
     */
    struct ClassMeta {
        uint64_t typeHash = 0;
        StringView typeName;
        /// Lucide icon name from `BE_CLASS(..., ICON=<name>)` (kebab case, no prefix); empty when the class names none.
        /// The editor maps it to a glyph; the engine core knows no font.
        StringView icon{};
        /// Part of an asset the class belongs to, from `BE_CLASS(..., PURPOSE=<part>)`; `Common` when it names none.
        AssetPurpose purpose = AssetPurpose::Common;

        /// Equality compares typeHash only (name, icon and purpose are ignored).
        constexpr bool operator==(const ClassMeta& other) const {
            return typeHash == other.typeHash;
        }

        constexpr bool operator!=(const ClassMeta& other) const {
            return typeHash != other.typeHash;
        }

        /// False for a default ClassMeta (zero hash).
        constexpr explicit operator bool() const {
            return typeHash != 0;
        }
    };

    /**
     * @brief ClassMeta of a reflected type at compile time (needs ReflectionTraits<T>::name)
     *
     * Only hash and name are filled: icon and purpose stay default here, the generated GetStaticTypeMeta()
     * of a `BE_CLASS` carries them.
     */
    template <typename T>
    requires HasReflection<T> consteval ClassMeta GetClassMeta() {
        constexpr auto name = ReflectionTraits<T>::name;
        return ClassMeta{StringUtils::GetHash(name), name};
    }

}  // namespace BECore
