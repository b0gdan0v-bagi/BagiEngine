// What a `Get` of a Value<T> costs against a plain field (Values/Value.h). BE_VALUE_BENCH=<iterations> prints a line per case, the form the
// benchmark set reads (`ValueBench <type> <case> ns_per_get=<x>`); without it a case is a smoke run that checks the results and asserts
// nothing about time: tests run side by side and a machine drifts, so the time belongs to the perf set (`.claude/rules/perf-project.md`).
// A formula of about five operations over two objects is timed as `formula_plain` (C++), `formula_tree` (a virtual node per operation, a heap object each),
// `formula_program` (the compiled `expr`, fused, in the interpreter), `formula_native` (the same program through the generated function of the engine's set,
// `Programs/Native/CLAUDE.md`), `formula_run` (`Run` called directly: the interpreter with no look for a bound function in `Evaluate`; against `formula_program` it is what that look
// costs), `formula_unfused` (the same with `CompileOptions::fuse = false`, in the interpreter) and `formula_cached` (a `CachedValue` read with nothing changed). `madd_plain`, `madd_program`,
// `madd_native` and `madd_unfused` time `slot * k + b` alone, so what the peephole and the generated code gain is one number each. Every case must sum to what the plain one does.
// The same lines are printed for `int32_t` (`caster.attack * (100 + cast.level * 10) / 100`, `caster.attack * 3 + 7`) and for `Vec3`
// (`(caster.velocity + cast.target) * cast.power - caster.velocity * 0.5`, `caster.velocity * 2 + vec3(1, 2, 3)`: every step is one `Vec3` instruction and no conversion).
// A formula over a game's registered type is timed as `mask_plain` (C++ over the toy 256-bit mask: `popcount((a & shift(b, East)) | c) > 3` over three context objects),
// `mask_program` (the interpreter: five `CallDomain` instructions, each an indirect call of a registered function), `mask_run` (`Run` called directly) and `mask_native` (the
// generated function of the engine's set); the lines are `ValueBench mask_<case> ns_per_get=<x>`.
// A trigger of `Values/Effects/CLAUDE.md` (count a timer down, flip a flag and emit a command at zero, or narrow a mask and emit another) is timed as `trigger_plain` (C++ writing the
// same cell and pushing into the same `EmitBuffer`) and `trigger_script` (`RunEffects`); a case is a copy of a prototype cell, the trigger and a digest of the cell and the commands,
// `ns_per_get` is the time of one such round.
// A derived value (`Values/Derived/CLAUDE.md`) read with nothing changed is timed as `derived_hit_1` (one level), `derived_hit_2` (two levels) and `derived_hit_sources` (one source counter, no
// level), and `derived_hand` is the hand-kept compare it replaces (two `uint64_t` members against the counters of two levels); the lines are `ValueBench derived_<case> ns_per_get=<x>`. `list_count`,
// `list_any`, `list_any_early`, `list_sum` and `list_nested` time a formula with a loop over the lists of a `ProgramListHand` (`Values/Programs/Lists/`) as `list_<op>_plain` (a C++ loop of the same
// shape), `_program` (the interpreter) and `_native` (the generated function): 14 tiles in an array and a few sets of four in a vector, 64 seeded hands.

#include <BECore/EffectsToys.h>
#include <BECore/ProgramDomainTypes.h>
#include <BECore/ProgramListTypes.h>
#include <BECore/ProgramRowTypes.h>
#include <BECore/Random/Rng.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/CachedValue.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Derived/Derived.h>
#include <BECore/Values/Effects/EffectCommands.h>
#include <BECore/Values/Effects/EffectCompiler.h>
#include <BECore/Values/Effects/EffectRunner.h>
#include <BECore/Values/Effects/WriteTargets.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <NativePrograms/NativeProgramFormulas.h>
#include <NativePrograms/NativeProgramSet.h>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;
    using ProgramDomainData::BitsAnd;
    using ProgramDomainData::BitsOr;
    using ProgramDomainData::BitsPopcount;
    using ProgramDomainData::BitsShift;
    using ProgramDomainData::DomainBits256;
    using ProgramDomainData::DomainDirection;

    namespace {

        constexpr const char* kBenchVariable = "BE_VALUE_BENCH";
        constexpr uint64_t kSmokeIterations = 4000;
        constexpr double kNanosPerSecond = 1e9;
        /// A power of two, so the index of an iteration wraps with a mask; the objects (about 16 KB) stay in the cache whatever the case
        constexpr size_t kObjects = 64;
        constexpr size_t kObjectMask = kObjects - 1;
        constexpr StringView kFormula = "caster.attack * (100 + cast.level * 10) / 100";
        constexpr int32_t kFormulaBase = 100;
        constexpr int32_t kFormulaPerLevel = 10;
        constexpr int32_t kCastLevels = 5;
        constexpr StringView kMadd = "caster.attack * 3 + 7";
        constexpr StringView kVec3Formula = "(caster.velocity + cast.target) * cast.power - caster.velocity * 0.5";
        constexpr StringView kVec3Madd = "caster.velocity * 2 + vec3(1, 2, 3)";
        constexpr float kVec3Half = 0.5f;
        constexpr float kVec3Scale = 2.0f;
        constexpr Vec3 kVec3Term{1.0f, 2.0f, 3.0f};
        constexpr int32_t kMaddFactor = 3;
        constexpr int32_t kMaddTerm = 7;
        constexpr float kPowerBase = 0.5f;
        constexpr float kPowerStep = 0.25f;
        constexpr size_t kPowerSteps = 7;
        constexpr float kTargetY = 4.5f;
        constexpr float kTargetZ = -2.0f;

        uint64_t Iterations(bool& printing) {
            const char* text = std::getenv(kBenchVariable);
            printing = false;
            if (text == nullptr || *text == '\0') {
                return kSmokeIterations;
            }
            uint64_t value = 0;
            const char* end = text + std::strlen(text);
            const auto result = std::from_chars(text, end, value);
            if (result.ec != std::errc() || result.ptr != end || value == 0) {
                return kSmokeIterations;
            }
            printing = true;
            return value;
        }

        /// Makes the result escape so that an optimizer cannot drop the reads that made it
        void Escape(uint64_t result) {
            static std::atomic<uint64_t> sink;
            sink.store(result, std::memory_order_relaxed);
        }

        /// The bits of a value as an integer, so a sum of them is exact whatever the type and costs next to nothing
        uint64_t Fold(int32_t value) {
            return static_cast<uint64_t>(std::bit_cast<uint32_t>(value));
        }

        uint64_t Fold(const Vec3& value) {
            return static_cast<uint64_t>(std::bit_cast<uint32_t>(value.x)) + std::bit_cast<uint32_t>(value.y) + std::bit_cast<uint32_t>(value.z);
        }

        /// The field of `ValueUnitStats` a type of the bench reads, and a value of that type that differs from one object to the next
        template <typename T>
        struct Subject;

        template <>
        struct Subject<int32_t> {
            static constexpr const char* kName = "int32_t";
            static constexpr StringView kPath = "caster.attack";
            static constexpr int32_t kStep = 7;

            static const int32_t& Field(const ValueUnitStats& object) {
                return object.attack;
            }
            static void Fill(ValueUnitStats& object, size_t index) {
                object.attack = 1000 + static_cast<int32_t>(index) * kStep;
            }
        };

        template <>
        struct Subject<Vec3> {
            static constexpr const char* kName = "Vec3";
            static constexpr StringView kPath = "caster.velocity";
            static constexpr float kFraction = 0.25f;

            static const Vec3& Field(const ValueUnitStats& object) {
                return object.velocity;
            }
            static void Fill(ValueUnitStats& object, size_t index) {
                const float base = static_cast<float>(index);
                object.velocity = Vec3{base, base * 2.0f + kFraction, base * 3.0f};
            }
        };

        /// The nodes of a tree of values: a small hierarchy where every read is a virtual call that reads the next node
        template <typename T>
        class Node {
        public:
            virtual ~Node() = default;
            [[nodiscard]] virtual const T& Read(const Context& context) const = 0;
        };

        template <typename T>
        class RelayNode final : public Node<T> {
        public:
            explicit RelayNode(const Node<T>& next) : _next(&next) {}
            [[nodiscard]] const T& Read(const Context& context) const override {
                return _next->Read(context);
            }

        private:
            const Node<T>* _next;
        };

        /// The end of a chain that reads the field in place
        template <typename T>
        class FieldNode final : public Node<T> {
        public:
            explicit FieldNode(const T& field) : _field(&field) {}
            [[nodiscard]] const T& Read(const Context& /*context*/) const override {
                return *_field;
            }

        private:
            const T* _field;
        };

        /// The end of a chain that reads a `Slot` of the context
        template <typename T>
        class SlotNode final : public Node<T> {
        public:
            explicit SlotNode(const Value<T>& value) : _value(value) {}
            [[nodiscard]] const T& Read(const Context& context) const override {
                return _value.Get(context);
            }

        private:
            Value<T> _value;
        };

        /// Objects, a context per object, and the values and chains that read them: every case reads the same numbers
        template <typename T>
        struct Rig {
            ContextDecl declaration;
            Vector<ValueUnitStats> objects;
            Vector<Context> contexts;
            Vector<Value<T>> constants;
            Value<T> slot;
            Vector<UniquePtr<Node<T>>> nodes;
            Array<const Node<T>*, kObjects> fieldChains{};
            Array<const Node<T>*, kObjects> slotChains{};

            Rig() : objects(kObjects) {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(slot.Link(declaration, Subject<T>::kPath).has_value());
                contexts.reserve(kObjects);
                constants.reserve(kObjects);
                for (size_t index = 0; index < kObjects; ++index) {
                    Subject<T>::Fill(objects[index], index);
                    Context& context = contexts.emplace_back(declaration);
                    context.Set(kCasterLevel, objects[index]);
                    constants.push_back(Value<T>::Const(Subject<T>::Field(objects[index])));
                    fieldChains[index] = MakeChain(MakeNode<FieldNode<T>>(Subject<T>::Field(objects[index])));
                    slotChains[index] = MakeChain(MakeNode<SlotNode<T>>(slot));
                }
            }

            Rig(const Rig&) = delete;
            Rig& operator=(const Rig&) = delete;

            /// Two relays and the end: three virtual calls per read
            const Node<T>* MakeChain(const Node<T>* end) {
                const Node<T>* second = MakeNode<RelayNode<T>>(*end);
                return MakeNode<RelayNode<T>>(*second);
            }

            template <typename TNode, typename... Args>
            const Node<T>* MakeNode(Args&&... args) {
                nodes.push_back(MakeUnique<TNode>(std::forward<Args>(args)...));
                return nodes.back().get();
            }
        };

        uint64_t Fold(bool value) {
            return value ? 1U : 0U;
        }

        uint64_t Fold(uint64_t value) {
            return value;
        }

        /// Runs @p read for @p iterations and returns the sum of the folded results; the time per read is printed when @p printing, as `ValueBench <type> <name>` or, with no @p type, `ValueBench
        /// <name>`
        template <typename TRead>
        uint64_t TimeCase(const char* type, const char* name, uint64_t iterations, bool printing, TRead&& read) {
            uint64_t sum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (uint64_t index = 0; index < iterations; ++index) {
                sum += Fold(read(static_cast<size_t>(index) & kObjectMask));
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            Escape(sum);
            if (printing) {
                const double nanos = std::chrono::duration<double>(elapsed).count() * kNanosPerSecond / static_cast<double>(iterations);
                if (type != nullptr) {
                    std::printf("ValueBench %s %s ns_per_get=%.3f\n", type, name, nanos);
                } else {
                    std::printf("ValueBench %s ns_per_get=%.3f\n", name, nanos);
                }
            }
            return sum;
        }

        template <typename T>
        void BenchType() {
            bool printing = false;
            const uint64_t iterations = Iterations(printing);
            const Rig<T> rig;
            const char* type = Subject<T>::kName;
            const Context empty;

            const uint64_t plain = TimeCase(type, "plain", iterations, printing, [&rig](size_t index) -> const T& { return Subject<T>::Field(rig.objects[index]); });
            EXPECT_NE(plain, 0U) << type;
            EXPECT_EQ(TimeCase(type, "const", iterations, printing, [&rig, &empty](size_t index) -> const T& { return rig.constants[index].Get(empty); }), plain) << type << " const";
            EXPECT_EQ(TimeCase(type, "slot", iterations, printing, [&rig](size_t index) -> const T& { return rig.slot.Get(rig.contexts[index]); }), plain) << type << " slot";
            EXPECT_EQ(TimeCase(type, "tree_field", iterations, printing, [&rig, &empty](size_t index) -> const T& { return rig.fieldChains[index]->Read(empty); }), plain) << type << " tree_field";
            EXPECT_EQ(TimeCase(type, "tree_slot", iterations, printing, [&rig](size_t index) -> const T& { return rig.slotChains[index]->Read(rig.contexts[index]); }), plain) << type << " tree_slot";
        }

        /// A formula as a tree of heap objects with a virtual `Eval` per step: the shape of a formula held as parts (a node per operation)
        template <typename T>
        class TreeNode {
        public:
            virtual ~TreeNode() = default;
            [[nodiscard]] virtual T Eval(const Context& context) const = 0;
        };

        template <typename T>
        class ConstNode final : public TreeNode<T> {
        public:
            explicit ConstNode(T value) : _value(value) {}
            [[nodiscard]] T Eval(const Context& /*context*/) const override {
                return _value;
            }

        private:
            T _value;
        };

        /// A field of a context object, read through the linked slot
        template <typename T>
        class ReadNode final : public TreeNode<T> {
        public:
            explicit ReadNode(const Value<T>& slot) : _slot(slot) {}
            [[nodiscard]] T Eval(const Context& context) const override {
                return _slot.Get(context);
            }

        private:
            Value<T> _slot;
        };

        struct AddOp {
            template <typename L, typename R>
            auto operator()(const L& left, const R& right) const {
                return left + right;
            }
        };

        struct SubOp {
            template <typename L, typename R>
            auto operator()(const L& left, const R& right) const {
                return left - right;
            }
        };

        struct MulOp {
            template <typename L, typename R>
            auto operator()(const L& left, const R& right) const {
                return left * right;
            }
        };

        struct DivOp {
            template <typename L, typename R>
            auto operator()(const L& left, const R& right) const {
                return left / right;
            }
        };

        template <typename TOut, typename TLeft, typename TRight, typename TOp>
        class BinaryNode final : public TreeNode<TOut> {
        public:
            BinaryNode(UniquePtr<TreeNode<TLeft>> left, UniquePtr<TreeNode<TRight>> right) : _left(std::move(left)), _right(std::move(right)) {}
            [[nodiscard]] TOut Eval(const Context& context) const override {
                return TOp{}(_left->Eval(context), _right->Eval(context));
            }

        private:
            UniquePtr<TreeNode<TLeft>> _left;
            UniquePtr<TreeNode<TRight>> _right;
        };

        template <typename T>
        using TreePtr = UniquePtr<TreeNode<T>>;

        template <typename T>
        TreePtr<T> Constant(T value) {
            return MakeUnique<ConstNode<T>>(value);
        }

        template <typename T>
        TreePtr<T> Read(const ContextDecl& declaration, StringView path) {
            Value<T> slot;
            EXPECT_TRUE(slot.Link(declaration, path).has_value()) << String(path).c_str();
            return MakeUnique<ReadNode<T>>(slot);
        }

        template <typename TOp, typename TOut, typename TLeft, typename TRight>
        TreePtr<TOut> Binary(TreePtr<TLeft> left, TreePtr<TRight> right) {
            return MakeUnique<BinaryNode<TOut, TLeft, TRight, TOp>>(std::move(left), std::move(right));
        }

        /// Two levels, an object of each per iteration: a context without counters, and one with a counter per level for the cached reads
        struct FormulaWorld {
            ContextDecl declaration;
            Vector<ValueUnitStats> casters;
            Vector<ValueCastInfo> casts;
            Vector<uint64_t> casterVersions;
            Vector<uint64_t> castVersions;
            Vector<Context> contexts;
            Vector<Context> versionedContexts;

            FormulaWorld() : casters(kObjects), casts(kObjects), casterVersions(kObjects, 1), castVersions(kObjects, 1) {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                contexts.reserve(kObjects);
                versionedContexts.reserve(kObjects);
                for (size_t index = 0; index < kObjects; ++index) {
                    Subject<int32_t>::Fill(casters[index], index);
                    Subject<Vec3>::Fill(casters[index], index);
                    casts[index].level = 1 + static_cast<int32_t>(index) % kCastLevels;
                    casts[index].power = kPowerBase + static_cast<float>(index % kPowerSteps) * kPowerStep;
                    casts[index].target = Vec3{static_cast<float>(index), kTargetY, kTargetZ};
                    Context& context = contexts.emplace_back(declaration);
                    context.Set(kCasterLevel, casters[index]);
                    context.Set(kCastLevel, casts[index]);
                    Context& versioned = versionedContexts.emplace_back(declaration);
                    versioned.Set(kCasterLevel, casters[index], &casterVersions[index]);
                    versioned.Set(kCastLevel, casts[index], &castVersions[index]);
                }
            }

            FormulaWorld(const FormulaWorld&) = delete;
            FormulaWorld& operator=(const FormulaWorld&) = delete;
        };

        /// The names of the lines one formula prints; a null name is a case that is not timed
        struct FormulaNames {
            const char* plain;
            const char* tree;
            const char* program;
            const char* native;
            const char* run;
            const char* unfused;
            const char* cached;
        };

        constexpr FormulaNames kFormulaNames{"formula_plain", "formula_tree", "formula_program", "formula_native", "formula_run", "formula_unfused", "formula_cached"};
        constexpr FormulaNames kMaddNames{"madd_plain", nullptr, "madd_program", "madd_native", nullptr, "madd_unfused", nullptr};

        /// Sets the generated code on or off for the length of a case and puts the state back: the interpreter and the generated function are timed through the same `Evaluate`
        class NativeSwitch {
        public:
            explicit NativeSwitch(bool enabled) : _was(NativePrograms::IsEnabled()) {
                NativePrograms::SetEnabled(enabled);
            }
            ~NativeSwitch() {
                NativePrograms::SetEnabled(_was);
            }
            NativeSwitch(const NativeSwitch&) = delete;
            NativeSwitch& operator=(const NativeSwitch&) = delete;

        private:
            bool _was = false;
        };

        size_t CountMulAddSlot(const Program& program) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [](const Instruction& instruction) { return IsMulAddSlot(instruction.op); }));
        }

        /// Times @p text as plain C++, as a tree (when @p tree is given), as the fused program in the interpreter and through its generated function, as the program with
        /// `fuse = false` and as a cached read (with the tree), and checks that every case gives the sum of the plain one; the generated function must be bound, or the case would time the
        /// interpreter twice. @p expectFused says whether the default compile has a `MulAddSlot` and the unfused one never has: a benchmark of two
        /// equal programs would say nothing; with no value the formula is not asked
        template <typename T, typename TPlain>
        void BenchFormulaCases(const FormulaWorld& world, StringView text, const FormulaNames& names, TPlain&& plainRead, const TreeNode<T>* tree, Optional<bool> expectFused) {
            bool printing = false;
            const uint64_t iterations = Iterations(printing);
            const char* type = Subject<T>::kName;

            const auto fusedCompiled = CompileValue<T>(text, world.declaration);
            ASSERT_TRUE(fusedCompiled.has_value()) << String(text).c_str();
            const Value<T>& fused = *fusedCompiled;
            const auto unfusedCompiled = CompileValue<T>(text, world.declaration, CompileOptions{.fuse = false});
            ASSERT_TRUE(unfusedCompiled.has_value()) << String(text).c_str();
            const Value<T>& unfused = *unfusedCompiled;
            ASSERT_EQ(fused.Kind(), ValueKind::Program) << names.program;
            ASSERT_EQ(unfused.Kind(), ValueKind::Program) << names.unfused;
            EXPECT_EQ(CountMulAddSlot(ProgramPool::Get(unfused.Id())), 0U) << names.unfused;
            if (expectFused.has_value()) {
                EXPECT_EQ(CountMulAddSlot(ProgramPool::Get(fused.Id())) > 0, *expectFused) << names.program;
                EXPECT_EQ(fused.Id() == unfused.Id(), !*expectFused) << names.program << " and " << names.unfused << " are the same program or not as the fusion says";
            }

            const uint64_t plain = TimeCase(type, names.plain, iterations, printing, plainRead);
            EXPECT_NE(plain, 0U) << names.plain;
            {
                const NativeSwitch interpreted(false);
                EXPECT_EQ(TimeCase(type, names.program, iterations, printing, [&](size_t index) { return fused.Evaluate(world.contexts[index]); }), plain) << names.program;
                if (names.run != nullptr) {
                    const ProgramId id = fused.Id();
                    EXPECT_EQ(TimeCase(type, names.run, iterations, printing, [&](size_t index) { return Run<T>(ProgramPool::Get(id), world.contexts[index]); }), plain) << names.run;
                }
                EXPECT_EQ(TimeCase(type, names.unfused, iterations, printing, [&](size_t index) { return unfused.Evaluate(world.contexts[index]); }), plain) << names.unfused;
            }
            {
                const NativeSwitch generated(true);
                ASSERT_NE(ProgramPool::Native(fused.Id()), nullptr) << names.native << ": no generated function is bound to " << String(text).c_str() << ", the set in tests/NativePrograms lacks it";
                EXPECT_EQ(TimeCase(type, names.native, iterations, printing, [&](size_t index) { return fused.Evaluate(world.contexts[index]); }), plain) << names.native;
            }
            if (tree == nullptr) {
                return;
            }
            EXPECT_EQ(TimeCase(type, names.tree, iterations, printing, [&](size_t index) { return tree->Eval(world.contexts[index]); }), plain) << names.tree;

            Vector<CachedValue<T>> cached;
            cached.reserve(kObjects);
            for (size_t index = 0; index < kObjects; ++index) {
                cached.emplace_back(fused);
                (void)cached.back().Evaluate(world.versionedContexts[index]);
            }
            EXPECT_EQ(TimeCase(type, names.cached, iterations, printing, [&](size_t index) { return cached[index].Evaluate(world.versionedContexts[index]); }), plain) << names.cached;
            for (const CachedValue<T>& value : cached) {
                EXPECT_EQ(value.Computations(), 1U) << names.cached << " ran its program again with nothing changed";
            }
        }

        /// `kFormula` written as C++ over the two objects
        int32_t PlainFormula(const ValueUnitStats& caster, const ValueCastInfo& cast) {
            return caster.attack * (kFormulaBase + cast.level * kFormulaPerLevel) / kFormulaBase;
        }

        /// `kFormula` as a tree of nodes of the same shape
        TreePtr<int32_t> FormulaTree(const ContextDecl& declaration) {
            TreePtr<int32_t> perLevel = Binary<MulOp, int32_t>(Read<int32_t>(declaration, "cast.level"), Constant<int32_t>(kFormulaPerLevel));
            TreePtr<int32_t> factor = Binary<AddOp, int32_t>(Constant<int32_t>(kFormulaBase), std::move(perLevel));
            TreePtr<int32_t> scaled = Binary<MulOp, int32_t>(Read<int32_t>(declaration, "caster.attack"), std::move(factor));
            return Binary<DivOp, int32_t>(std::move(scaled), Constant<int32_t>(kFormulaBase));
        }

        void BenchFormula() {
            const FormulaWorld world;
            const TreePtr<int32_t> tree = FormulaTree(world.declaration);
            BenchFormulaCases<int32_t>(world, kFormula, kFormulaNames, [&](size_t index) { return PlainFormula(world.casters[index], world.casts[index]); }, tree.get(), Optional<bool>{});
        }

        int32_t PlainMadd(const ValueUnitStats& caster) {
            return caster.attack * kMaddFactor + kMaddTerm;
        }

        void BenchMadd() {
            const FormulaWorld world;
            BenchFormulaCases<int32_t>(world, kMadd, kMaddNames, [&](size_t index) { return PlainMadd(world.casters[index]); }, nullptr, true);
        }

        /// `kVec3Formula` as C++: the multiplications and the subtraction in the order the program runs them, so the sums agree bit for bit
        Vec3 PlainVec3Formula(const ValueUnitStats& caster, const ValueCastInfo& cast) {
            return (caster.velocity + cast.target) * cast.power - caster.velocity * kVec3Half;
        }

        TreePtr<Vec3> Vec3FormulaTree(const ContextDecl& declaration) {
            TreePtr<Vec3> sum = Binary<AddOp, Vec3>(Read<Vec3>(declaration, "caster.velocity"), Read<Vec3>(declaration, "cast.target"));
            TreePtr<Vec3> scaled = Binary<MulOp, Vec3>(std::move(sum), Read<float>(declaration, "cast.power"));
            TreePtr<Vec3> half = Binary<MulOp, Vec3>(Read<Vec3>(declaration, "caster.velocity"), Constant<float>(kVec3Half));
            return Binary<SubOp, Vec3>(std::move(scaled), std::move(half));
        }

        void BenchVec3Formula() {
            const FormulaWorld world;
            const TreePtr<Vec3> tree = Vec3FormulaTree(world.declaration);
            BenchFormulaCases<Vec3>(world, kVec3Formula, kFormulaNames, [&](size_t index) { return PlainVec3Formula(world.casters[index], world.casts[index]); }, tree.get(), Optional<bool>{});
        }

        Vec3 PlainVec3Madd(const ValueUnitStats& caster) {
            return caster.velocity * kVec3Scale + kVec3Term;
        }

        /// `slot * k + b` over a `Vec3` is fused like the numbers, so the two programs differ
        void BenchVec3Madd() {
            const FormulaWorld world;
            BenchFormulaCases<Vec3>(world, kVec3Madd, kMaddNames, [&](size_t index) { return PlainVec3Madd(world.casters[index]); }, nullptr, true);
        }

        constexpr StringView kMaskFormula = kNativeDomainBoolFormulas[0];
        constexpr int32_t kMaskPopcountLimit = 3;
        constexpr uint64_t kMaskSeed = 0x6d61736b;
        constexpr size_t kMaskWords = 4;

        /// `kMaskFormula` as C++ over the toy mask, calling the same functions the registered operations are
        bool PlainMask(const DomainBits256& a, const DomainBits256& b, const DomainBits256& c) {
            return BitsPopcount(BitsOr(BitsAnd(a, BitsShift(b, DomainDirection::East)), c)) > kMaskPopcountLimit;
        }

        /// Three levels of masks, an object of each per iteration; the masks are sparse so that the answer changes from one object to the next
        struct MaskWorld {
            Vector<ProgramRowData::ProgramRowOperands> a;
            Vector<ProgramRowData::ProgramRowOperands> b;
            Vector<ProgramRowData::ProgramRowOperands> c;
            Vector<Context> contexts;

            MaskWorld() : a(kObjects), b(kObjects), c(kObjects) {
                Rng rng(kMaskSeed);
                contexts.reserve(kObjects);
                for (size_t index = 0; index < kObjects; ++index) {
                    for (size_t word = 0; word < kMaskWords; ++word) {
                        a[index].mask.words[word] = rng.Next64() & rng.Next64() & rng.Next64();
                        b[index].mask.words[word] = rng.Next64() & rng.Next64() & rng.Next64();
                        c[index].mask.words[word] = ((index >> word) & 1) != 0 ? uint64_t{1} << (rng.Next64() % 64) : 0;
                    }
                    Context& context = contexts.emplace_back(NativeProgramSetDeclaration());
                    context.Set(kOperandsLevelA, a[index]);
                    context.Set(kOperandsLevelB, b[index]);
                    context.Set(kOperandsLevelC, c[index]);
                }
            }

            MaskWorld(const MaskWorld&) = delete;
            MaskWorld& operator=(const MaskWorld&) = delete;
        };

        size_t CountCallDomain(const Program& program) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [](const Instruction& instruction) { return instruction.op == Opcode::CallDomain; }));
        }

        void BenchMask() {
            bool printing = false;
            const uint64_t iterations = Iterations(printing);
            // The toys first: the generated function of the set binds while the registry is what the tool had
            static_cast<void>(ProgramDomainData::RegisterDomainToys());
            const MaskWorld world;
            const auto compiled = CompileValue<bool>(kMaskFormula, NativeProgramSetDeclaration());
            ASSERT_TRUE(compiled.has_value()) << String(kMaskFormula).c_str();
            const Value<bool>& value = *compiled;
            ASSERT_EQ(value.Kind(), ValueKind::Program);
            // A case that ran no `CallDomain` would time the interpreter's other instructions and say nothing about a registered operation
            EXPECT_GE(CountCallDomain(ProgramPool::Get(value.Id())), 1U);

            const uint64_t plain = TimeCase(nullptr, "mask_plain", iterations, printing, [&](size_t index) { return PlainMask(world.a[index].mask, world.b[index].mask, world.c[index].mask); });
            EXPECT_GT(plain, 0U) << "the masks never pass the test";
            EXPECT_LT(plain, iterations) << "the masks always pass the test";
            {
                const NativeSwitch interpreted(false);
                EXPECT_EQ(TimeCase(nullptr, "mask_program", iterations, printing, [&](size_t index) { return value.Evaluate(world.contexts[index]); }), plain);
                const ProgramId id = value.Id();
                EXPECT_EQ(TimeCase(nullptr, "mask_run", iterations, printing, [&](size_t index) { return Run<bool>(ProgramPool::Get(id), world.contexts[index]); }), plain);
            }
            {
                const NativeSwitch generated(true);
                ASSERT_NE(ProgramPool::Native(value.Id()), nullptr) << "mask_native: no generated function is bound to " << String(kMaskFormula).c_str();
                EXPECT_EQ(TimeCase(nullptr, "mask_native", iterations, printing, [&](size_t index) { return value.Evaluate(world.contexts[index]); }), plain);
            }
        }

        constexpr StringView kDerivedAttackFormula = "caster.attack";
        constexpr StringView kDerivedLevelFormula = "cast.level";

        /// What the four derived cases compute, so that a hit and a hand-kept compare are checked against the same C++
        int32_t DerivedOneLevel(const ValueUnitStats& caster) {
            return caster.attack * kMaddFactor + kMaddTerm;
        }

        int32_t DerivedTwoLevels(const ValueUnitStats& caster, const ValueCastInfo& cast) {
            return caster.attack * kMaddFactor + cast.level;
        }

        /// The version compare a screen keeps by hand: the counters it saw at the last build, one member each
        struct HandKept {
            uint64_t casterSeen = 0;
            uint64_t castSeen = 0;
            int32_t result = 0;
            bool valid = false;
            uint32_t builds = 0;

            int32_t Get(const Context& context, const ValueUnitStats& caster, const ValueCastInfo& cast) {
                const uint64_t casterNow = context.Version(kCasterLevel);
                const uint64_t castNow = context.Version(kCastLevel);
                if (!valid || casterNow != casterSeen || castNow != castSeen) {
                    result = DerivedTwoLevels(caster, cast);
                    casterSeen = casterNow;
                    castSeen = castNow;
                    valid = true;
                    ++builds;
                }
                return result;
            }
        };

        void BenchDerived() {
            bool printing = false;
            const uint64_t iterations = Iterations(printing);
            const FormulaWorld world;
            Value<int32_t> attack;
            Value<int32_t> level;
            ASSERT_TRUE(attack.Link(world.declaration, kDerivedAttackFormula).has_value());
            ASSERT_TRUE(level.Link(world.declaration, kDerivedLevelFormula).has_value());
            const Context noLevels;

            const uint64_t plainOne = TimeCase(nullptr, "derived_plain_1", iterations, false, [&](size_t index) { return DerivedOneLevel(world.casters[index]); });
            const uint64_t plainTwo = TimeCase(nullptr, "derived_plain_2", iterations, false, [&](size_t index) { return DerivedTwoLevels(world.casters[index], world.casts[index]); });

            // One value per object, each built once before the clock starts: a case that timed a build would be a case about the builder
            Vector<Derived<int32_t>> oneLevel;
            Vector<Derived<int32_t>> twoLevels;
            Vector<Derived<int32_t>> sources;
            oneLevel.reserve(kObjects);
            twoLevels.reserve(kObjects);
            sources.reserve(kObjects);
            for (size_t index = 0; index < kObjects; ++index) {
                DerivedReads one;
                one.Add(attack);
                oneLevel.emplace_back(std::move(one));
                DerivedReads two;
                two.Add(attack).Add(level);
                twoLevels.emplace_back(std::move(two));
                DerivedReads counted;
                ASSERT_TRUE(counted.AddSource(world.casterVersions[index]));
                sources.emplace_back(std::move(counted));
            }
            const auto buildOne = [&](int32_t& out, const Context& view) { out = attack.Get(view) * kMaddFactor + kMaddTerm; };
            const auto buildTwo = [&](int32_t& out, const Context& view) { out = attack.Get(view) * kMaddFactor + level.Get(view); };
            const auto buildCounted = [&](size_t index) { return [&world, index](int32_t& out, const Context&) { out = DerivedOneLevel(world.casters[index]); }; };
            for (size_t index = 0; index < kObjects; ++index) {
                static_cast<void>(oneLevel[index].Get(world.versionedContexts[index], buildOne));
                static_cast<void>(twoLevels[index].Get(world.versionedContexts[index], buildTwo));
                static_cast<void>(sources[index].Get(noLevels, buildCounted(index)));
            }
            Vector<HandKept> hand(kObjects);
            for (size_t index = 0; index < kObjects; ++index) {
                static_cast<void>(hand[index].Get(world.versionedContexts[index], world.casters[index], world.casts[index]));
            }

            EXPECT_EQ(TimeCase(nullptr, "derived_hit_1", iterations, printing, [&](size_t index) { return oneLevel[index].Get(world.versionedContexts[index], buildOne); }), plainOne)
                << "derived_hit_1";
            EXPECT_EQ(TimeCase(nullptr, "derived_hit_2", iterations, printing, [&](size_t index) { return twoLevels[index].Get(world.versionedContexts[index], buildTwo); }), plainTwo)
                << "derived_hit_2";
            EXPECT_EQ(TimeCase(nullptr, "derived_hit_sources", iterations, printing, [&](size_t index) { return sources[index].Get(noLevels, buildCounted(index)); }), plainOne)
                << "derived_hit_sources";
            EXPECT_EQ(TimeCase(nullptr, "derived_hand", iterations, printing, [&](size_t index) { return hand[index].Get(world.versionedContexts[index], world.casters[index], world.casts[index]); }),
                      plainTwo)
                << "derived_hand";

            for (size_t index = 0; index < kObjects; ++index) {
                EXPECT_EQ(oneLevel[index].Rebuilds(), 1U) << "derived_hit_1 built again with nothing changed";
                EXPECT_EQ(twoLevels[index].Rebuilds(), 1U) << "derived_hit_2 built again with nothing changed";
                EXPECT_EQ(sources[index].Rebuilds(), 1U) << "derived_hit_sources built again with nothing changed";
                EXPECT_EQ(hand[index].builds, 1U) << "derived_hand built again with nothing changed";
            }
        }

        constexpr StringView kTriggerScript = "if (cell.i32 > 0) { cell.i32 -= 1; if (cell.i32 == 0) { cell.flag = !cell.flag; emit FxCount(cell.i32b); } } "
                                              "else if (board.mask != cell.mask) { cell.mask = and_mask(cell.mask, board.mask); emit FxMask(cell.mask); }";
        constexpr uint64_t kTriggerSeed = 0x74726967;
        constexpr size_t kTriggerBufferCapacity = 4;
        constexpr uint64_t kDigestMultiplier = 0x100000001b3ULL;
        constexpr uint32_t kTimerRange = 4;
        constexpr size_t kEveryNthSharesTheMask = 3;
        constexpr LevelIndex kCellLevel = 0;
        constexpr LevelIndex kBoardLevel = 1;

        using TriggerObject = ProgramRowData::ProgramRowOperands;

        /// A digest of the cell and of what the run pushed, so that a case that stored or emitted differently sums differently
        uint64_t Digest(const TriggerObject& cell, const EmitBuffer& buffer) {
            uint64_t digest = static_cast<uint64_t>(static_cast<uint32_t>(cell.i32));
            const auto mix = [&digest](uint64_t value) { digest = (digest ^ value) * kDigestMultiplier; };
            mix(cell.flag ? 1U : 0U);
            for (const uint64_t word : cell.mask.words) {
                mix(word);
            }
            mix(buffer.Count());
            for (const EmittedCommand& command : buffer.Commands()) {
                mix(command.id);
                uint64_t first = 0;
                std::memcpy(&first, command.arguments[0].bytes.data(), sizeof(first));
                mix(first);
            }
            return digest;
        }

        /// The script as C++: the commands go to the same buffer type
        void PlainTrigger(TriggerObject& cell, const TriggerObject& board, EmitBuffer& buffer, const EffectsData::EffectToys& toys) {
            EmittedCommand command;
            command.count = 1;
            if (cell.i32 > 0) {
                cell.i32 -= 1;
                if (cell.i32 == 0) {
                    cell.flag = !cell.flag;
                    command.id = toys.count;
                    std::memcpy(command.arguments[0].bytes.data(), &cell.i32b, sizeof(cell.i32b));
                    buffer.Push(command);
                }
            } else if (board.mask != cell.mask) {
                cell.mask = BitsAnd(cell.mask, board.mask);
                command.id = toys.mask;
                std::memcpy(command.arguments[0].bytes.data(), &cell.mask, sizeof(cell.mask));
                buffer.Push(command);
            }
        }

        void BenchTrigger() {
            bool printing = false;
            const uint64_t iterations = Iterations(printing);
            const EffectsData::EffectToys& toys = EffectsData::RegisterEffectToys();
            ContextDecl declaration;
            ASSERT_TRUE(declaration.Add<TriggerObject>("cell").has_value());
            ASSERT_TRUE(declaration.Add<TriggerObject>("board").has_value());
            WriteTargets targets;
            ASSERT_TRUE(targets.Allow("cell"));
            EffectOptions options;
            options.targets = &targets;
            const auto compiled = CompileEffects(kTriggerScript, declaration, options);
            ASSERT_TRUE(compiled.has_value());
            const EffectScript& script = *compiled;

            Rng rng(kTriggerSeed);
            TriggerObject board;
            for (uint64_t& word : board.mask.words) {
                word = rng.Next64();
            }
            // Prototypes reach every branch: a timer that ends, a timer still running, a spent timer with another mask and one with the board's
            Vector<TriggerObject> prototypes(kObjects);
            for (size_t index = 0; index < kObjects; ++index) {
                TriggerObject& cell = prototypes[index];
                cell.i32 = static_cast<int32_t>(rng.Next32() % kTimerRange);
                cell.i32b = static_cast<int32_t>(index);
                cell.flag = (rng.Next32() & 1U) != 0;
                for (size_t word = 0; word < kMaskWords; ++word) {
                    cell.mask.words[word] = index % kEveryNthSharesTheMask == 0 ? board.mask.words[word] : rng.Next64();
                }
            }
            Vector<TriggerObject> cells = prototypes;
            Vector<uint64_t> versions(kObjects, 0);
            Vector<Context> contexts;
            contexts.reserve(kObjects);
            for (size_t index = 0; index < kObjects; ++index) {
                Context& context = contexts.emplace_back(declaration);
                context.SetMutable(kCellLevel, cells[index], &versions[index]);
                context.Set(kBoardLevel, board);
            }
            EmitBufferStorage<kTriggerBufferCapacity> storage;
            EmitBuffer& buffer = storage.Buffer();

            // The script must store and emit, or both cases would time a trigger that does nothing
            uint64_t emittedTotal = 0;
            uint64_t changedTotal = 0;
            for (size_t index = 0; index < kObjects; ++index) {
                buffer.Clear();
                const EffectResult result = RunEffects(*script.Program(), contexts[index], buffer);
                emittedTotal += result.emitted;
                changedTotal += result.changed;
            }
            EXPECT_GT(emittedTotal, 0U);
            EXPECT_GT(changedTotal, 0U);
            EXPECT_LT(emittedTotal, kObjects);

            const uint64_t plain = TimeCase(nullptr, "trigger_plain", iterations, printing, [&](size_t index) {
                cells[index] = prototypes[index];
                buffer.Clear();
                PlainTrigger(cells[index], board, buffer, toys);
                return Digest(cells[index], buffer);
            });
            EXPECT_EQ(TimeCase(nullptr, "trigger_script", iterations, printing,
                               [&](size_t index) {
                                   cells[index] = prototypes[index];
                                   buffer.Clear();
                                   RunEffects(*script.Program(), contexts[index], buffer);
                                   return Digest(cells[index], buffer);
                               }),
                      plain);
        }

        using ProgramListData::ProgramListHand;
        using ProgramListData::ProgramListSet;
        using ProgramListData::ProgramListTile;

        constexpr uint64_t kListSeed = 20260301;
        constexpr int32_t kMaxRank = 8;
        constexpr double kTerminalChance = 0.3;
        constexpr size_t kFewestSets = 4;
        constexpr size_t kSetCountSpread = 5;

        /// Hands whose answers vary from one to the next but whose ranks never pass `kMaxRank`, so `any(.., t.rank > 8)` is a full loop; the first tile of every hand is a terminal, so
        /// `any(.., t.terminal)` is answered by the first element
        struct ListWorld {
            Vector<ProgramListHand> hands;
            Vector<Context> contexts;

            ListWorld() : hands(kObjects) {
                Rng rng(kListSeed);
                const auto fill = [&rng](ProgramListTile& tile) {
                    tile.rank = rng.UniformInt<int32_t>(0, kMaxRank);
                    tile.terminal = rng.Chance(kTerminalChance);
                };
                contexts.reserve(kObjects);
                for (size_t index = 0; index < kObjects; ++index) {
                    ProgramListHand& hand = hands[index];
                    for (ProgramListTile& tile : hand.tiles) {
                        fill(tile);
                    }
                    hand.tiles[0].terminal = true;
                    for (size_t s = 0; s < kFewestSets + index % kSetCountSpread; ++s) {
                        ProgramListSet set;
                        for (ProgramListTile& tile : set.tiles) {
                            fill(tile);
                        }
                        hand.sets.push_back(set);
                    }
                    Context& context = contexts.emplace_back(NativeProgramSetDeclaration());
                    context.Set(kListsLevel, hand);
                }
            }

            ListWorld(const ListWorld&) = delete;
            ListWorld& operator=(const ListWorld&) = delete;
        };

        /// Times `list_<op>` as a plain C++ loop, as the interpreter's program and through the generated function; the formula must be one of the set (tests/NativePrograms), or the
        /// last case would time the interpreter twice, and every case must sum to the plain one
        template <typename T, typename TPlain>
        void BenchListCase(const ListWorld& world, const char* type, StringView op, StringView text, TPlain&& plainRead) {
            bool printing = false;
            const uint64_t iterations = Iterations(printing);
            const String plainName = Format("list_{}_plain", op);
            const String programName = Format("list_{}_program", op);
            const String nativeName = Format("list_{}_native", op);

            const auto compiled = CompileValue<T>(text, NativeProgramSetDeclaration());
            ASSERT_TRUE(compiled.has_value()) << String(text).c_str();
            const Value<T>& value = *compiled;
            ASSERT_EQ(value.Kind(), ValueKind::Program) << String(text).c_str();

            const uint64_t plain = TimeCase(type, plainName.c_str(), iterations, printing, plainRead);
            if constexpr (!std::is_same_v<T, bool>) {
                EXPECT_NE(plain, 0U) << plainName.c_str();
            }
            {
                const NativeSwitch interpreted(false);
                EXPECT_EQ(TimeCase(type, programName.c_str(), iterations, printing, [&](size_t index) { return value.Evaluate(world.contexts[index]); }), plain) << programName.c_str();
            }
            {
                const NativeSwitch generated(true);
                ASSERT_NE(ProgramPool::Native(value.Id()), nullptr)
                    << nativeName.c_str() << ": no generated function is bound to " << String(text).c_str() << ", the set in tests/NativePrograms lacks it";
                EXPECT_EQ(TimeCase(type, nativeName.c_str(), iterations, printing, [&](size_t index) { return value.Evaluate(world.contexts[index]); }), plain) << nativeName.c_str();
            }
        }

        void BenchLists() {
            const ListWorld world;
            BenchListCase<int32_t>(world, "int32_t", "count", "count(hand.tiles, t, t.terminal)", [&](size_t index) {
                int32_t count = 0;
                for (const ProgramListTile& tile : world.hands[index].tiles) {
                    count += tile.terminal ? 1 : 0;
                }
                return count;
            });
            BenchListCase<bool>(world, "bool", "any", "any(hand.tiles, t, t.rank > 8)", [&](size_t index) {
                for (const ProgramListTile& tile : world.hands[index].tiles) {
                    if (tile.rank > kMaxRank) {
                        return true;
                    }
                }
                return false;
            });
            BenchListCase<bool>(world, "bool", "any_early", "any(hand.tiles, t, t.terminal)", [&](size_t index) {
                for (const ProgramListTile& tile : world.hands[index].tiles) {
                    if (tile.terminal) {
                        return true;
                    }
                }
                return false;
            });
            BenchListCase<int32_t>(world, "int32_t", "sum", "sum(hand.tiles, t, t.rank)", [&](size_t index) {
                int32_t sum = 0;
                for (const ProgramListTile& tile : world.hands[index].tiles) {
                    sum += tile.rank;
                }
                return sum;
            });
            BenchListCase<int32_t>(world, "int32_t", "nested", "sum(hand.sets, s, count(s.tiles, t, t.terminal))", [&](size_t index) {
                int32_t sum = 0;
                for (const ProgramListSet& set : world.hands[index].sets) {
                    for (const ProgramListTile& tile : set.tiles) {
                        sum += tile.terminal ? 1 : 0;
                    }
                }
                return sum;
            });
        }

    }  // namespace

    TEST(ValueBenchTest, Int32) {
        BenchType<int32_t>();
    }

    TEST(ValueBenchTest, Vec3) {
        BenchType<Vec3>();
    }

    TEST(ValueBenchTest, Formula) {
        BenchFormula();
    }

    TEST(ValueBenchTest, Madd) {
        BenchMadd();
    }

    TEST(ValueBenchTest, Vec3Formula) {
        BenchVec3Formula();
    }

    TEST(ValueBenchTest, Vec3Madd) {
        BenchVec3Madd();
    }

    TEST(ValueBenchTest, Mask) {
        BenchMask();
    }

    TEST(ValueBenchTest, Derived) {
        BenchDerived();
    }

    TEST(ValueBenchTest, Trigger) {
        BenchTrigger();
    }

    TEST(ValueBenchTest, Lists) {
        BenchLists();
    }

}  // namespace BECore::Tests
