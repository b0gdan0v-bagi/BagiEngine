#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Instruction.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace BECore {

    /// The place of a command in the registry: its registration order, never reused, so a script that names it keeps its meaning
    using CommandId = uint32_t;

    inline constexpr CommandId kNoCommand = std::numeric_limits<CommandId>::max();

    /// A command carries its arguments in the images of an emitted record
    inline constexpr size_t kMaxCommandArguments = 3;

    /// A registered command: the word `emit` names and the types of its arguments. A command has one signature and no result: it is a message to the host
    struct EffectCommand {
        String name;
        Array<DomainValueType, kMaxCommandArguments> parameters{};
        uint8_t parameterCount = 0;
    };

    /**
     * @brief The registry of the commands a script may emit, filled by the game before its content loads
     *
     * The model of `ProgramDomain`: process-wide, append-only, the order of registration is the id, so the executable and every tool of a game register the same commands in the
     * same order. A parameter is a program type, an enum or a type the game registered with `ProgramDomain` (found by `ValueTypeOf`). A refused registration changes nothing.
     */
    class EffectCommands {
    public:
        /// Registers @p name with the parameters @p Args. Refused: a name that is no identifier or is taken (no overloads), more than `kMaxCommandArguments` parameters, a parameter type nobody
        /// registered
        template <typename... Args>
        static std::expected<CommandId, String> Register(StringView name) {
            if constexpr (sizeof...(Args) > kMaxCommandArguments) {
                return std::unexpected(Format("command '{}' takes {} parameters, the most is {}", name, sizeof...(Args), kMaxCommandArguments));
            } else {
                Array<DomainValueType, kMaxCommandArguments> parameters{};
                size_t index = 0;
                String refusal;
                const auto take = [&](const Optional<DomainValueType>& type) {
                    if (!type) {
                        refusal = Format("command '{}': parameter {} has a type that is not registered", name, index + 1);
                        return false;
                    }
                    parameters[index++] = *type;
                    return true;
                };
                if (!(take(ProgramDomain::ValueTypeOf<std::remove_cvref_t<Args>>()) && ...)) {
                    return std::unexpected(std::move(refusal));
                }
                return RegisterImpl(name, Span<const DomainValueType>(parameters.data(), sizeof...(Args)));
            }
        }

        [[nodiscard]] static Optional<CommandId> Find(StringView name);

        /// A copy, since the registry may grow while the caller reads: an id nobody registered is an `ASSERT` and an empty command
        [[nodiscard]] static EffectCommand Command(CommandId id);

        [[nodiscard]] static size_t Count();

        /// Every name in registration order, for a message that lists what a script may emit
        [[nodiscard]] static Vector<String> Names();

    private:
        static std::expected<CommandId, String> RegisterImpl(StringView name, Span<const DomainValueType> parameters);
    };

    /// One command a run pushed: the id and the images of its arguments, as many as the command's signature has
    struct EmittedCommand {
        CommandId id = kNoCommand;
        uint8_t count = 0;
        Array<RegisterImage, kMaxCommandArguments> arguments{};

        /// Argument @p index as its C++ type: an enum by its index, a registered type by its bytes. The host asks for the type the signature registered
        template <typename T>
        [[nodiscard]] T Argument(size_t index) const {
            ASSERT(index < count, "EmittedCommand: no such argument");
            return Detail::LoadOperand<T>(arguments[index < kMaxCommandArguments ? index : 0]);
        }
    };

    /**
     * @brief The commands a run emitted, in the storage of the host: it never grows
     *
     * A command that finds the buffer full is dropped and counted, the newest and not the oldest, so what is in the buffer keeps its order and the host learns how many it lost.
     */
    class EmitBuffer {
    public:
        EmitBuffer() = default;

        explicit EmitBuffer(Span<EmittedCommand> storage) : _storage(storage) {}

        /// False when the buffer is full (the command is dropped and counted)
        bool Push(const EmittedCommand& command) noexcept {
            if (_count >= _storage.size()) {
                ++_dropped;
                return false;
            }
            _storage[_count++] = command;
            return true;
        }

        [[nodiscard]] Span<const EmittedCommand> Commands() const {
            return Span<const EmittedCommand>(_storage.data(), _count);
        }

        [[nodiscard]] size_t Count() const {
            return _count;
        }

        [[nodiscard]] size_t Capacity() const {
            return _storage.size();
        }

        [[nodiscard]] size_t Dropped() const {
            return _dropped;
        }

        /// Empties the buffer and the count of the dropped
        void Clear() {
            _count = 0;
            _dropped = 0;
        }

    private:
        Span<EmittedCommand> _storage;
        size_t _count = 0;
        size_t _dropped = 0;
    };

    /// An `EmitBuffer` with its own array, for a host that has no storage of its own and for tests; it cannot be copied, since the buffer points into it
    template <size_t Capacity>
    class EmitBufferStorage {
    public:
        EmitBufferStorage() : _buffer(Span<EmittedCommand>(_storage.data(), _storage.size())) {}

        EmitBufferStorage(const EmitBufferStorage&) = delete;
        EmitBufferStorage& operator=(const EmitBufferStorage&) = delete;

        [[nodiscard]] EmitBuffer& Buffer() {
            return _buffer;
        }

    private:
        Array<EmittedCommand, Capacity> _storage{};
        EmitBuffer _buffer;
    };

}  // namespace BECore
