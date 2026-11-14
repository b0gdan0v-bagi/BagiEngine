#pragma once

#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Reflection/SerializationTraits.h>
#include <cstdio>

namespace BECore {

    /**
     * @brief Dispatch helpers for IPropertyVisitor, analogous to Ser::Save/Ser::Load
     *
     * PropVisit::Visit(visitor, field, key) selects the right overload based on the
     * field type:
     *   - Arithmetic types              → visitor.Visit(key, field) directly
     *   - PoolString / String (and any string type) → visitor.Visit(key, field) directly
     *   - CORE_ENUM (IsReflectedEnum)   → visitor.VisitEnum(key, names, count, index)
     *   - Types with VisitProperties()  → BeginCompound + VisitProperties + EndCompound
     *   - BE_CLASS T by value           → BeginCompound + AcceptPropertyVisitor + EndCompound
     *   - IntrusivePtr<BE_CLASS T>      → BeginCompound + ptr->AcceptPropertyVisitor + EndCompound
     *   - vector<IntrusivePtr<T>>       → BeginCompound + iterate elements + EndCompound
     *
     * Generated AcceptPropertyVisitor implementations call these helpers.
     */
    namespace PropVisit {

        // =====================================================================
        // Detail helpers for intrusive pointer dispatch
        // =====================================================================

        namespace Detail {

            template <typename Ptr>
            struct IntrusivePtrPointee {};

            template <typename T>
            struct IntrusivePtrPointee<IntrusivePtrAtomic<T>> {
                using type = T;
            };

            template <typename T>
            struct IntrusivePtrPointee<IntrusivePtrNonAtomic<T>> {
                using type = T;
            };

            template <typename Ptr>
            concept IsIntrusivePtr = requires {
                typename IntrusivePtrPointee<Ptr>::type;
            };

        }  // namespace Detail

        // =====================================================================
        // Arithmetic types
        // =====================================================================

        template <typename T>
        requires std::is_arithmetic_v<T> bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            return v.Visit(key, value);
        }

        // =====================================================================
        // PoolString
        // =====================================================================

        inline bool Visit(IPropertyVisitor& v, PoolString& value, eastl::string_view key) {
            return v.Visit(key, value);
        }

        // =====================================================================
        // Strings: String, a string of any allocator, FixedString
        // =====================================================================

        namespace Detail {

            template <typename Text>
            bool VisitText(IPropertyVisitor& v, Text& value, eastl::string_view key) {
                if constexpr (std::is_same_v<Text, String>) {
                    return v.Visit(key, value);
                } else {
                    // The visitor edits a String: edit a copy and take it back when it changed
                    String text(value.data(), value.size());
                    if (!v.Visit(key, text)) {
                        return false;
                    }
                    ::BECore::Detail::AssignText(value, StringView(text.data(), text.size()));
                    return true;
                }
            }

        }  // namespace Detail

        template <typename Alloc>
        bool Visit(IPropertyVisitor& v, eastl::basic_string<char, Alloc>& value, eastl::string_view key) {
            return Detail::VisitText(v, value, key);
        }

        // Its own overload: through the base above, the type would be a basic_string and the length limit of the FixedString lost
        template <int N, bool Overflow, typename OverflowAlloc>
        bool Visit(IPropertyVisitor& v, eastl::fixed_string<char, N, Overflow, OverflowAlloc>& value, eastl::string_view key) {
            return Detail::VisitText(v, value, key);
        }

        // =====================================================================
        // Compound types (with VisitProperties method)
        // =====================================================================

        template <typename T>
        requires requires(T& t, IPropertyVisitor& visitor) {
            {
                t.VisitProperties(visitor)
            } -> std::same_as<bool>;
        }
        bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            if (v.BeginCompound(key)) {
                bool changed = value.VisitProperties(v);
                v.EndCompound();
                return changed;
            }
            return false;
        }

        // =====================================================================
        // IntrusivePtr<T> (with AcceptPropertyVisitor)
        // =====================================================================

        template <typename Ptr>
        requires(Detail::IsIntrusivePtr<Ptr>&& requires(typename Detail::IntrusivePtrPointee<Ptr>::type& t, IPropertyVisitor& v) {
            {
                t.AcceptPropertyVisitor(v)
            } -> std::same_as<bool>;
        }) bool Visit(IPropertyVisitor& v, Ptr& ptr, eastl::string_view key) {
            if (!ptr) {
                return false;
            }
            if (v.BeginCompound(key)) {
                bool changed = ptr->AcceptPropertyVisitor(v);
                v.EndCompound();
                return changed;
            }
            return false;
        }

        // =====================================================================
        // eastl::vector<IntrusivePtr<T>>
        // =====================================================================

        template <typename Ptr, typename Alloc>
        requires Detail::IsIntrusivePtr<Ptr> bool Visit(IPropertyVisitor& v, eastl::vector<Ptr, Alloc>& vec, eastl::string_view key) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                char indexBuf[32];
                for (size_t i = 0; i < vec.size(); ++i) {
                    snprintf(indexBuf, sizeof(indexBuf), "[%zu]", i);
                    changed |= Visit(v, vec[i], eastl::string_view(indexBuf));
                }
                v.EndCompound();
            }
            return changed;
        }

        // =====================================================================
        // BE_CLASS types embedded by value (have AcceptPropertyVisitor directly)
        // =====================================================================

        template <typename T>
        requires(!Detail::IsIntrusivePtr<T> && !IsReflectedEnum<T> && !std::is_arithmetic_v<T> && requires(T & t, IPropertyVisitor& v) {
            {
                t.AcceptPropertyVisitor(v)
            } -> std::same_as<bool>;
        }) bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            if (v.BeginCompound(key)) {
                bool changed = value.AcceptPropertyVisitor(v);
                v.EndCompound();
                return changed;
            }
            return false;
        }

        // =====================================================================
        // CORE_ENUM / IsReflectedEnum types
        // =====================================================================

        template <typename T>
        requires IsReflectedEnum<T> bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            const auto& names = EnumUtils<T>::Names();
            const auto& values = EnumUtils<T>::Values();
            size_t selected = 0;
            for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] == value) {
                    selected = i;
                    break;
                }
            }
            if (v.VisitEnum(key, names.data(), names.size(), selected)) {
                value = values[selected];
                return true;
            }
            return false;
        }

    }  // namespace PropVisit

}  // namespace BECore
