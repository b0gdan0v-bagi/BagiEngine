// What a `Get` of a Value<T> costs against a plain field (Values/Value.h). BE_VALUE_BENCH=<iterations> prints a line per case, the form the
// benchmark set reads (`ValueBench <type> <case> ns_per_get=<x>`); without it a case is a smoke run that checks the results and asserts
// nothing about time: tests run side by side and a machine drifts, so the time belongs to the perf set (`.claude/rules/perf-project.md`).
// `formula_plain` and `formula_program` time one formula as plain C++ and as a compiled `expr` run with `Evaluate`.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

        /// `kFormula` written as C++ over the two objects
        int32_t PlainFormula(const ValueUnitStats& caster, const ValueCastInfo& cast) {
            return caster.attack * (kFormulaBase + cast.level * kFormulaPerLevel) / kFormulaBase;
        }

        /// Two levels, an object of each per iteration, and the formula compiled once outside the loop
        void BenchFormula() {
            bool printing = false;
            const uint64_t iterations = Iterations(printing);
            const char* type = Subject<int32_t>::kName;

            ContextDecl declaration;
            EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
            EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
            Vector<ValueUnitStats> casters(kObjects);
            Vector<ValueCastInfo> casts(kObjects);
            Vector<Context> contexts;
            contexts.reserve(kObjects);
            for (size_t index = 0; index < kObjects; ++index) {
                Subject<int32_t>::Fill(casters[index], index);
                casts[index].level = 1 + static_cast<int32_t>(index) % kCastLevels;
                Context& context = contexts.emplace_back(declaration);
                context.Set(kCasterLevel, casters[index]);
                context.Set(kCastLevel, casts[index]);
            }

            const auto compiled = CompileValue<int32_t>(kFormula, declaration);
            ASSERT_TRUE(compiled.has_value());
            const Value<int32_t>& program = *compiled;
            ASSERT_EQ(program.Kind(), ValueKind::Program);

            const uint64_t plain = TimeCase(type, "formula_plain", iterations, printing, [&](size_t index) { return PlainFormula(casters[index], casts[index]); });
            EXPECT_NE(plain, 0U);
            EXPECT_EQ(TimeCase(type, "formula_program", iterations, printing, [&](size_t index) { return program.Evaluate(contexts[index]); }), plain) << "formula_program";
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

}  // namespace BECore::Tests
