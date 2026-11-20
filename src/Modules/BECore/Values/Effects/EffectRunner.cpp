#include "EffectRunner.h"

#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>

namespace BECore {

    namespace {

        constexpr size_t kLargestEnumField = sizeof(uint64_t);

        /// The index in the low @p bytes of @p out, the order the field's own type has in memory
        void EncodeEnumIndex(uint32_t index, uint8_t bytes, uint8_t* out) {
            switch (bytes) {
                case sizeof(uint8_t): {
                    const auto value = static_cast<uint8_t>(index);
                    std::memcpy(out, &value, sizeof(value));
                    return;
                }
                case sizeof(uint16_t): {
                    const auto value = static_cast<uint16_t>(index);
                    std::memcpy(out, &value, sizeof(value));
                    return;
                }
                case sizeof(uint32_t): {
                    std::memcpy(out, &index, sizeof(index));
                    return;
                }
                default: {
                    const uint64_t value = index;
                    std::memcpy(out, &value, sizeof(value));
                    return;
                }
            }
        }

        /// @p image takes the value of the operand: the bytes of a constant, of a field (an enum zero-extends to its index, a registered type is zero-padded) or the result of a program
        void Evaluate(const EffectOperand& operand, const Context& context, RegisterImage& image) {
            image = RegisterImage{};
            switch (operand.kind) {
                case CompiledKind::Constant:
                    image = operand.constant;
                    return;
                case CompiledKind::Slot: {
                    const void* object = context.Object(operand.slot.level);
                    ASSERT(object != nullptr, "RunEffects: the context has no object at the level the script reads from");
                    if (object == nullptr) {
                        return;
                    }
                    const auto* field = static_cast<const uint8_t*>(object) + operand.slot.offset;
                    if (operand.type == ProgramType::Enum) {
                        const ProgramEnum index{Kernel::LoadEnumIndex(field, operand.bytes)};
                        std::memcpy(image.bytes.data(), &index, sizeof(index));
                    } else {
                        std::memcpy(image.bytes.data(), field, std::min<size_t>(operand.bytes, kProgramRegisterBytes));
                    }
                    return;
                }
                case CompiledKind::Program: {
                    const NativeProgramFn native = ProgramPool::Native(operand.program);
                    if (native == nullptr) {
                        Run(ProgramPool::Get(operand.program), context, image.bytes.data());
                    } else {
                        native(context, image.bytes.data());
                    }
                    return;
                }
            }
        }

        /// Writes @p image into the field of @p step; true when a byte changed
        bool Store(const EffectStep& step, const RegisterImage& image, const Context& context, void* object) {
            auto* field = static_cast<uint8_t*>(object) + step.target.offset;
            Array<uint8_t, kLargestEnumField> encoded{};
            const uint8_t* source = image.bytes.data();
            if (step.type == ProgramType::Enum) {
                ProgramEnum index;
                std::memcpy(&index, image.bytes.data(), sizeof(index));
                EncodeEnumIndex(index.index, step.bytes, encoded.data());
                source = encoded.data();
            }
            if (std::memcmp(field, source, step.bytes) == 0) {
                return false;
            }
            std::memcpy(field, source, step.bytes);
            context.BumpVersion(step.target.level);
            return true;
        }

    }  // namespace

    EffectResult RunEffects(const EffectProgram& script, const Context& context, EmitBuffer& emitted) {
        EffectResult result;
        const Span<const EffectStep> steps = script.Steps();
        RegisterImage image;
        size_t pc = 0;
        while (pc < steps.size()) {
            const EffectStep& step = steps[pc];
            switch (step.op) {
                case EffectOp::Store: {
                    void* object = context.MutableObject(step.target.level);
                    ASSERT(object != nullptr, "RunEffects: the script stores to a level the host did not make writable (Context::SetMutable)");
                    if (object == nullptr) {
                        ++result.refused;
                    } else {
                        Evaluate(step.operands[0], context, image);
                        ++result.stores;
                        result.changed += Store(step, image, context, object) ? 1U : 0U;
                    }
                    ++pc;
                    break;
                }
                case EffectOp::JumpIfNot:
                    Evaluate(step.operands[0], context, image);
                    pc = image.bytes[0] != 0 ? pc + 1 : step.jump;
                    break;
                case EffectOp::Jump:
                    pc = step.jump;
                    break;
                case EffectOp::Emit: {
                    EmittedCommand command;
                    command.id = step.command;
                    command.count = step.count;
                    for (size_t i = 0; i < step.count; ++i) {
                        Evaluate(step.operands[i], context, command.arguments[i]);
                    }
                    if (emitted.Push(command)) {
                        ++result.emitted;
                    } else {
                        ++result.dropped;
                    }
                    ++pc;
                    break;
                }
            }
        }
        return result;
    }

}  // namespace BECore
