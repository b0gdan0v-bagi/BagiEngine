// The tree of a trace (Values/Explain/ProgramTrace.h): which instruction fed which, with the value each one made. The root of every program of the native set is the value `Run` gives.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/ProgramListTypes.h>
#include <BECore/ProgramRowTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Explain/ProgramTrace.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <NativePrograms/NativeProgramFormulas.h>
#include <NativePrograms/NativeProgramSet.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;
    using namespace ProgramListData;
    using namespace ProgramRowData;
    using namespace ValueTestData;

    namespace {

        constexpr int32_t kAttack = 12;
        constexpr int32_t kLevel = 3;
        constexpr size_t kLongList = 600;

        /// The objects of the native set's declaration, with values that tell the fields apart
        struct NativeWorld {
            ValueUnitStats caster;
            ValueCastInfo cast;
            ProgramRowOperands a;
            ProgramRowOperands b;
            ProgramRowOperands c;
            ProgramListHand hand;
            Context context{NativeProgramSetDeclaration()};

            NativeWorld() {
                caster.attack = kAttack;
                cast.level = kLevel;
                a.flag = true;
                a.i32 = 2;
                b.i32 = 5;
                c.i32 = 1;
                a.narrow = ValueNarrowEnum::Mid;
                a.v3 = Vec3{1.0f, 2.0f, 3.0f};
                for (size_t i = 0; i < hand.tiles.size(); ++i) {
                    hand.tiles[i].rank = static_cast<int32_t>(i) * 2 - 5;
                }
                context.Set(kCasterLevel, caster);
                context.Set(kCastLevel, cast);
                context.Set(kOperandsLevelA, a);
                context.Set(kOperandsLevelB, b);
                context.Set(kOperandsLevelC, c);
                context.Set(kListsLevel, hand);
            }

            NativeWorld(const NativeWorld&) = delete;
            NativeWorld& operator=(const NativeWorld&) = delete;
        };

        template <typename T>
        Value<T> Compile(const ContextDecl& declaration, StringView text, bool fuse = true) {
            const auto compiled = CompileValue<T>(text, declaration, CompileOptions{.tables = &NativeProgramSetTables(), .fuse = fuse});
            EXPECT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? "" : compiled.error().message.c_str());
            EXPECT_TRUE(compiled.has_value() && compiled->Kind() == ValueKind::Program) << String(text).c_str();
            return compiled.has_value() ? *compiled : Value<T>{};
        }

        const Program& ProgramOf(const Value<int32_t>& value) {
            return ProgramPool::Get(value.Id());
        }

        const TraceNode& Root(const ProgramTrace& trace) {
            EXPECT_NE(trace.root, kNoTraceNode);
            return trace.nodes[trace.root != kNoTraceNode ? trace.root : 0];
        }

        const TraceNode& ChildAt(const ProgramTrace& trace, const TraceNode& node, size_t index) {
            const Span<const uint32_t> children = trace.Children(node);
            EXPECT_LT(index, children.size());
            return trace.nodes[children[index < children.size() ? index : 0]];
        }

        /// An index into `at(i, 10, 20, 30, 40)`, the row it takes and the entry
        struct TableCase {
            int32_t level;
            uint32_t row;
            int32_t expected;
        };

        bool StartsWith(const String& text, StringView prefix) {
            return text.size() >= prefix.size() && StringView(text.data(), prefix.size()) == prefix;
        }

        int32_t IntOf(const TraceNode& node) {
            return Kernel::Load<int32_t>(node.image);
        }

        size_t CountKind(const ProgramTrace& trace, TraceStepKind kind) {
            size_t count = 0;
            for (const TraceNode& node : trace.nodes) {
                count += node.kind == kind ? 1 : 0;
            }
            return count;
        }

        const TraceNode* FindLabel(const ProgramTrace& trace, StringView label) {
            for (const TraceNode& node : trace.nodes) {
                if (node.label == label) {
                    return &node;
                }
            }
            return nullptr;
        }

        /// A node's children are made before it, so a walk from the front sees an operand before its use
        void ExpectChildrenPrecede(const ProgramTrace& trace, StringView what) {
            for (size_t i = 0; i < trace.nodes.size(); ++i) {
                for (const uint32_t child : trace.Children(trace.nodes[i])) {
                    EXPECT_LT(child, i) << String(what).c_str();
                }
            }
        }

        /// A constant has no type of its own: it is the type of the instruction that reads it, which the operands of these operations share with the result
        void ExpectOperandTypesAgree(const ProgramTrace& trace, StringView what) {
            for (const TraceNode& node : trace.nodes) {
                if (!node.operation.has_value() || node.kind != TraceStepKind::Operation) {
                    continue;
                }
                const Span<const uint32_t> children = trace.Children(node);
                switch (*node.operation) {
                    case ProgramOperation::Add:
                    case ProgramOperation::Sub:
                    case ProgramOperation::Div:
                    case ProgramOperation::Mod:
                    case ProgramOperation::Min:
                    case ProgramOperation::Max:
                    case ProgramOperation::Clamp:
                    case ProgramOperation::Neg:
                    case ProgramOperation::Abs:
                        // A vector divided by its scalar is a mixed row
                        if (node.type != ProgramType::Int32 && node.type != ProgramType::Int64 && node.type != ProgramType::Float && node.type != ProgramType::Double) {
                            break;
                        }
                        for (const uint32_t child : children) {
                            EXPECT_EQ(trace.nodes[child].type, node.type) << String(what).c_str() << ": " << node.label.c_str();
                        }
                        break;
                    case ProgramOperation::Eq:
                    case ProgramOperation::Ne:
                    case ProgramOperation::Lt:
                    case ProgramOperation::Gt:
                        ASSERT_EQ(children.size(), 2U);
                        EXPECT_EQ(trace.nodes[children[0]].type, trace.nodes[children[1]].type) << String(what).c_str() << ": " << node.label.c_str();
                        break;
                    case ProgramOperation::Select:
                        ASSERT_EQ(children.size(), 3U);
                        EXPECT_EQ(trace.nodes[children[0]].type, ProgramType::Bool) << String(what).c_str();
                        EXPECT_EQ(trace.nodes[children[1]].type, node.type) << String(what).c_str();
                        EXPECT_EQ(trace.nodes[children[2]].type, node.type) << String(what).c_str();
                        break;
                    default:
                        break;
                }
            }
        }

        /// The root of the trace and the result are the bytes `Run` gives, whether or not fused and whatever the program computes
        template <typename T, size_t N>
        void ExpectRootIsRun(const StringView (&formulas)[N], const NativeWorld& world) {
            for (const bool fuse : {false, true}) {
                for (const StringView text : formulas) {
                    const Value<T> value = Compile<T>(NativeProgramSetDeclaration(), text, fuse);
                    const Program& program = ProgramPool::Get(value.Id());
                    RegisterImage plain{};
                    Run(program, world.context, plain.bytes.data());

                    const ProgramTrace trace = ExplainProgram(program, world.context, NativeProgramSetDeclaration());
                    const String what = Format("{} ({})", text, fuse ? "fused" : "unfused");
                    EXPECT_EQ(trace.result, plain) << what.c_str();
                    EXPECT_FALSE(trace.truncated) << what.c_str();
                    ASSERT_NE(trace.root, kNoTraceNode) << what.c_str();
                    EXPECT_EQ(trace.nodes[trace.root].image, plain) << what.c_str();
                    EXPECT_EQ(trace.nodes[trace.root].type, program.ResultType()) << what.c_str();
                    ExpectChildrenPrecede(trace, what);
                    ExpectOperandTypesAgree(trace, what);
                }
            }
        }

    }  // namespace

    TEST(ValueTraceTest, TheRootOfEveryProgramOfTheNativeSetIsTheValueRunGives) {
        static_cast<void>(RegisterDomainToys());
        const NativeWorld world;
        ExpectRootIsRun<int32_t>(kNativeIntFormulas, world);
        ExpectRootIsRun<int64_t>(kNativeInt64Formulas, world);
        ExpectRootIsRun<float>(kNativeFloatFormulas, world);
        ExpectRootIsRun<double>(kNativeDoubleFormulas, world);
        ExpectRootIsRun<bool>(kNativeBoolFormulas, world);
        ExpectRootIsRun<Vec2>(kNativeVec2Formulas, world);
        ExpectRootIsRun<Vec3>(kNativeVec3Formulas, world);
        ExpectRootIsRun<Vec4>(kNativeVec4Formulas, world);
        ExpectRootIsRun<IVec2>(kNativeIVec2Formulas, world);
        ExpectRootIsRun<Quat>(kNativeQuatFormulas, world);
        ExpectRootIsRun<Color>(kNativeColorFormulas, world);
        ExpectRootIsRun<ValueNarrowEnum>(kNativeNarrowEnumFormulas, world);
        ExpectRootIsRun<ValueShortEnum>(kNativeShortEnumFormulas, world);
        ExpectRootIsRun<ValueWideEnum>(kNativeWideEnumFormulas, world);
        ExpectRootIsRun<PoolString>(kNativeTextFormulas, world);
        ExpectRootIsRun<bool>(kNativeDomainBoolFormulas, world);
        ExpectRootIsRun<int32_t>(kNativeDomainIntFormulas, world);
        ExpectRootIsRun<DomainBits256>(kNativeDomainMaskFormulas, world);
        ExpectRootIsRun<DomainPair>(kNativeDomainPairFormulas, world);
        ExpectRootIsRun<int32_t>(kNativeListIntFormulas, world);
        ExpectRootIsRun<int64_t>(kNativeListInt64Formulas, world);
        ExpectRootIsRun<float>(kNativeListFloatFormulas, world);
        ExpectRootIsRun<double>(kNativeListDoubleFormulas, world);
        ExpectRootIsRun<bool>(kNativeListBoolFormulas, world);
    }

    TEST(ValueTraceTest, AnExpressionBecomesItsOperationsOverTheFieldsAndConstantsItReads) {
        const ValueWorld world;
        const Value<int32_t> value = Compile<int32_t>(world.declaration, "caster.attack * 2 + cast.level", false);
        const ProgramTrace trace = ExplainProgram(ProgramOf(value), world.context, world.declaration);

        const TraceNode& add = Root(trace);
        EXPECT_EQ(add.kind, TraceStepKind::Operation);
        EXPECT_EQ(add.operation, ProgramOperation::Add);
        EXPECT_EQ(IntOf(add), value.Evaluate(world.context));
        ASSERT_EQ(add.childCount, 2U);
        const TraceNode& mul = ChildAt(trace, add, 0);
        EXPECT_EQ(mul.operation, ProgramOperation::Mul);
        EXPECT_EQ(IntOf(mul), kAttack * 2);
        const TraceNode& level = ChildAt(trace, add, 1);
        EXPECT_EQ(level.kind, TraceStepKind::ContextKey);
        EXPECT_EQ(level.label, "cast.level");
        EXPECT_EQ(IntOf(level), world.cast.level);
        ASSERT_EQ(mul.childCount, 2U);
        EXPECT_EQ(ChildAt(trace, mul, 0).label, "caster.attack");
        EXPECT_EQ(ChildAt(trace, mul, 1).kind, TraceStepKind::Constant);
        EXPECT_EQ(IntOf(ChildAt(trace, mul, 1)), 2);

        // The tree reads the context as it is now: the same program, another value
        ValueWorld other;
        other.caster.attack = 100;
        EXPECT_EQ(IntOf(Root(ExplainProgram(ProgramOf(value), other.context, other.declaration))), 100 * 2 + other.cast.level);
    }

    TEST(ValueTraceTest, TheTreeIsWrittenOneNodePerLineIndentedUnderItsParent) {
        const ValueWorld world;
        const Value<int32_t> value = Compile<int32_t>(world.declaration, "caster.attack * 2 + cast.level", false);
        const ProgramTrace trace = ExplainProgram(ProgramOf(value), world.context, world.declaration);
        EXPECT_EQ(FormatTrace(trace), "Add = 27\n"
                                      "  Mul = 24\n"
                                      "    caster.attack = 12\n"
                                      "    constant = 2\n"
                                      "  cast.level = 3\n");
    }

    TEST(ValueTraceTest, AFusedInstructionIsOneNodeOverTheFieldAndTheConstantsItHolds) {
        const ValueWorld world;
        const Value<int32_t> unfused = Compile<int32_t>(world.declaration, "caster.attack * 3 + 7", false);
        const Value<int32_t> fused = Compile<int32_t>(world.declaration, "caster.attack * 3 + 7", true);
        ASSERT_LT(ProgramOf(fused).Code().size(), ProgramOf(unfused).Code().size()) << "the pass fused nothing";
        const ProgramTrace trace = ExplainProgram(ProgramOf(fused), world.context, world.declaration);

        const TraceNode& root = Root(trace);
        EXPECT_EQ(root.kind, TraceStepKind::Operation);
        EXPECT_TRUE(IsMulAddSlot(root.opcode));
        EXPECT_EQ(IntOf(root), kAttack * 3 + 7);
        ASSERT_EQ(root.childCount, 3U);
        EXPECT_EQ(ChildAt(trace, root, 0).label, "caster.attack");
        EXPECT_EQ(IntOf(ChildAt(trace, root, 0)), kAttack);
        EXPECT_EQ(IntOf(ChildAt(trace, root, 1)), 3);
        EXPECT_EQ(IntOf(ChildAt(trace, root, 2)), 7);
        EXPECT_EQ(IntOf(root), unfused.Evaluate(world.context));
    }

    TEST(ValueTraceTest, ALerpOfTwoFieldsKeepsTheWeightAsItsThirdChild) {
        NativeWorld world;
        world.a.f32 = 2.0f;
        world.b.f32 = 6.0f;
        const Value<float> value = Compile<float>(NativeProgramSetDeclaration(), "lerp(a.f32, b.f32, 0.25)", true);
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, NativeProgramSetDeclaration());
        const TraceNode& root = Root(trace);
        EXPECT_EQ(root.operation, ProgramOperation::Lerp);
        ASSERT_EQ(root.childCount, 3U);
        EXPECT_EQ(ChildAt(trace, root, 0).label, "a.f32");
        EXPECT_EQ(ChildAt(trace, root, 1).label, "b.f32");
        EXPECT_EQ(Kernel::Load<float>(ChildAt(trace, root, 2).image), 0.25f);
        EXPECT_EQ(Kernel::Load<float>(root.image), 3.0f);
    }

    TEST(ValueTraceTest, ATableNodeSaysTheRowTakenAndHowManyThereAre) {
        ValueWorld world;
        for (const bool fuse : {false, true}) {
            const Value<int32_t> value = Compile<int32_t>(world.declaration, "at(cast.level, 10, 20, 30, 40)", fuse);
            for (const TableCase& tableCase : {TableCase{1, 1, 20}, TableCase{-4, 0, 10}, TableCase{9, 3, 40}}) {
                const auto [level, row, expected] = tableCase;
                world.cast.level = level;
                const ProgramTrace trace = ExplainProgram(ProgramOf(value), world.context, world.declaration);
                const TraceNode& table = Root(trace);
                EXPECT_EQ(table.kind, TraceStepKind::Table) << fuse;
                EXPECT_EQ(table.label, Format("table row {} of 4", row)) << fuse;
                EXPECT_EQ(IntOf(table), expected);
                ASSERT_EQ(table.childCount, 1U);
                EXPECT_EQ(ChildAt(trace, table, 0).label, "cast.level");
                EXPECT_EQ(IntOf(ChildAt(trace, table, 0)), level);
            }
        }
    }

    TEST(ValueTraceTest, ByLevelIsATableOverItsIndex) {
        NativeWorld world;
        const Value<int32_t> value = Compile<int32_t>(NativeProgramSetDeclaration(), "ByLevel('NativeDamage', cast.level) + caster.attack");
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, NativeProgramSetDeclaration());
        ASSERT_EQ(CountKind(trace, TraceStepKind::Table), 1U);
        EXPECT_EQ(IntOf(Root(trace)), value.Evaluate(world.context));
    }

    TEST(ValueTraceTest, AnEnumIsPrintedByItsNamesOnBothSidesOfAComparison) {
        ValueWorld world;
        const Value<bool> value = Compile<bool>(world.declaration, "caster.element == Water");
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, world.declaration);
        EXPECT_EQ(FormatTrace(trace), "Eq = true\n"
                                      "  caster.element = Water\n"
                                      "  constant = Water\n");

        world.caster.element = ValueElement::Fire;
        EXPECT_EQ(FormatTrace(ExplainProgram(ProgramPool::Get(value.Id()), world.context, world.declaration)), "Eq = false\n"
                                                                                                               "  caster.element = Fire\n"
                                                                                                               "  constant = Water\n");
    }

    TEST(ValueTraceTest, ASelectOfEnumsKeepsTheNamesOfItsResult) {
        const NativeWorld world;
        const Value<ValueNarrowEnum> value = Compile<ValueNarrowEnum>(NativeProgramSetDeclaration(), "select(a.flag, a.narrow, High)");
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, NativeProgramSetDeclaration());
        ASSERT_NE(trace.resultDescribed, nullptr);
        EXPECT_EQ(FormatImage(trace.resultType, trace.result, trace.resultDescribed), "Mid");
    }

    TEST(ValueTraceTest, ALoopIsAListNodeUnderTheReadsOfItsElements) {
        NativeWorld world;
        const Value<int32_t> value = Compile<int32_t>(NativeProgramSetDeclaration(), "sum(hand.tiles, t, t.rank)");
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, NativeProgramSetDeclaration());

        EXPECT_FALSE(trace.truncated);
        ASSERT_EQ(CountKind(trace, TraceStepKind::List), 1U);
        const TraceNode* list = FindLabel(trace, "hand.tiles");
        ASSERT_NE(list, nullptr);
        EXPECT_EQ(FormatNodeValue(*list), Format("{} elements", world.hand.tiles.size()));
        int32_t sum = 0;
        for (size_t i = 0; i < world.hand.tiles.size(); ++i) {
            const TraceNode* element = FindLabel(trace, Format("hand.tiles[{}].rank", i));
            ASSERT_NE(element, nullptr) << i;
            EXPECT_EQ(element->kind, TraceStepKind::ContextKey);
            EXPECT_EQ(IntOf(*element), world.hand.tiles[i].rank);
            ASSERT_EQ(element->childCount, 1U);
            EXPECT_EQ(trace.nodes[trace.Children(*element)[0]].kind, TraceStepKind::List);
            sum += world.hand.tiles[i].rank;
        }
        EXPECT_EQ(IntOf(Root(trace)), sum);
    }

    TEST(ValueTraceTest, TheListOfAnElementAndAnIndexedElementAreNamedAfterTheirPlace) {
        NativeWorld world;
        world.hand.sets.resize(2);
        world.hand.sets[1].tiles[2].terminal = true;
        const Value<int32_t> nested = Compile<int32_t>(NativeProgramSetDeclaration(), "sum(hand.sets, s, count(s.tiles, t, t.terminal))");
        const ProgramTrace nestedTrace = ExplainProgram(ProgramPool::Get(nested.Id()), world.context, NativeProgramSetDeclaration());
        EXPECT_NE(FindLabel(nestedTrace, "hand.sets[0].tiles"), nullptr);
        EXPECT_NE(FindLabel(nestedTrace, "hand.sets[1].tiles"), nullptr);
        EXPECT_NE(FindLabel(nestedTrace, "hand.sets[1].tiles[2].terminal"), nullptr);
        EXPECT_EQ(IntOf(Root(nestedTrace)), 1);

        // a.i32 is 2
        const Value<int32_t> indexed = Compile<int32_t>(NativeProgramSetDeclaration(), "hand.tiles[a.i32].rank");
        const ProgramTrace indexedTrace = ExplainProgram(ProgramPool::Get(indexed.Id()), world.context, NativeProgramSetDeclaration());
        const TraceNode* element = FindLabel(indexedTrace, "hand.tiles[2].rank");
        ASSERT_NE(element, nullptr);
        EXPECT_EQ(IntOf(*element), world.hand.tiles[2].rank);
    }

    TEST(ValueTraceTest, ALoopOverALongListIsCutAtTheCapAndTheValueIsStillRight) {
        NativeWorld world;
        world.hand.none.resize(kLongList);
        int32_t sum = 0;
        for (size_t i = 0; i < kLongList; ++i) {
            world.hand.none[i].rank = static_cast<int32_t>(i % 7);
            sum += world.hand.none[i].rank;
        }
        const Value<int32_t> value = Compile<int32_t>(NativeProgramSetDeclaration(), "sum(hand.none, t, t.rank)");
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, NativeProgramSetDeclaration());

        EXPECT_TRUE(trace.truncated);
        EXPECT_EQ(trace.nodes.size(), kMaxTraceNodes);
        EXPECT_EQ(Kernel::Load<int32_t>(trace.result), sum);
        EXPECT_EQ(sum, value.Evaluate(world.context));
        EXPECT_EQ(IntOf(Root(trace)), sum);
        ExpectChildrenPrecede(trace, "cut");
        const String text = FormatTrace(trace);
        EXPECT_NE(text.find("the trace stops at 512 nodes"), String::npos);
    }

    TEST(ValueTraceTest, ACallOfAGamesOperationCarriesItsNameAndItsArguments) {
        static_cast<void>(RegisterDomainToys());
        const NativeWorld world;
        const Value<int32_t> value = Compile<int32_t>(NativeProgramSetDeclaration(), "popcount(shift(a.mask, South)) + a.i32 % 7");
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, NativeProgramSetDeclaration());

        const TraceNode* shift = FindLabel(trace, "shift");
        ASSERT_NE(shift, nullptr);
        EXPECT_EQ(shift->kind, TraceStepKind::Call);
        ASSERT_EQ(shift->childCount, 2U);
        const TraceNode& mask = ChildAt(trace, *shift, 0);
        EXPECT_EQ(mask.label, "a.mask");
        EXPECT_TRUE(StartsWith(FormatNodeValue(mask), "<mask 32 bytes: ")) << FormatNodeValue(mask).c_str();
        EXPECT_EQ(FormatNodeValue(ChildAt(trace, *shift, 1)), "South");
        const TraceNode* popcount = FindLabel(trace, "popcount");
        ASSERT_NE(popcount, nullptr);
        EXPECT_EQ(popcount->childCount, 1U);
        EXPECT_EQ(IntOf(Root(trace)), value.Evaluate(world.context));
    }

    TEST(ValueTraceTest, AComponentReadIsNamedAfterItsField) {
        NativeWorld world;
        world.a.v3 = Vec3{1.0f, 2.5f, 3.0f};
        const Value<float> value = Compile<float>(NativeProgramSetDeclaration(), "a.v3.y * 2");
        const ProgramTrace trace = ExplainProgram(ProgramPool::Get(value.Id()), world.context, NativeProgramSetDeclaration());
        const TraceNode* component = FindLabel(trace, "a.v3.y");
        ASSERT_NE(component, nullptr);
        EXPECT_EQ(component->type, ProgramType::Float);
        EXPECT_EQ(Kernel::Load<float>(component->image), 2.5f);
    }

    TEST(ValueTraceTest, ADeclarationWithFewerLevelsThanTheProgramReadsNamesThePlaceInstead) {
        const ValueWorld world;
        const Value<int32_t> value = Compile<int32_t>(world.declaration, "caster.attack * 2 + cast.level", false);
        const ContextDecl none;
        const ProgramTrace trace = ExplainProgram(ProgramOf(value), world.context, none);

        EXPECT_EQ(IntOf(Root(trace)), value.Evaluate(world.context));
        EXPECT_EQ(CountKind(trace, TraceStepKind::ContextKey), 2U);
        size_t named = 0;
        for (const TraceNode& node : trace.nodes) {
            named += node.kind == TraceStepKind::ContextKey && (StartsWith(node.label, "level 0 +") || StartsWith(node.label, "level 1 +")) ? 1 : 0;
        }
        EXPECT_EQ(named, 2U);

        ContextDecl first;
        static_cast<void>(first.Add<ValueUnitStats>("caster"));
        const ProgramTrace half = ExplainProgram(ProgramOf(value), world.context, first);
        EXPECT_NE(FindLabel(half, "caster.attack"), nullptr);
        EXPECT_EQ(CountKind(half, TraceStepKind::ContextKey), 2U);
    }

    TEST(ValueTraceTest, AContextWithoutAnObjectForALevelTheProgramReadsGivesNoTraceAndSaysWhich) {
        const ValueWorld world;
        const Value<int32_t> value = Compile<int32_t>(world.declaration, "caster.attack * 2 + cast.level", false);
        Context partial(world.declaration);
        partial.Set(0, world.caster);
        const ProgramTrace trace = ExplainProgram(ProgramOf(value), partial, world.declaration);
        EXPECT_TRUE(trace.nodes.empty());
        EXPECT_EQ(trace.root, kNoTraceNode);
        ASSERT_TRUE(trace.missingLevel.has_value());
        EXPECT_EQ(*trace.missingLevel, 1);
        EXPECT_TRUE(FormatTrace(trace).empty());
    }

    TEST(ValueTraceTest, ImagesArePrintedAsAPersonWritesThem) {
        RegisterImage image{};
        const auto put = [&image](auto value) { std::memcpy(image.bytes.data(), &value, sizeof(value)); };
        put(true);
        EXPECT_EQ(FormatImage(ProgramType::Bool, image), "true");
        put(Vec3{1.0f, 2.5f, -3.0f});
        EXPECT_EQ(FormatImage(ProgramType::Vec3, image), "(1, 2.5, -3)");
        put(IVec2{4, -5});
        EXPECT_EQ(FormatImage(ProgramType::IVec2, image), "(4, -5)");
        put(Color{1, 2, 3, 4});
        EXPECT_EQ(FormatImage(ProgramType::Color, image), "(1, 2, 3, 4)");
        put(PoolString("alpha"_intern));
        EXPECT_EQ(FormatImage(ProgramType::String, image), "\"alpha\"");
        put(ProgramEnum{2});
        EXPECT_EQ(FormatImage(ProgramType::Enum, image), "#2");
        EXPECT_EQ(FormatImage(ProgramType::Enum, image, &DescribeType<ValueElement>()), "Earth");
        put(ProgramEnum{9});
        EXPECT_EQ(FormatImage(ProgramType::Enum, image, &DescribeType<ValueElement>()), "#9");
    }

}  // namespace BECore::Tests
