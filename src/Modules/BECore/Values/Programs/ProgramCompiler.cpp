#include "ProgramCompiler.h"

#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <limits>

namespace BECore {

    namespace {

        using Failure = std::unexpected<ProgramError>;

        /// How much of a long text the caret line shows: this many bytes before the column and after it
        constexpr size_t kExcerptBefore = 32;
        constexpr size_t kExcerptAfter = 28;
        constexpr StringView kExcerptGap = "...";
        constexpr StringView kExcerptIndent = "  ";
        constexpr char kUnprintable = '?';

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
                    _builder.EmitLoadSlot(dst, node.type.program, node.slot, EnumBytesOf(node.type));
                    return {};
                }
                if (node.kind == TypedNodeKind::Fold) {
                    return LowerFold(node, dst);
                }
                if (node.kind == TypedNodeKind::Element) {
                    return LowerElementAt(node, dst);
                }
                if (node.kind == TypedNodeKind::List) {
                    return LowerList(node, dst);
                }
                if (node.operandCount > 0 && IsReference(_expr[node.operands[0]])) {
                    return LowerElementRead(node, dst);
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

            static bool IsReference(const TypedNode& node) {
                return node.kind == TypedNodeKind::List || node.kind == TypedNodeKind::Element;
            }

            static uint8_t EnumBytesOf(const ExprType& type) {
                return type.program == ProgramType::Enum ? static_cast<uint8_t>(type.enumeration->enumBytes) : uint8_t{0};
            }

            /// The register that holds the element @p id: the register of its loop for a variable (no code), else @p dst, where `list[i]` is computed
            std::expected<uint8_t, ProgramError> ElementRegister(TypedNodeId id, uint8_t dst) {
                const TypedNode& node = _expr[id];
                if (node.operandCount == 0) {
                    ASSERT(node.loop < _loopRegisters.size(), "ElementRegister: a variable is read inside its loop");
                    return _loopRegisters[node.loop];
                }
                if (const auto lowered = LowerElementAt(node, dst); !lowered.has_value()) {
                    return std::unexpected(lowered.error());
                }
                return dst;
            }

            /// `list[i]` into @p dst: the list is computed into @p dst and the index in the register above it
            std::expected<void, ProgramError> LowerElementAt(const TypedNode& node, uint8_t dst) {
                if (const auto list = LowerList(_expr[node.operands[0]], dst); !list.has_value()) {
                    return list;
                }
                const auto indexRegister = _builder.AcquireRegister();
                if (!indexRegister.has_value()) {
                    return Failure(ProgramError{indexRegister.error().message, node.column});
                }
                ASSERT(*indexRegister == dst + 1, "LowerElementAt: the index is above the list");
                if (const auto index = Lower(node.operands[1], *indexRegister); !index.has_value()) {
                    return index;
                }
                _builder.EmitListAt(dst, dst, *indexRegister);
                _builder.ReleaseRegister(*indexRegister);
                return {};
            }

            /// A list into @p dst: the field of a level, or of the element that is its operand
            std::expected<void, ProgramError> LowerList(const TypedNode& node, uint8_t dst) {
                if (node.operandCount == 0) {
                    _builder.EmitListSlot(dst, node.slot.level, node.list);
                    return {};
                }
                const auto element = ElementRegister(node.operands[0], dst);
                if (!element.has_value()) {
                    return Failure(element.error());
                }
                _builder.EmitListElem(dst, *element, node.list);
                return {};
            }

            /// A read of a field of an element (`LoadElem`) or the count of a list (`ListSize`): the operand is a reference, not a value, so it is not lowered as an operand is
            std::expected<void, ProgramError> LowerElementRead(const TypedNode& node, uint8_t dst) {
                if (node.opcode == Opcode::ListSize) {
                    if (const auto list = LowerList(_expr[node.operands[0]], dst); !list.has_value()) {
                        return list;
                    }
                    _builder.EmitListSize(dst, dst);
                    return {};
                }
                ASSERT(IsLoadElem(node.opcode), "LowerElementRead: the operations over a reference are ListSize and LoadElem");
                const auto element = ElementRegister(node.operands[0], dst);
                if (!element.has_value()) {
                    return Failure(element.error());
                }
                _builder.EmitLoadElem(dst, node.type.program, *element, node.immediate, EnumBytesOf(node.type));
                return {};
            }

            /// The value a fold of @p kind over @p type starts from: what an empty list gives, and what no element changes (`min` starts at the largest value, `max` at the smallest)
            void EmitIdentity(uint8_t dst, FoldKind kind, ProgramType type) {
                switch (kind) {
                    case FoldKind::All:
                        _builder.EmitLoadConst(dst, true);
                        return;
                    case FoldKind::Min:
                    case FoldKind::Max: {
                        const bool smallest = kind == FoldKind::Min;
                        switch (type) {
                            case ProgramType::Int64:
                                _builder.EmitLoadConst(dst, smallest ? std::numeric_limits<int64_t>::max() : std::numeric_limits<int64_t>::lowest());
                                return;
                            case ProgramType::Float:
                                _builder.EmitLoadConst(dst, smallest ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
                                return;
                            case ProgramType::Double:
                                _builder.EmitLoadConst(dst, smallest ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity());
                                return;
                            default:
                                _builder.EmitLoadConst(dst, smallest ? std::numeric_limits<int32_t>::max() : std::numeric_limits<int32_t>::lowest());
                                return;
                        }
                    }
                    case FoldKind::Count:
                    case FoldKind::Sum:
                    case FoldKind::Any:
                        break;
                }
                _builder.Emit(Opcode::LoadConst, dst, 0, 0, 0, _builder.AddConstant(RegisterImage{}));
            }

            /// The instruction that folds the body's value in register @p body into the accumulator @p dst
            void EmitAccumulate(const TypedNode& fold, uint8_t dst, uint8_t body, uint8_t one, uint8_t zero) {
                const ProgramType type = fold.type.program;
                switch (fold.fold) {
                    case FoldKind::Count:
                        _builder.Emit(OpcodeOf(ProgramOperation::Select, ProgramType::Int32), body, body, one, zero);
                        _builder.Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), dst, dst, body);
                        return;
                    case FoldKind::Sum:
                        _builder.Emit(OpcodeOf(ProgramOperation::Add, type), dst, dst, body);
                        return;
                    case FoldKind::Min:
                        _builder.Emit(OpcodeOf(ProgramOperation::Min, type), dst, dst, body);
                        return;
                    case FoldKind::Max:
                        _builder.Emit(OpcodeOf(ProgramOperation::Max, type), dst, dst, body);
                        return;
                    case FoldKind::Any:
                        _builder.Emit(OpcodeOf(ProgramOperation::Or, ProgramType::Bool), dst, dst, body);
                        _builder.EmitBreakIf(dst, true);
                        return;
                    case FoldKind::All:
                        _builder.Emit(OpcodeOf(ProgramOperation::And, ProgramType::Bool), dst, dst, body);
                        _builder.EmitBreakIf(dst, false);
                        return;
                }
            }

            /**
             * A fold into @p dst, which is the accumulator. Above it, in this order: the list, the element of the loop, for `count` the constants 1 and 0, and the register the body is computed
             * into (and the registers its operations take above that). The body's nodes are lowered here and nowhere else, so it runs per element.
             */
            std::expected<void, ProgramError> LowerFold(const TypedNode& node, uint8_t dst) {
                uint8_t acquired = 0;
                const auto acquire = [&]() -> std::expected<uint8_t, ProgramError> {
                    const auto reg = _builder.AcquireRegister();
                    if (!reg.has_value()) {
                        return Failure(ProgramError{reg.error().message, node.column});
                    }
                    ++acquired;
                    return *reg;
                };
                EmitIdentity(dst, node.fold, node.type.program);
                const auto list = acquire();
                if (!list.has_value()) {
                    return Failure(list.error());
                }
                if (const auto lowered = LowerList(_expr[node.operands[0]], *list); !lowered.has_value()) {
                    return lowered;
                }
                const auto element = acquire();
                if (!element.has_value()) {
                    return Failure(element.error());
                }
                uint8_t one = 0;
                uint8_t zero = 0;
                if (node.fold == FoldKind::Count) {
                    const auto oneRegister = acquire();
                    const auto zeroRegister = oneRegister.has_value() ? acquire() : oneRegister;
                    if (!oneRegister.has_value() || !zeroRegister.has_value()) {
                        return Failure(oneRegister.has_value() ? zeroRegister.error() : oneRegister.error());
                    }
                    one = *oneRegister;
                    zero = *zeroRegister;
                    _builder.EmitLoadConst(one, int32_t{1});
                    _builder.EmitLoadConst(zero, int32_t{0});
                }
                const auto body = acquire();
                if (!body.has_value()) {
                    return Failure(body.error());
                }
                if (_loopRegisters.size() <= node.loop) {
                    _loopRegisters.resize(node.loop + 1, 0);
                }
                _loopRegisters[node.loop] = *element;
                const size_t begin = _builder.EmitLoopBegin(*element, *list);
                if (const auto lowered = Lower(node.body, *body); !lowered.has_value()) {
                    return lowered;
                }
                EmitAccumulate(node, dst, *body, one, zero);
                _builder.EmitLoopEnd(begin);
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
            /// By loop number: the register that holds the element of that loop while its body is lowered
            Vector<uint8_t> _loopRegisters;
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
