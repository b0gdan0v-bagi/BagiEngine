#pragma once

#include <BECore/EffectsToys.h>
#include <BECore/ProgramDomainTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Effects/EffectCommands.h>
#include <BECore/Values/Effects/EffectScript.h>
#include <cstdint>

namespace BECore::EffectsData {

    // The board game of the entry in miniature: a cell with a timer, a flag, an enum and a mask, a board with a mask, and the one command a host handles. The .gen.hpp of this header is
    // included in EffectsTriggerTest.cpp only; a type is found by its simple name, so the names are unique among all test headers.

    CORE_ENUM(TriggerAction, uint8_t, Open, Mask)
    CORE_ENUM(TriggerTone, uint8_t, Calm, Loud, Sharp)

    struct TriggerCell {
        BE_CLASS(TriggerCell)

        BE_REFLECT_FIELD int32_t index = 0;
        BE_REFLECT_FIELD int32_t timer = 0;
        BE_REFLECT_FIELD bool open = false;
        BE_REFLECT_FIELD TriggerTone tone = TriggerTone::Calm;
        BE_REFLECT_FIELD ProgramDomainData::DomainBits256 mask;
    };

    struct TriggerBoard {
        BE_CLASS(TriggerBoard)

        BE_REFLECT_FIELD ProgramDomainData::DomainBits256 mask;
    };

    struct TriggerDocument {
        BE_CLASS(TriggerDocument)

        BE_REFLECT_FIELD ContextDecl context;
        BE_REFLECT_FIELD EffectScript onTick;
    };

    inline constexpr StringView kTriggerContext = R"(<context cell="TriggerCell" board="TriggerBoard"/>)";

    /// The trigger of the entry: count a timer down and open at zero, or take the board's mask into the cell
    inline constexpr StringView kTriggerScript = "if (cell.timer > 0) { cell.timer -= 1; if (cell.timer == 0) { cell.open = !cell.open; emit Action(Open, cell.index); } } "
                                                 "else if (board.mask != cell.mask) { cell.mask = and_mask(cell.mask, board.mask); emit Action(Mask, cell.index); }";

    /// The command a host handles, registered once per process (the registry never forgets) after the toy mask it does not take
    [[nodiscard]] inline CommandId RegisterTriggerAction() {
        static const CommandId id = [] {
            (void)ProgramDomainData::RegisterDomainToys();
            return RegisteredCommand(EffectCommands::Register<TriggerAction, int32_t>("Action"));
        }();
        return id;
    }

}  // namespace BECore::EffectsData
