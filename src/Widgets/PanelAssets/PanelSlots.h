#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/Singleton.h>
#include <cstdint>

namespace BECore {

    class PanelSlots;

    /// Draws one slot inside the window the interpreter has begun
    using PanelSlotDraw = MoveOnlyFunction<void()>;

    /**
     * @brief The registration of one slot: unregisters it when destroyed
     *
     * Move-only; it must not outlive the registry. A name registered again replaces the drawer and invalidates the older handle of that name.
     */
    class PanelSlotHandle {
    public:
        PanelSlotHandle() = default;
        ~PanelSlotHandle();

        PanelSlotHandle(const PanelSlotHandle&) = delete;
        PanelSlotHandle& operator=(const PanelSlotHandle&) = delete;
        PanelSlotHandle(PanelSlotHandle&& other) noexcept;
        PanelSlotHandle& operator=(PanelSlotHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle or one that a later registration replaced)
        void Reset();

        [[nodiscard]] explicit operator bool() const {
            return _owner != nullptr;
        }

    private:
        friend class PanelSlots;
        PanelSlotHandle(PanelSlots& owner, String name, uint64_t token) : _owner(&owner), _name(std::move(name)), _token(token) {}

        PanelSlots* _owner = nullptr;
        String _name;
        uint64_t _token = 0;
    };

    /**
     * @brief The hand-written drawers a `PanelSlot` element of an asset calls by name
     *
     * For what the format cannot say (a viewport-like control, a chart). Main thread only, so no lock. Names are `String`s looked up as text, never
     * interned: a name comes from an asset file.
     */
    class PanelSlots : public Singleton<PanelSlots> {
    public:
        PanelSlots() = default;

        /// Registers @p draw under @p name; a slot of the same name is replaced (logged once)
        [[nodiscard]] PanelSlotHandle Register(StringView name, PanelSlotDraw draw);

        /// The drawer of @p name, or null. Valid until the next Register or Unregister, never kept past the call that uses it
        [[nodiscard]] PanelSlotDraw* Find(StringView name);

    private:
        friend class PanelSlotHandle;

        struct Entry {
            PanelSlotDraw draw;
            uint64_t token = 0;
        };

        void Unregister(StringView name, uint64_t token);

        Map<String, Entry> _slots;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
