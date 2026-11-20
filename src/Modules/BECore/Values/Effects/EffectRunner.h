#pragma once

#include <BECore/Values/Context.h>
#include <BECore/Values/Effects/EffectCommands.h>
#include <BECore/Values/Effects/EffectProgram.h>
#include <cstdint>

namespace BECore {

    /// What one run did
    struct EffectResult {
        /// Stores that reached a writable level
        uint32_t stores = 0;
        /// Of them, those that changed a byte (and bumped the level's counter)
        uint32_t changed = 0;
        /// Stores to a level that is not `Context::SetMutable`: skipped
        uint32_t refused = 0;
        /// Commands this run pushed into the buffer
        uint32_t emitted = 0;
        /// Commands the full buffer refused
        uint32_t dropped = 0;
    };

    /**
     * @brief Runs @p script over the objects of @p context: stores into its writable levels and pushes commands to @p emitted
     *
     * Allocates nothing and always ends (the jumps go forward). An operand is evaluated into one register image on the stack, so a store whose operand reads the field it writes
     * is well defined and the next step sees the new value. A store writes the image back only when the bytes differ, and then bumps the level's counter once. A store to a level
     * that is not writable is skipped and counted in `refused` (and is an `ASSERT`: the host forgot `SetMutable`). Every level the script reads must have an object (`ASSERT`, as
     * `Run`). Nothing is cleared: @p emitted keeps what it holds, `EmitBuffer::Clear` is the host's.
     */
    EffectResult RunEffects(const EffectProgram& script, const Context& context, EmitBuffer& emitted);

}  // namespace BECore
