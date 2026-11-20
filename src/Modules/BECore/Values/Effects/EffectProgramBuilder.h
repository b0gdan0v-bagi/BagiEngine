#pragma once

#include <BECore/Values/Effects/EffectProgram.h>
#include <expected>

namespace BECore {

    /**
     * @brief Assembles an `EffectProgram`: the compiler of script text and the tests both use it
     *
     * A step is added with its own call and gets the index of the next one. A forward jump is added unpatched and `PatchJump` points it at a step (the end when the script is
     * finished) once that is known, which is what `if` / `else` need. `Finish` checks the whole script.
     */
    class EffectProgramBuilder {
    public:
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
        Vector<EffectStep> _steps;
    };

}  // namespace BECore
