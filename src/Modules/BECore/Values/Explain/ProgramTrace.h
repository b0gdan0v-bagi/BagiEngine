#pragma once

#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <cstdint>
#include <limits>

namespace BECore {

    /// What produced a value of a trace
    CORE_ENUM(TraceStepKind, uint8_t,
              Constant,    // a constant of the program
              ContextKey,  // a field of a context object or of an element of a list (`label` is its path)
              Table,       // an entry of a table of constants (`at(...)`, `ByLevel`)
              Operation,   // an operation of the language, a fused instruction or the size of a list
              Conversion,  // `int(x)`, `float(x)`
              Call,        // an operation a game registered (`ProgramDomain`)
              List)        // a list of a context object: `image` holds its count

    /// The most nodes of a trace: a loop over a long list stops being recorded at this many (`ProgramTrace::truncated`); the value is still computed in full. The tree stays shallow enough for the
    /// recursion of its printer
    inline constexpr uint32_t kMaxTraceNodes = 512;

    /// The most nodes any text or answer made from a trace writes out: a node several parents share is written under each of them, and a hand-built program can share them again and again
    inline constexpr uint32_t kMaxFormattedLines = kMaxTraceNodes * 4;

    /// The `label` of the node of a fused `MulAddSlot`: its children are the field, the factor and the addend, in that order
    inline constexpr StringView kMulAddLabel = "MulAdd";

    inline constexpr uint32_t kNoTraceNode = std::numeric_limits<uint32_t>::max();

    /**
     * @brief One value of a run and where it came from
     *
     * The children are the nodes that wrote the registers the instruction read, in the order of its operands (`ProgramTrace::Children`). A fused instruction is one node whose children are
     * the field reads and constants it holds. A constant is the node of the `LoadConst` that wrote it, so a constant read twice is one node with two parents.
     */
    struct TraceNode {
        TraceStepKind kind = TraceStepKind::Constant;
        /// What `image` holds (an `Int32` count for a `List`)
        ProgramType type = ProgramType::Int32;
        /// The instruction that made the node: `Invalid` for a field read inside a fused instruction, which has none
        Opcode opcode = Opcode::Invalid;
        /// The operation of the language it is, when it is one (`Add`, `Lerp`); empty for a fused instruction that is two (`MulAddSlot`), a conversion, a call, a table or a list
        Optional<ProgramOperation> operation;
        /// The source as a person reads it: the path of a field (`caster.attack`, or `level 'caster' +24` when no field starts there and `level 3 +24` when the declaration has no such level), the
        /// name of the operation or of the registered function, `table row 1 of 3`, the path of a list
        String label;
        RegisterImage image;
        /// The enum or the registered type of an `Enum` or a `Domain` value (the register holds only the index or the bytes); the type of the list for a `List`; null when none is known
        const TypeDesc* described = nullptr;
        uint32_t firstChild = 0;
        uint32_t childCount = 0;
    };

    /**
     * @brief The tree of how one run computed its value
     *
     * Built from the instructions that ran, not from the text of the formula: a fused instruction is one node and a subtree of constants is the constant the compiler folded it to. Every node's
     * children come before it in `nodes`, so a walk from the front sees an operand before its use; `root` is the node of the result.
     */
    struct ProgramTrace {
        Vector<TraceNode> nodes;
        Vector<uint32_t> childIndexes;
        /// The node of the result register, `kNoTraceNode` when no node was made (a missing level, or a trace cut before the result was written)
        uint32_t root = kNoTraceNode;
        ProgramType resultType = ProgramType::Int32;
        /// The program's value: what `Run` writes, which the root holds too (a cut trace sets the root's value to it, the nodes below are what the cut left)
        RegisterImage result;
        /// The enum or registered type of the result, as `TraceNode::described`
        const TypeDesc* resultDescribed = nullptr;
        /// The instructions beyond `kMaxTraceNodes` nodes were run but not recorded
        bool truncated = false;
        /// The first level the program reads that the context has no object at: nothing ran, the trace is empty
        Optional<LevelIndex> missingLevel;

        [[nodiscard]] Span<const uint32_t> Children(const TraceNode& node) const {
            return Span<const uint32_t>(childIndexes.data() + node.firstChild, node.childCount);
        }
    };

    /**
     * @brief Runs @p program over @p context and records how its value came to be
     *
     * Always through the interpreter (`RunTraced`), whether a generated function is bound or not; the value is the one `Evaluate` gives. @p declaration names the fields the
     * instructions read (`ResolveField`): a level it does not have is a node that says `level 3`, never an assertion. A context that lacks an object at a level the program reads gives
     * an empty trace and `missingLevel`. Allocates; it runs when a person asks, never in a frame.
     */
    [[nodiscard]] ProgramTrace ExplainProgram(const Program& program, const Context& context, const ContextDecl& declaration);

    /// @p image as a person reads a value of @p type: `true`, `12`, `(1, 2, 3)`, an enum by its name from @p described (`#2` when none is known), a string in quotes, a registered type as `<Name N
    /// bytes: hex>`
    [[nodiscard]] String FormatImage(ProgramType type, const RegisterImage& image, const TypeDesc* described = nullptr);

    /// @p count bytes as two lowercase hex digits each: how a type nobody described is shown
    [[nodiscard]] String FormatHexBytes(const void* bytes, size_t count);

    /// The value of a node as text: `FormatImage`, and `N elements` for a list
    [[nodiscard]] String FormatNodeValue(const TraceNode& node);

    /// The tree one node per line, the root first and each node's children indented under it: `label = value`; a cut trace ends with a line that says so
    [[nodiscard]] String FormatTrace(const ProgramTrace& trace);

}  // namespace BECore
