// What a `Get` of a Value<T> costs against a plain field (Values/Value.h). BE_VALUE_BENCH=<iterations> prints a line per case, the form the
// benchmark set reads (`ValueBench <type> <case> ns_per_get=<x>`); without it a case is a smoke run that checks the results and asserts
// nothing about time: tests run side by side and a machine drifts, so the time belongs to the perf set (`.claude/rules/perf-project.md`).
// A formula of about five operations over two objects is timed as `formula_plain` (C++), `formula_tree` (a virtual node per operation, a heap object each),
// `formula_program` (the compiled `expr`, fused), `formula_unfused` (the same with `CompileOptions::fuse = false`) and `formula_cached` (a `CachedValue` read with nothing
// changed). `madd_plain`, `madd_program` and `madd_unfused` time `slot * k + b` alone, so what the peephole gains is one number. Every case must sum to what the plain one does.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/CachedValue.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr const char* kBenchVariable = "BE_VALUE_BENCH";
        constexpr uint64_t kSmokeIterations = 4000;
        constexpr double kNanosPerSecond = 1e9;
        /// A power of two, so the index of an iteration wraps with a mask; the objects (about 16 KB) stay in the cache whatever the case
        constexpr size_t kObjects = 64;
        constexpr size_t kObjectMask = kObjects - 1;
        constexpr LevelIndex kCasterLevel = 0;
        constexpr LevelIndex kCastLevel = 1;
        constexpr StringView kFormula = "caster.attack * (100 + cast.level * 10) / 100";
        constexpr int32_t kFormulaBase = 100;
        constexpr int32_t kFormulaPerLevel = 10;
        constexpr int32_t kCastLevels = 5;
        constexpr StringView kMadd = "caster.attack * 3 + 7";
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

        /// Runs @p read for @p iterations and returns the sum of the folded results; the time per read is printed when @p printing
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
                std::printf("ValueBench %s %s ns_per_get=%.3f\n", type, name, nanos);
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
            const char* unfused;
            const char* cached;
        };

        constexpr FormulaNames kFormulaNames{"formula_plain", "formula_tree", "formula_program", "formula_unfused", "formula_cached"};
        constexpr FormulaNames kMaddNames{"madd_plain", nullptr, "madd_program", "madd_unfused", nullptr};

        size_t CountMulAddSlot(const Program& program) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [](const Instruction& instruction) { return IsMulAddSlot(instruction.op); }));
        }

        /// Times @p text as plain C++, as a tree (when @p tree is given), as the fused program, as the program with `fuse = false` and as a cached read (with the tree), and checks that
        /// every case gives the sum of the plain one. @p expectFused says whether the default compile has a `MulAddSlot` and the unfused one never has: a benchmark of two
        /// equal programs would say nothing, and where there is no such fusion yet the two must be the same code
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
            EXPECT_EQ(TimeCase(type, names.program, iterations, printing, [&](size_t index) { return fused.Evaluate(world.contexts[index]); }), plain) << names.program;
            EXPECT_EQ(TimeCase(type, names.unfused, iterations, printing, [&](size_t index) { return unfused.Evaluate(world.contexts[index]); }), plain) << names.unfused;
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

}  // namespace BECore::Tests
