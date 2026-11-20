#include "ProgramCompiler.h"

#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/ProgramBuilder.h>

namespace BECore {

    namespace {

        using Failure = std::unexpected<ProgramError>;

        /// How much of a long text the caret line shows: this many bytes before the column and after it
        constexpr size_t kExcerptBefore = 32;
        constexpr size_t kExcerptAfter = 28;
        constexpr StringView kExcerptGap = "...";
        constexpr StringView kExcerptIndent = "  ";
        constexpr char kUnprintable = '?';

        /// The size of the field a `LoadSlot` of @p type reads: an enum's width and a registered type's size are not in the opcode, the other types' are
        uint8_t SlotBytes(const ExprType& type) {
            if (type.program == ProgramType::Enum) {
                return static_cast<uint8_t>(type.enumeration->enumBytes);
            }
            if (type.program == ProgramType::Domain) {
                return static_cast<uint8_t>(ProgramDomain::Type(type.domain).size);
            }
            return 0;
        }

        /// A call that reads the context is not a constant when its arguments are: folding runs it over an empty context, and the board it reads is not there until the formula runs
        bool ReadsTheContext(const TypedNode& node) {
            return node.opcode == Opcode::CallDomain && ProgramDomain::Operation(node.immediate).takesContext;
        }

        /// The images of the entries of a table node, which the lowering has checked are constants
        Vector<RegisterImage> EntryImages(const TypedNode& node, const Vector<RegisterImage>& images) {
            Vector<RegisterImage> run;
            run.reserve(node.entries.size());
            for (const TypedNodeId entry : node.entries) {
                run.push_back(images[entry]);
            }
            return run;
        }

        /// Appends the entries of a table node as a run of constants and emits its `LoadTable` of the index in @p indexRegister into @p dst
        void EmitTable(ProgramBuilder& builder, const TypedNode& node, const Vector<RegisterImage>& images, uint8_t dst, uint8_t indexRegister) {
            const Vector<RegisterImage> run = EntryImages(node, images);
            const uint32_t first = builder.AddConstantRun(Span<const RegisterImage>(run.data(), run.size()));
            builder.EmitLoadTable(dst, indexRegister, first, static_cast<uint32_t>(run.size()));
        }

        /// The register image of what an operation gives: the same kernels as `Run`, so a folded value is what the program would have computed; a table is folded by running its own
        /// instruction over its own run of constants
        std::expected<RegisterImage, ProgramError> Fold(const TypedNode& node, const Vector<RegisterImage>& images) {
            ProgramBuilder builder;
            Array<uint8_t, kMaxTypedOperands> regs{};
            for (uint8_t i = 0; i < node.operandCount; ++i) {
                const auto reg = builder.AcquireRegister();
                if (!reg.has_value()) {
                    return std::unexpected(reg.error());
                }
                regs[i] = *reg;
                builder.Emit(Opcode::LoadConst, *reg, 0, 0, 0, builder.AddConstant(images[node.operands[i]]));
            }
            if (node.opcode == Opcode::LoadTable) {
                EmitTable(builder, node, images, regs[0], regs[0]);
            } else {
                builder.Emit(node.opcode, regs[0], regs[0], regs[1], regs[2], node.immediate);
            }
            const auto program = builder.Finish(node.type.program, regs[0]);
            if (!program.has_value()) {
                return std::unexpected(program.error());
            }
            RegisterImage image{};
            Run(*program, Context{}, image.bytes.data());
            return image;
        }

        /// Turns a checked formula into the compiled result; one instance per formula. The operands of a node are in consecutive registers from the first one (the node's own `dst`):
        /// an instruction names three of them (`a`, `b`, `c`), and `Pack` of four reads them from `a` up.
        class Lowering {
        public:
            Lowering(const TypedExpr& expr, StringView source, bool fuse) : _expr(expr), _source(source), _fuse(fuse) {}

            std::expected<CompiledExpression, ProgramError> Compile() {
                if (const auto folded = FoldConstants(); !folded.has_value()) {
                    return std::unexpected(folded.error());
                }
                const TypedNode& root = _expr[_expr.root];
                CompiledExpression result;
                result.type = root.type.program;
                result.domain = root.type.domain;
                if (_isConstant[_expr.root]) {
                    result.kind = CompiledKind::Constant;
                    result.constant = _images[_expr.root];
                    return result;
                }
                if (root.kind == TypedNodeKind::Slot) {
                    result.kind = CompiledKind::Slot;
                    result.slot = root.slot;
                    result.path = root.path;
                    return result;
                }
                const auto resultRegister = _builder.AcquireRegister();
                if (!resultRegister.has_value()) {
                    return std::unexpected(resultRegister.error());
                }
                if (const auto lowered = Lower(_expr.root, *resultRegister); !lowered.has_value()) {
                    return std::unexpected(lowered.error());
                }
                if (_fuse) {
                    _builder.Fuse(*resultRegister);
                }
                auto program = _builder.Finish(root.type.program, *resultRegister, PoolString::Intern(_source));
                if (!program.has_value()) {
                    return std::unexpected(program.error());
                }
                const Optional<ProgramId> id = ProgramPool::Intern(std::move(*program));
                if (!id.has_value()) {
                    return Failure(ProgramError{Format("the pool of programs is full ({} programs)", ProgramPool::kMaxPrograms), 0});
                }
                result.kind = CompiledKind::Program;
                result.program = *id;
                return result;
            }

        private:
            /// The nodes come after their operands, so one pass in order sees each operand's answer
            std::expected<void, ProgramError> FoldConstants() {
                const size_t count = _expr.nodes.size();
                _isConstant.assign(count, false);
                _images.assign(count, RegisterImage{});
                for (size_t i = 0; i < count; ++i) {
                    const TypedNode& node = _expr.nodes[i];
                    if (node.kind == TypedNodeKind::Constant) {
                        _isConstant[i] = true;
                        _images[i] = node.constant;
                    } else if (node.kind == TypedNodeKind::Operation) {
                        if (const auto entries = RequireConstantEntries(node); !entries.has_value()) {
                            return entries;
                        }
                        if (ReadsTheContext(node)) {
                            continue;
                        }
                        const bool allConstant = std::all_of(node.operands.begin(), node.operands.begin() + node.operandCount, [this](TypedNodeId operand) { return _isConstant[operand]; });
                        if (!allConstant) {
                            continue;
                        }
                        const auto image = Fold(node, _images);
                        if (!image.has_value()) {
                            return std::unexpected(ProgramError{image.error().message, node.column});
                        }
                        _isConstant[i] = true;
                        _images[i] = *image;
                    }
                }
                return {};
            }

            /// A table is a run of constants of the program: an entry that did not fold to one (a path, say) is refused at its own column
            std::expected<void, ProgramError> RequireConstantEntries(const TypedNode& node) const {
                for (const TypedNodeId entry : node.entries) {
                    if (!_isConstant[entry]) {
                        return Failure(ProgramError{"a table entry must be a constant: a number, a literal or an operation on them, not a value read from the context", _expr[entry].column});
                    }
                }
                return {};
            }

            /// Writes the code that leaves the value of @p id in @p dst, which is the top register; the operands go into @p dst and the registers above it
            std::expected<void, ProgramError> Lower(TypedNodeId id, uint8_t dst) {
                const TypedNode& node = _expr[id];
                if (_isConstant[id]) {
                    _builder.Emit(Opcode::LoadConst, dst, 0, 0, 0, _builder.AddConstant(_images[id]));
                    return {};
                }
                if (node.kind == TypedNodeKind::Slot) {
                    _builder.EmitLoadSlot(dst, node.type.program, node.slot, SlotBytes(node.type));
                    return {};
                }
                Array<uint8_t, kMaxTypedOperands> regs{};
                uint8_t acquired = 0;
                for (uint8_t i = 0; i < node.operandCount; ++i) {
                    if (i == 0) {
                        regs[i] = dst;
                    } else {
                        const auto reg = _builder.AcquireRegister();
                        if (!reg.has_value()) {
                            return std::unexpected(ProgramError{reg.error().message, node.column});
                        }
                        regs[i] = *reg;
                        ++acquired;
                        // `dst` is the top register and an operand gives back what it took, so each operand lands right above the one before it
                        ASSERT(regs[i] == regs[i - 1] + 1, "Lower: the operands of a node are in consecutive registers");
                    }
                    if (const auto lowered = Lower(node.operands[i], regs[i]); !lowered.has_value()) {
                        return lowered;
                    }
                }
                if (node.opcode == Opcode::LoadTable) {
                    EmitTable(_builder, node, _images, dst, regs[0]);
                } else {
                    _builder.Emit(node.opcode, dst, regs[0], regs[1], regs[2], node.immediate);
                }
                ReleaseAbove(dst, acquired);
                return {};
            }

            /// Gives back the @p count registers above @p dst, the last one first
            void ReleaseAbove(uint8_t dst, uint8_t count) {
                for (uint8_t i = count; i > 0; --i) {
                    _builder.ReleaseRegister(static_cast<uint8_t>(dst + i));
                }
            }

            const TypedExpr& _expr;
            StringView _source;
            bool _fuse;
            ProgramBuilder _builder;
            Vector<bool> _isConstant;
            Vector<RegisterImage> _images;
        };

        char Printable(char c) {
            return c >= ' ' && c <= '~' ? c : kUnprintable;
        }

    }  // namespace

    std::expected<CompiledExpression, ProgramError> CompileExpression(StringView text, const ContextDecl& declaration, const TypeDesc& expected, const CompileOptions& options) {
        const auto wanted = CheckExpectedType(expected);
        if (!wanted.has_value()) {
            return std::unexpected(wanted.error());
        }
        const auto ast = ParseExpression(text);
        if (!ast.has_value()) {
            return std::unexpected(ast.error());
        }
        const auto typed = CheckExpression(*ast, declaration, expected, options);
        if (!typed.has_value()) {
            return std::unexpected(typed.error());
        }
        Lowering lowering(*typed, text, options.fuse);
        return lowering.Compile();
    }

    String FormatProgramError(StringView text, const ProgramError& error) {
        if (error.column == 0) {
            return error.message;
        }
        const size_t position = std::min<size_t>(error.column - 1, text.size());
        const size_t from = position > kExcerptBefore ? position - kExcerptBefore : 0;
        const size_t to = std::min(text.size(), position + kExcerptAfter);
        const bool cutBefore = from > 0;
        const bool cutAfter = to < text.size();

        String excerpt;
        if (cutBefore) {
            excerpt.append(kExcerptGap.data(), kExcerptGap.size());
        }
        for (size_t i = from; i < to; ++i) {
            excerpt.push_back(Printable(text[i]));
        }
        if (cutAfter) {
            excerpt.append(kExcerptGap.data(), kExcerptGap.size());
        }
        const size_t caretColumn = (cutBefore ? kExcerptGap.size() : 0) + (position - from);
        return Format("{} (column {})\n{}{}\n{}{}^", error.message, error.column, kExcerptIndent, excerpt, kExcerptIndent, String(caretColumn, ' '));
    }

}  // namespace BECore
