#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief The primary selections of the scene editor in the order they were made, for stepping back and forward
     *
     * Entries are runtime ids of nodes (`SceneNode::GetRuntimeId()`; 0 is "nothing selected"), so a dead node is never mistaken for a live one. No ImGui and no
     * scene: the caller says which ids are alive, and clears the history when the scene is replaced. Holds the last kCapacity selections and never allocates.
     */
    class SelectionHistory {
    public:
        static constexpr size_t kCapacity = 64;

        /**
         * @brief Remembers that @p selected is the selection now, and forgets the entries a step back had left ahead
         *
         * Nothing (0) and the entry the history stands on change nothing, so the selection a step has just made is not recorded again and the caller may tell
         * the selection every frame.
         */
        void Record(uint64_t selected);

        /**
         * @brief Steps to the selection before @p selected, dropping the dead entries it passes over
         *
         * A @p selected that was not recorded yet is recorded first. With nothing selected (0) the answer is the entry the history stands on, the last selection.
         *
         * @return The id to select, or 0 when there is none
         */
        [[nodiscard]] uint64_t Back(uint64_t selected, FunctionRef<bool(uint64_t)> isAlive);

        /// Like Back, towards the newer entries: the one a Back came from. A new selection leaves nothing ahead
        [[nodiscard]] uint64_t Forward(uint64_t selected, FunctionRef<bool(uint64_t)> isAlive);

        void Clear();

    private:
        void Erase(size_t index);

        Array<uint64_t, kCapacity> _entries{};  // oldest first
        size_t _count = 0;
        size_t _passed = 0;  // the entries up to and including the one the history stands on; the rest are ahead of it
    };

}  // namespace BECore
