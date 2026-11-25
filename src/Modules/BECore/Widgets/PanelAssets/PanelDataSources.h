#pragma once

#include <BECore/Reflection/TypedRef.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/Singleton.h>
#include <cstdint>

namespace BECore {

    class PanelDataSources;

    /// Gives the object of a source as it is now (empty when it has none): called at every read, so an object that appears or dies is found again
    using PanelSourceProvider = MoveOnlyFunction<TypedRef()>;

    /**
     * @brief The registration of one source: unregisters it when destroyed
     *
     * Move-only. It must not outlive the registry it came from. A name registered again replaces the provider and invalidates the older
     * handle of that name: destroying the older handle later removes nothing.
     */
    class PanelSourceHandle {
    public:
        PanelSourceHandle() = default;
        ~PanelSourceHandle();

        PanelSourceHandle(const PanelSourceHandle&) = delete;
        PanelSourceHandle& operator=(const PanelSourceHandle&) = delete;
        PanelSourceHandle(PanelSourceHandle&& other) noexcept;
        PanelSourceHandle& operator=(PanelSourceHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle or one that a later registration replaced)
        void Reset();

        [[nodiscard]] explicit operator bool() const {
            return _owner != nullptr;
        }

    private:
        friend class PanelDataSources;
        PanelSourceHandle(PanelDataSources& owner, String name, uint64_t token) : _owner(&owner), _name(std::move(name)), _token(token) {}

        PanelDataSources* _owner = nullptr;
        String _name;
        uint64_t _token = 0;
    };

    /**
     * @brief The named reflected objects a panel asset may bind to (`game.fields`): a model registers its object, the asset names it
     *
     * Main thread only, like `ControlRoots`, so it takes no lock. Names are held as `String` and looked up as text, never interned: a
     * name comes from an asset file. The registry owns no object and a `TypedRef` it gives is not kept past the frame.
     *
     * A registry may have a parent (a widget's own registry behind which the global one stands): a name is looked up in the own sources
     * first and, when the own sources do not hold it, in the parent. A name the own sources hold wins even when its provider gives no object.
     */
    class PanelDataSources : public Singleton<PanelDataSources> {
    public:
        PanelDataSources() = default;

        /// Registers @p provider under @p name; a source of the same name is replaced (logged once)
        [[nodiscard]] PanelSourceHandle Register(StringView name, PanelSourceProvider provider);

        /// The object of @p name now; empty when the name is unknown or the provider gives none. Never interns, allocates nothing
        [[nodiscard]] TypedRef Find(StringView name) const;

        [[nodiscard]] bool Contains(StringView name) const;

        /// The registry looked in when a name is not an own source; not owned, it must outlive this one. A cycle is the caller's bug: @p parent == this is refused and logged
        void SetParent(const PanelDataSources* parent);

    private:
        friend class PanelSourceHandle;

        struct Entry {
            PanelSourceProvider provider;
            uint64_t token = 0;
        };

        void Unregister(StringView name, uint64_t token);

        Map<String, Entry> _sources;
        uint64_t _nextToken = 1;
        const PanelDataSources* _parent = nullptr;
    };

}  // namespace BECore
