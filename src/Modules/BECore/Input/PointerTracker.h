#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class IUIInputHandler;

    /**
     * @brief What UIInput remembers between events: which handler holds a pressed button and which one the pointer is over.
     *
     * A pointer is the `which` of an SDL mouse event, a button is the SDL button index. The tracker only stores and
     * compares pointers to handlers and never calls one, so a handler that is gone can only be forgotten (Forget), not
     * asked. It holds its entries inside the object: no heap allocation, ever. When all kMaxEntries places are taken, a new
     * capture or hover of another key is refused (the return value says so); the dispatcher then simply does not hold it,
     * which is the behaviour of a handler without a capture.
     */
    class PointerTracker {
    public:
        /// Places for captures and, separately, for hovers; far more than a mouse and a few fingers press at once
        static constexpr size_t kMaxEntries = 8;

        /// A handler holds the press of (which, button). A second press of the same key replaces the holder and keeps its age
        /// @return false when the key is new and every place is taken
        bool BeginCapture(uint32_t which, uint8_t button, IUIInputHandler& handler);

        /// @return the holder of the press, or null
        [[nodiscard]] IUIInputHandler* FindCapture(uint32_t which, uint8_t button) const;

        /// The holder that receives the motion of a pointer: the oldest capture of `which`
        /// @return the holder, or null when the pointer holds no press
        [[nodiscard]] IUIInputHandler* FindCaptureOfPointer(uint32_t which) const;

        void EndCapture(uint32_t which, uint8_t button);

        /// Sets the handler the pointer is over (null: over nothing)
        /// @return the previous hover of the pointer, null when none, so the caller can send the leave when it differs.
        ///         When all places are taken by other pointers the new hover is not kept and null is returned
        IUIInputHandler* SetHover(uint32_t which, IUIInputHandler* handler);

        [[nodiscard]] IUIInputHandler* GetHover(uint32_t which) const;

        /// Drops every capture and hover of the handler (it is being destroyed or unregistered)
        void Forget(const IUIInputHandler& handler);

        void Clear();

        [[nodiscard]] size_t CaptureCount() const {
            return _captures.size();
        }

    private:
        struct Capture {
            uint32_t which = 0;
            uint8_t button = 0;
            IUIInputHandler* handler = nullptr;
        };

        struct Hover {
            uint32_t which = 0;
            IUIInputHandler* handler = nullptr;
        };

        FixedVector<Capture, kMaxEntries> _captures;  // oldest first
        FixedVector<Hover, kMaxEntries> _hovers;
    };

}  // namespace BECore
