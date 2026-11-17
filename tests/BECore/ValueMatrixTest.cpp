// Value<T> over every value type (Values/Value.h, ValueAccessor.h): the same checks for a Const and a Slot of each type, through XML and
// binary, and for every way a link is refused. The context objects are those of ValueTestTypes.h (generated code in ValueContextTest.cpp).

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Threading/Thread.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueLinkScope.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr StringView kState = "State";
        constexpr StringView kKey = "v";
        constexpr int kLoadingThreads = 2;
        constexpr auto kRendezvousLimit = std::chrono::seconds(10);

        /// Per value type: the field of `ValueUnitStats` a slot reads, a literal and another one (both unlike the field), and the field of another type
        template <typename T>
        struct MatrixCase;

#define BE_MATRIX_CASE(Type, field, wrongField, literal, other)                                                                                                                                        \
    template <>                                                                                                                                                                                        \
    struct MatrixCase<Type> {                                                                                                                                                                          \
        static constexpr StringView kName = #Type;                                                                                                                                                     \
        static constexpr StringView kField = #field;                                                                                                                                                   \
        static constexpr StringView kPath = "caster." #field;                                                                                                                                          \
        static constexpr StringView kWrongPath = "caster." #wrongField;                                                                                                                                \
        static constexpr StringView kWrongField = #wrongField;                                                                                                                                         \
        static constexpr auto kMember = &ValueUnitStats::field;                                                                                                                                        \
        static Type Literal() {                                                                                                                                                                        \
            return literal;                                                                                                                                                                            \
        }                                                                                                                                                                                              \
        static Type Other() {                                                                                                                                                                          \
            return other;                                                                                                                                                                              \
        }                                                                                                                                                                                              \
    };

        BE_MATRIX_CASE(bool, alive, attack, false, true)
        BE_MATRIX_CASE(int32_t, attack, speed, 5, 6)
        BE_MATRIX_CASE(uint32_t, count, attack, 5U, 6U)
        BE_MATRIX_CASE(int64_t, score, attack, 7, 8)
        BE_MATRIX_CASE(float, speed, precise, 0.5f, 0.75f)
        BE_MATRIX_CASE(double, precise, speed, 0.5, 0.75)
        BE_MATRIX_CASE(PoolString, title, attack, PoolString::Intern("Hero"), PoolString::Intern("Mage"))
        BE_MATRIX_CASE(ValueElement, element, attack, ValueElement::Fire, ValueElement::Earth)
        BE_MATRIX_CASE(Vec2, position, cell, (Vec2{7.0f, 8.0f}), (Vec2{8.0f, 7.0f}))
        BE_MATRIX_CASE(IVec2, cell, position, (IVec2{7, 8}), (IVec2{8, 7}))
        BE_MATRIX_CASE(Vec3, velocity, position, (Vec3{4.0f, 5.0f, 6.0f}), (Vec3{6.0f, 5.0f, 4.0f}))
        BE_MATRIX_CASE(Vec4, plane, facing, (Vec4{1.0f, 2.0f, 3.0f, 4.0f}), (Vec4{4.0f, 3.0f, 2.0f, 1.0f}))
        BE_MATRIX_CASE(Quat, facing, plane, (Quat{0.0f, 1.0f, 0.0f, 0.0f}), (Quat{0.0f, 0.0f, 1.0f, 0.0f}))
        BE_MATRIX_CASE(Rect, bounds, plane, (Rect{5.0f, 6.0f, 7.0f, 8.0f}), (Rect{8.0f, 7.0f, 6.0f, 5.0f}))
        BE_MATRIX_CASE(Color, tint, bounds, (Color{50, 60, 70, 80}), (Color{80, 70, 60, 50}))
        BE_MATRIX_CASE(ValuePoint, spawn, cell, (ValuePoint{1, 2}), (ValuePoint{2, 1}))

#undef BE_MATRIX_CASE

        /// Rect and Color have no operator==, and a literal is copied bit by bit, so bytes are the comparison
        template <typename T>
        bool SameBytes(const T& a, const T& b) {
            return std::memcmp(&a, &b, sizeof(T)) == 0;
        }

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// The one field of a document: the value under test, or the plain field the same file would fill
        template <typename T>
        struct ValueHolder {
            Value<T> value;

            template <typename Serializer>
            void Serialize(Serializer& s) const {
                Ser::DataAccessor<Value<T>>::Save(s, value, kKey);
            }

            template <typename Deserializer>
            void Deserialize(Deserializer& d) {
                Ser::DataAccessor<Value<T>>::Load(d, value, kKey);
            }
        };

        template <typename T>
        struct PlainHolder {
            T value{};

            template <typename Serializer>
            void Serialize(Serializer& s) const {
                Ser::DataAccessor<T>::Save(s, value, kKey);
            }

            template <typename Deserializer>
            void Deserialize(Deserializer& d) {
                Ser::DataAccessor<T>::Load(d, value, kKey);
            }
        };

        template <typename Holder>
        String SaveXml(const Holder& holder) {
            XmlSerializer serializer;
            if (serializer.BeginObject(kState)) {
                holder.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        template <typename Holder>
        Vector<uint8_t> SaveBinary(const Holder& holder) {
            BinarySerializer serializer;
            if (serializer.BeginObject(kState)) {
                holder.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        String ErrorsOf(const IDeserializer& reader) {
            String out;
            for (const DeserializeError& report : reader.GetErrors()) {
                AppendFormat(out, "{}: {}\n", report.path, report.errorMessage);
            }
            return out;
        }

        /// Loads a document of `State` into the holder; the declaration, when given, is the link scope of the load. Returns what the reader reported
        template <typename Holder>
        String LoadXml(StringView xml, Holder& holder, const ContextDecl* declaration) {
            XmlDeserializer reader;
            EXPECT_TRUE(reader.LoadFromString(String(xml))) << String(xml).c_str();
            EXPECT_TRUE(reader.BeginObject(kState));
            Optional<ValueLinkScope> scope;
            if (declaration != nullptr) {
                scope.emplace(*declaration);
            }
            holder.Deserialize(reader);
            reader.EndObject();
            return ErrorsOf(reader);
        }

        template <typename Holder>
        String LoadBinary(const Vector<uint8_t>& data, Holder& holder, const ContextDecl* declaration) {
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromBuffer(data));
            EXPECT_TRUE(reader.BeginObject(kState));
            Optional<ValueLinkScope> scope;
            if (declaration != nullptr) {
                scope.emplace(*declaration);
            }
            holder.Deserialize(reader);
            reader.EndObject();
            return ErrorsOf(reader);
        }

        /// The document a slot of @p path saves as
        template <typename T>
        ValueHolder<T> SlotHolder(const ValueWorld& world, StringView path) {
            ValueHolder<T> holder;
            const auto linked = holder.value.Link(world.declaration, path);
            EXPECT_TRUE(linked.has_value()) << (linked.has_value() ? String{} : linked.error().message).c_str();
            return holder;
        }

        /// A holder that starts with a literal no load of the matrix produces, so a field a load left is seen
        template <typename T>
        ValueHolder<T> PreloadedHolder() {
            ValueHolder<T> holder;
            holder.value = Value<T>::Const(MatrixCase<T>::Other());
            return holder;
        }

        /// Rect and Color have no operator==, so a Value of them has none either: a slot is the same when it reads the same place
        template <typename T>
        bool SameSlot(const Value<T>& a, const Value<T>& b) {
            return a.Kind() == b.Kind() && a.Level() == b.Level() && a.Offset() == b.Offset() && a.Path() == b.Path();
        }

        template <typename T>
        void ExpectUntouched(const ValueHolder<T>& holder) {
            EXPECT_TRUE(holder.value.IsConst());
            EXPECT_TRUE(SameBytes(holder.value.GetConst(), MatrixCase<T>::Other()));
        }

        template <typename T>
        void ExpectReadsTheMember(const ValueHolder<T>& holder, const ValueWorld& world) {
            ASSERT_TRUE(holder.value.IsSlot());
            EXPECT_EQ(holder.value.Path(), PoolString::Intern(MatrixCase<T>::kPath));
            const T& member = world.caster.*MatrixCase<T>::kMember;
            EXPECT_EQ(static_cast<const void*>(&holder.value.Get(world.context)), static_cast<const void*>(&member));
        }

        /// The type as a link message says it, enough to find it in the text
        String TypeText(const TypeDesc& type) {
            switch (type.kind) {
                case TypeKind::Opaque:
                    return String(type.opaqueName);
                case TypeKind::Object:
                    return String(type.ObjectName());
                case TypeKind::Enum:
                    return "enum {";
                default:
                    return String(TypeKindName(type.kind));
            }
        }

        struct MatrixTypeNames {
            template <typename T>
            static std::string GetName(int) {                                                  //-NoStd: the name of a gtest case
                return std::string(MatrixCase<T>::kName.data(), MatrixCase<T>::kName.size());  //-NoStd: the name of a gtest case
            }
        };

    }  // namespace

    template <typename T>
    class ValueMatrixTest : public ::testing::Test {};

    using MatrixTypes = ::testing::Types<bool, int32_t, uint32_t, int64_t, float, double, PoolString, ValueElement, Vec2, IVec2, Vec3, Vec4, Quat, Rect, Color, ValuePoint>;
    TYPED_TEST_SUITE(ValueMatrixTest, MatrixTypes, MatrixTypeNames);

    // =========================================================================
    // Get
    // =========================================================================

    TYPED_TEST(ValueMatrixTest, TheCasesAreDistinct) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        EXPECT_FALSE(SameBytes(Case::Literal(), Case::Other()));
        EXPECT_FALSE(SameBytes(Case::Literal(), world.caster.*Case::kMember));
    }

    TYPED_TEST(ValueMatrixTest, AConstGetsItsLiteralWhateverTheContextHolds) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        const Value<TypeParam> value = Value<TypeParam>::Const(Case::Literal());
        EXPECT_TRUE(value.IsConst());
        EXPECT_TRUE(SameBytes(value.Get(world.context), Case::Literal()));
        EXPECT_TRUE(SameBytes(value.Get(Context{}), Case::Literal()));
    }

    TYPED_TEST(ValueMatrixTest, ASlotGetsTheContextFieldInPlace) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        const ValueHolder<TypeParam> holder = SlotHolder<TypeParam>(world, Case::kPath);
        ExpectReadsTheMember(holder, world);
        EXPECT_TRUE(SameBytes(holder.value.Get(world.context), world.caster.*Case::kMember));
    }

    // =========================================================================
    // XML and binary
    // =========================================================================

    TYPED_TEST(ValueMatrixTest, AConstRoundTripsThroughXml) {
        using Case = MatrixCase<TypeParam>;
        ValueHolder<TypeParam> saved;
        saved.value = Value<TypeParam>::Const(Case::Literal());
        const String xml = SaveXml(saved);

        ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
        const String errors = LoadXml(xml, loaded, nullptr);
        EXPECT_TRUE(errors.empty()) << errors.c_str() << xml.c_str();
        ASSERT_TRUE(loaded.value.IsConst()) << xml.c_str();
        EXPECT_TRUE(SameBytes(loaded.value.GetConst(), Case::Literal())) << xml.c_str();
        EXPECT_EQ(SaveXml(loaded), xml);
    }

    TYPED_TEST(ValueMatrixTest, ASlotRoundTripsThroughXml) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        const ValueHolder<TypeParam> saved = SlotHolder<TypeParam>(world, Case::kPath);
        const String xml = SaveXml(saved);
        EXPECT_TRUE(HasText(xml, "from=")) << xml.c_str();
        EXPECT_TRUE(HasText(xml, Case::kPath)) << xml.c_str();

        ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
        const String errors = LoadXml(xml, loaded, &world.declaration);
        EXPECT_TRUE(errors.empty()) << errors.c_str() << xml.c_str();
        ExpectReadsTheMember(loaded, world);
        EXPECT_TRUE(SameSlot(loaded.value, saved.value));
        EXPECT_EQ(SaveXml(loaded), xml);
    }

    TYPED_TEST(ValueMatrixTest, AConstRoundTripsThroughBinary) {
        using Case = MatrixCase<TypeParam>;
        ValueHolder<TypeParam> saved;
        saved.value = Value<TypeParam>::Const(Case::Literal());
        const Vector<uint8_t> data = SaveBinary(saved);

        ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
        const String errors = LoadBinary(data, loaded, nullptr);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        ASSERT_TRUE(loaded.value.IsConst());
        EXPECT_TRUE(SameBytes(loaded.value.GetConst(), Case::Literal()));
    }

    TYPED_TEST(ValueMatrixTest, ASlotRoundTripsThroughBinary) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        const ValueHolder<TypeParam> saved = SlotHolder<TypeParam>(world, Case::kPath);
        const Vector<uint8_t> data = SaveBinary(saved);

        ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
        const String errors = LoadBinary(data, loaded, &world.declaration);
        EXPECT_TRUE(errors.empty()) << errors.c_str();
        ExpectReadsTheMember(loaded, world);
        EXPECT_TRUE(SameSlot(loaded.value, saved.value));
    }

    TYPED_TEST(ValueMatrixTest, ASlotInBinaryNeedsAScopeAndKeepsTheField) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        const Vector<uint8_t> data = SaveBinary(SlotHolder<TypeParam>(world, Case::kPath));

        ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
        const String errors = LoadBinary(data, loaded, nullptr);
        EXPECT_FALSE(errors.empty());
        EXPECT_TRUE(HasText(errors, Case::kPath)) << errors.c_str();
        ExpectUntouched(loaded);
    }

    TYPED_TEST(ValueMatrixTest, APlainAttributeFileLoadsAsItDidBeforeTheFieldWasAValue) {
        using Case = MatrixCase<TypeParam>;
        PlainHolder<TypeParam> plainSaved;
        plainSaved.value = Case::Literal();
        const String xml = SaveXml(plainSaved);

        ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
        PlainHolder<TypeParam> plain;
        const ValueWorld world;
        // With a scope and without: a document that has no `from` reads the same
        for (const ContextDecl* declaration : {static_cast<const ContextDecl*>(nullptr), &world.declaration}) {
            loaded = PreloadedHolder<TypeParam>();
            const String errors = LoadXml(xml, loaded, declaration);
            EXPECT_TRUE(errors.empty()) << errors.c_str() << xml.c_str();
            ASSERT_TRUE(loaded.value.IsConst()) << xml.c_str();
            EXPECT_TRUE(SameBytes(loaded.value.GetConst(), Case::Literal())) << xml.c_str();
        }
        EXPECT_TRUE(LoadXml(xml, plain, nullptr).empty());
        EXPECT_TRUE(SameBytes(loaded.value.GetConst(), plain.value));
        // A document written by the old plain field is what the value writes
        EXPECT_EQ(SaveXml(loaded), xml);
    }

    TYPED_TEST(ValueMatrixTest, AScalarConstIsOnePlainAttributeAndAnObjectKeepsItsOwnElement) {
        using Case = MatrixCase<TypeParam>;
        ValueHolder<TypeParam> saved;
        saved.value = Value<TypeParam>::Const(Case::Literal());
        const String xml = SaveXml(saved);
        const String attribute = Format("{}=\"", kKey);
        const String element = Format("<{}", kKey);
        if constexpr (Ser::Detail::IsAttributeValue<TypeParam>) {
            EXPECT_TRUE(HasText(xml, attribute)) << xml.c_str();
            EXPECT_FALSE(HasText(xml, element)) << xml.c_str();
        } else {
            EXPECT_TRUE(HasText(xml, element)) << xml.c_str();
        }
    }

    // =========================================================================
    // Linking errors
    // =========================================================================

    TYPED_TEST(ValueMatrixTest, AnUnknownFieldNamesThePathAndTheFieldsTheTypeHas) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        const String path = Format("caster.{}x", Case::kField);
        Value<TypeParam> value = Value<TypeParam>::Const(Case::Other());
        const auto linked = value.Link(world.declaration, path);
        ASSERT_FALSE(linked.has_value());
        EXPECT_EQ(linked.error().code, LinkErrorCode::unknown_field);
        EXPECT_TRUE(HasText(linked.error().message, path)) << linked.error().message.c_str();
        EXPECT_TRUE(HasText(linked.error().message, "ValueUnitStats")) << linked.error().message.c_str();
        EXPECT_TRUE(HasText(linked.error().message, Case::kField)) << linked.error().message.c_str();
        EXPECT_TRUE(SameBytes(value.GetConst(), Case::Other()));
        EXPECT_TRUE(value.IsConst());
    }

    TYPED_TEST(ValueMatrixTest, APathBehindAListIsNotFixed) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        Value<TypeParam> value = Value<TypeParam>::Const(Case::Other());
        const auto linked = value.Link(world.declaration, "caster.buffs[0]");
        ASSERT_FALSE(linked.has_value());
        EXPECT_EQ(linked.error().code, LinkErrorCode::not_fixed);
        EXPECT_TRUE(HasText(linked.error().message, "caster.buffs[0]")) << linked.error().message.c_str();
        EXPECT_TRUE(HasText(linked.error().message, "list element")) << linked.error().message.c_str();
        EXPECT_TRUE(value.IsConst());
    }

    TYPED_TEST(ValueMatrixTest, AFieldOfAnotherTypeIsAMismatchThatNamesBothTypes) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        Value<TypeParam> value = Value<TypeParam>::Const(Case::Other());
        const auto linked = value.Link(world.declaration, Case::kWrongPath);
        ASSERT_FALSE(linked.has_value());
        EXPECT_EQ(linked.error().code, LinkErrorCode::type_mismatch);
        const String& message = linked.error().message;
        EXPECT_TRUE(HasText(message, Case::kWrongPath)) << message.c_str();
        EXPECT_TRUE(HasText(message, Case::kWrongField)) << message.c_str();
        EXPECT_TRUE(HasText(message, TypeText(DescribeType<TypeParam>()))) << message.c_str();
        EXPECT_TRUE(HasText(message, "ValueUnitStats")) << message.c_str();
        EXPECT_TRUE(value.IsConst());
        EXPECT_TRUE(SameBytes(value.GetConst(), Case::Other()));
    }

    TYPED_TEST(ValueMatrixTest, ALoadOfARefusedPathReportsItUnderTheFieldAndKeepsTheField) {
        using Case = MatrixCase<TypeParam>;
        const ValueWorld world;
        const String unknown = Format("caster.{}x", Case::kField);
        const String paths[] = {unknown, String("caster.buffs[0]"), String(Case::kWrongPath)};
        for (const String& path : paths) {
            ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
            const String xml = Format("<root><State><{} from=\"{}\"/></State></root>", kKey, path);
            const String errors = LoadXml(xml, loaded, &world.declaration);
            EXPECT_TRUE(HasText(errors, "State.v.from")) << errors.c_str() << xml.c_str();
            EXPECT_TRUE(HasText(errors, path)) << errors.c_str();
            ExpectUntouched(loaded);
        }
    }

    TYPED_TEST(ValueMatrixTest, AFromWithoutAScopeIsAnErrorThatNamesThePath) {
        using Case = MatrixCase<TypeParam>;
        ValueHolder<TypeParam> loaded = PreloadedHolder<TypeParam>();
        const String errors = LoadXml(Format("<root><State><{} from=\"{}\"/></State></root>", kKey, Case::kPath), loaded, nullptr);
        EXPECT_TRUE(HasText(errors, "No context is declared")) << errors.c_str();
        EXPECT_TRUE(HasText(errors, Case::kPath)) << errors.c_str();
        ExpectUntouched(loaded);
    }

    // =========================================================================
    // Allocations
    // =========================================================================

    TYPED_TEST(ValueMatrixTest, AGetOfEitherKindAllocatesNothing) {
        if (!AllocTracker::kEnabled) {
            GTEST_SKIP() << "the allocation counters are off in this preset (ENGINE_PROFILER_ALLOC)";
        }
        using Case = MatrixCase<TypeParam>;
        constexpr int kReads = 100;
        const ValueWorld world;
        const ValueHolder<TypeParam> slot = SlotHolder<TypeParam>(world, Case::kPath);
        const Value<TypeParam> literal = Value<TypeParam>::Const(Case::Literal());
        int same = 0;
        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        for (int i = 0; i < kReads; ++i) {
            same += SameBytes(slot.value.Get(world.context), world.caster.*Case::kMember) ? 1 : 0;
            same += SameBytes(literal.Get(world.context), Case::Literal()) ? 1 : 0;
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();
        EXPECT_EQ(after.count, before.count);
        EXPECT_EQ(same, 2 * kReads);
    }

    // =========================================================================
    // Threads
    // =========================================================================

    TEST(ValueMatrixThreadsTest, TwoThreadsLoadingAtOnceEachSeeTheirOwnScope) {
        ContextDecl first;
        ContextDecl second;
        ASSERT_TRUE(first.Add<ValueUnitStats>("caster").has_value());
        ASSERT_TRUE(second.Add<ValueUnitStats>("target").has_value());

        struct Outcome {
            bool ownLevelLoaded = false;
            bool otherLevelRefused = false;
            bool metOther = false;
        };
        std::atomic<int> arrived{0};
        const auto work = [&arrived](const ContextDecl& declaration, StringView own, StringView other, Outcome& outcome) {
            ValueLinkScope scope(declaration);
            arrived.fetch_add(1);
            const auto deadline = std::chrono::steady_clock::now() + kRendezvousLimit;
            while (arrived.load() < kLoadingThreads && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            outcome.metOther = arrived.load() == kLoadingThreads;
            for (int round = 0; round < 50; ++round) {
                ValueHolder<int32_t> ownHolder;
                ValueHolder<int32_t> otherHolder;
                XmlDeserializer ownReader;
                XmlDeserializer otherReader;
                ownReader.LoadFromString(Format("<root><State><v from=\"{}.attack\"/></State></root>", own));
                otherReader.LoadFromString(Format("<root><State><v from=\"{}.attack\"/></State></root>", other));
                ownReader.BeginObject(kState);
                ownHolder.Deserialize(ownReader);
                ownReader.EndObject();
                otherReader.BeginObject(kState);
                otherHolder.Deserialize(otherReader);
                otherReader.EndObject();
                outcome.ownLevelLoaded = ownHolder.value.IsSlot() && !ownReader.HasErrors();
                outcome.otherLevelRefused = otherHolder.value.IsConst() && otherReader.HasErrors();
                if (!outcome.ownLevelLoaded || !outcome.otherLevelRefused) {
                    break;
                }
            }
        };

        Outcome a;
        Outcome b;
        {
            Thread threadA("ValueMatrix A", [&] { work(first, "caster", "target", a); });
            Thread threadB("ValueMatrix B", [&] { work(second, "target", "caster", b); });
            threadA.join();
            threadB.join();
        }
        EXPECT_TRUE(a.metOther);
        EXPECT_TRUE(b.metOther);
        EXPECT_TRUE(a.ownLevelLoaded);
        EXPECT_TRUE(a.otherLevelRefused);
        EXPECT_TRUE(b.ownLevelLoaded);
        EXPECT_TRUE(b.otherLevelRefused);
        EXPECT_EQ(ValueLinkScope::Current(), nullptr);
    }

}  // namespace BECore::Tests
