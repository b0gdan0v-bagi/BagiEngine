#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Effects/EffectCommands.h>
#include <BECore/Values/Effects/EffectProgram.h>
#include <BECore/Values/Effects/EffectRunner.h>

namespace BECore {

    /**
     * @brief A compiled script as a field of a content type: the shared program and nothing else, so a copy is a reference count
     *
     * The default handle is an empty script, which runs nothing. The text is the program's own (`Source()`), so a saved field writes what was loaded. Two handles are equal when
     * they hold the same program or two programs that came from the same text and have the same steps (a document loaded twice is equal to itself).
     */
    class EffectScript {
    public:
        EffectScript() = default;

        explicit EffectScript(IntrusivePtr<const EffectProgram> program) : _program(std::move(program)) {}

        /// True for a handle that holds no program or a program with no steps
        [[nodiscard]] bool Empty() const {
            return !_program || _program->Steps().empty();
        }

        [[nodiscard]] const IntrusivePtr<const EffectProgram>& Program() const {
            return _program;
        }

        /// The text the script was compiled from; empty for an empty handle
        [[nodiscard]] StringView Source() const {
            return _program ? StringView(_program->Source()) : StringView();
        }

        [[nodiscard]] LevelMask Reads() const {
            return _program ? _program->Reads() : 0;
        }

        [[nodiscard]] LevelMask Writes() const {
            return _program ? _program->Writes() : 0;
        }

        /// `RunEffects` of the program (nothing for an empty handle)
        EffectResult Run(const Context& context, EmitBuffer& emitted) const {
            return _program ? RunEffects(*_program, context, emitted) : EffectResult{};
        }

        [[nodiscard]] bool operator==(const EffectScript& other) const {
            if (_program == other._program) {
                return true;
            }
            return _program && other._program && _program->Equivalent(*other._program);
        }

        /// The editor has no property to show in a compiled script: its text is the field's own form, not a property
        template <typename Visitor>
        bool VisitProperties(Visitor& /*visitor*/) {
            return false;
        }

    private:
        IntrusivePtr<const EffectProgram> _program;
    };

}  // namespace BECore

// The accessor is part of the type: a generated .gen.hpp that names an EffectScript field sees it by including this header
#include <BECore/Values/Effects/EffectScriptAccessor.h>
