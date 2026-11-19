#pragma once

#include <BECore/RefCounted/New.h>
#include <BECore/Types/Types.h>
#include <expected>
#include <gtest/gtest.h>
#include <source_location>
#include <type_traits>

namespace BECore::Tests {

    namespace RequireDetail {

        struct IStandIn {
            virtual ~IStandIn() = default;
        };

        /// A default-made T: through New for a ref-counted type, whose destructor is the release of the last reference
        template <typename T>
        struct StandIn final : IStandIn {
            static constexpr bool kRefCounted = std::is_base_of_v<RefCountedAtomic, T> || std::is_base_of_v<RefCountedNonAtomic, T>;
            std::conditional_t<kRefCounted, typename GetIntrusivePtrType<T>::type, UniquePtr<T>> object;

            StandIn() {
                if constexpr (kRefCounted) {
                    object = New<T>();
                } else {
                    object = MakeUnique<T>();
                }
            }
        };

        inline Vector<UniquePtr<IStandIn>>& Pool() {
            static Vector<UniquePtr<IStandIn>> pool;
            return pool;
        }

    }  // namespace RequireDetail

    /// Destroys the stand-ins Require() made; TestMain calls it after every case (a stand-in must not outlive the case that read through it)
    inline void ClearRequireStandIns() {
        RequireDetail::Pool().clear();
    }

    /**
     * @brief *@p found, or a fatal failure of the case at the line that asked and a blank T: a regression fails the case instead of ending the process.
     *
     * Only the case itself can return, so the case goes on with the blank (a default-made T, destroyed after the case): it reads defaults and fails
     * again, but touches no memory it does not own. A case that must not go on calls it through `ASSERT_NO_FATAL_FAILURE`, or checks the pointer with
     * `ASSERT_NE` itself. T must be default constructible.
     */
    template <typename T>
    T& Require(T* found, std::source_location where = std::source_location::current()) {
        using Plain = std::remove_const_t<T>;
        if (found != nullptr) {
            return *found;
        }
        [&] { FAIL() << "a lookup the case needs found nothing (" << where.file_name() << ":" << where.line() << ")"; }();
        auto standIn = MakeUnique<RequireDetail::StandIn<Plain>>();
        Plain& object = *standIn->object;
        RequireDetail::Pool().push_back(std::move(standIn));
        return object;
    }

    /// The value of @p found, or the same failure and a blank T. A temporary Optional lives to the end of the statement, and so may the reference
    template <typename T>
    const T& Require(const Optional<T>& found, std::source_location where = std::source_location::current()) {
        return Require(found.has_value() ? &*found : nullptr, where);
    }

    /// The value of @p found, or the same failure and a blank T when it holds an error
    template <typename T, typename TError>
    const T& Require(const std::expected<T, TError>& found, std::source_location where = std::source_location::current()) {
        return Require(found.has_value() ? &*found : nullptr, where);
    }

    /// @p container[@p index], or the same failure and a blank element when the container is shorter
    template <typename TContainer>
    const auto& At(const TContainer& container, size_t index, std::source_location where = std::source_location::current()) {
        using Element = std::remove_cvref_t<decltype(container[index])>;
        return Require(index < container.size() ? &container[index] : static_cast<const Element*>(nullptr), where);
    }

    /// The last element of @p container, or the same failure and a blank element when it is empty
    template <typename TContainer>
    const auto& Back(const TContainer& container, std::source_location where = std::source_location::current()) {
        using Element = std::remove_cvref_t<decltype(container.back())>;
        return Require(container.empty() ? static_cast<const Element*>(nullptr) : &container.back(), where);
    }

    template <typename T>
    T& Require(const IntrusivePtrAtomic<T>& found, std::source_location where = std::source_location::current()) {
        return Require(found.Get(), where);
    }

    template <typename T>
    T& Require(const IntrusivePtrNonAtomic<T>& found, std::source_location where = std::source_location::current()) {
        return Require(found.Get(), where);
    }

}  // namespace BECore::Tests
