#pragma once

#include <BECore/ProgramDomainTypes.h>
#include <BECore/Values/Effects/EffectCommands.h>
#include <cstdio>
#include <cstdlib>

namespace BECore::EffectsData {

    // The commands the effects tests emit, registered once per process (the registry never forgets). Their parameters are engine types, an enum and the toy mask of
    // `ProgramDomainTypes.h`; the names are unique among all test headers.

    struct EffectToys {
        CommandId ping = kNoCommand;
        CommandId count = kNoCommand;
        CommandId face = kNoCommand;
        CommandId mask = kNoCommand;
        CommandId mixed = kNoCommand;
    };

    [[nodiscard]] inline CommandId RegisteredCommand(const std::expected<CommandId, String>& result) {
        if (!result.has_value()) {
            std::fprintf(stderr, "the toy command registration was refused: %s\n", result.error().c_str());
            std::abort();
        }
        return *result;
    }

    [[nodiscard]] inline const EffectToys& RegisterEffectToys() {
        static const EffectToys toys = [] {
            // The mask must be registered before a command takes it
            (void)ProgramDomainData::RegisterDomainToys();
            using ProgramDomainData::DomainBits256;
            using ProgramDomainData::DomainDirection;
            EffectToys made;
            made.ping = RegisteredCommand(EffectCommands::Register<>("FxPing"));
            made.count = RegisteredCommand(EffectCommands::Register<int32_t>("FxCount"));
            made.face = RegisteredCommand(EffectCommands::Register<DomainDirection>("FxFace"));
            made.mask = RegisteredCommand(EffectCommands::Register<DomainBits256>("FxMask"));
            made.mixed = RegisteredCommand(EffectCommands::Register<int32_t, DomainDirection, DomainBits256>("FxMixed"));
            return made;
        }();
        return toys;
    }

}  // namespace BECore::EffectsData
