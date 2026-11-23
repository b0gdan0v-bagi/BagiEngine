#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Values/Effects/EffectProgram.h>
#include <expected>

namespace BECore {

    /**
     * @brief Assembles an `EffectProgram`: the compiler of script text and the tests both use it
     *
     * A step is added with its own call and gets the index of the next one. A forward jump is added unpatched and `PatchJump` points it at a step (the end when the script is
     * finished) once that is known, which is what `if` / `else` need. `Finish` checks the whole script.
     *
     * The steps are on the thread's scratch arena (`Memory/CLAUDE.md`, "Scratch arena and scopes"): a builder belongs to the `ScratchScope` that is alive when it is made and must not
     * outlive it, be a member of a longer-lived object or cross threads. The `EffectProgram` that `Finish` returns copies them, so it may outlive the builder. A builder made with no
     * scope open (a test) only keeps its memory until its thread ends.
     */
    class EffectProgramBuilder {
    public:
        /// Makes room for @p steps, so that adding them takes no new block of the arena (it gives nothing back on growth)
        void Reserve(size_t steps) {
            _steps.reserve(steps);
        }

        /// Writes @p value, a value of @p type, into the field at @p target. @p bytes as in `EffectFieldBytes`
        uint32_t AddStore(SlotRef target, ProgramType type, uint8_t bytes, const EffectOperand& value);

        /// When @p condition (a `Bool`) is false, go to the step `PatchJump` names
        uint32_t AddJumpIfNot(const EffectOperand& condition);

        uint32_t AddJump();

        /// Pushes command @p command with @p arguments, as many as its signature has
        uint32_t AddEmit(CommandId command, Span<const EffectOperand> arguments);

        /// The index the next step gets: the target of a jump that goes to the end of what is added so far
        [[nodiscard]] uint32_t NextStep() const {
            return static_cast<uint32_t>(_steps.size());
        }

        /// Points the jump at @p step to @p target; `Finish` refuses one that is not forward
        void PatchJump(uint32_t step, uint32_t target);

        /// The script, or the first step that is not valid; @p source is the text it came from
        [[nodiscard]] std::expected<IntrusivePtr<const EffectProgram>, ProgramError> Finish(String source = {}) const;

    private:
        ArenaVector<EffectStep> _steps;
    };

}  // namespace BECore
