#include "EffectCommands.h"

#include <BECore/Values/Effects/ScriptIdentifier.h>

namespace BECore {

    namespace {

        class Registry {
        public:
            static Registry& Instance() {
                static Registry registry;
                return registry;
            }

            Mutex& Lock() {
                return _mutex;
            }

            Vector<EffectCommand> commands;

        private:
            Registry() = default;

            Mutex _mutex{BE_LOCK_SITE("EffectCommands")};
        };

        [[nodiscard]] Optional<CommandId> FindLocked(const Registry& registry, StringView name) {
            for (size_t i = 0; i < registry.commands.size(); ++i) {
                if (StringView(registry.commands[i].name) == name) {
                    return static_cast<CommandId>(i);
                }
            }
            return std::nullopt;
        }

    }  // namespace

    std::expected<CommandId, String> EffectCommands::RegisterImpl(StringView name, Span<const DomainValueType> parameters) {
        if (!IsScriptIdentifier(name)) {
            return std::unexpected(Format("command name '{}' is not an identifier (letters, digits and '_', no digit first)", name));
        }
        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        if (FindLocked(registry, name)) {
            return std::unexpected(Format("command '{}' is registered already: a command has one signature", name));
        }
        EffectCommand command;
        command.name = String(name);
        command.parameterCount = static_cast<uint8_t>(parameters.size());
        for (size_t i = 0; i < parameters.size(); ++i) {
            command.parameters[i] = parameters[i];
        }
        registry.commands.push_back(std::move(command));
        return static_cast<CommandId>(registry.commands.size() - 1);
    }

    Optional<CommandId> EffectCommands::Find(StringView name) {
        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        return FindLocked(registry, name);
    }

    EffectCommand EffectCommands::Command(CommandId id) {
        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        ASSERT(id < registry.commands.size(), "EffectCommands: no command has this id");
        return id < registry.commands.size() ? registry.commands[id] : EffectCommand{};
    }

    size_t EffectCommands::Count() {
        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        return registry.commands.size();
    }

    Vector<String> EffectCommands::Names() {
        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        Vector<String> names;
        names.reserve(registry.commands.size());
        for (const EffectCommand& command : registry.commands) {
            names.push_back(command.name);
        }
        return names;
    }

}  // namespace BECore
