#include "PropertyVisitorContainersTest.h"

#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <Common/Printers.h>
#include <Generated/PropertyVisitorContainersTest.gen.hpp>

namespace BECore::Tests {

    using namespace VisitorContainersTestData;

    namespace {

        /// Every Visit does nothing and no hook is overridden: the shape of a visitor that knows no container hook
        class NullVisitor : public IPropertyVisitor {
        public:
            bool Visit(StringView, bool&) override {
                return false;
            }
            bool Visit(StringView, int8_t&) override {
                return false;
            }
            bool Visit(StringView, uint8_t&) override {
                return false;
            }
            bool Visit(StringView, int16_t&) override {
                return false;
            }
            bool Visit(StringView, uint16_t&) override {
                return false;
            }
            bool Visit(StringView, int32_t&) override {
                return false;
            }
            bool Visit(StringView, uint32_t&) override {
                return false;
            }
            bool Visit(StringView, int64_t&) override {
                return false;
            }
            bool Visit(StringView, uint64_t&) override {
                return false;
            }
            bool Visit(StringView, float&) override {
                return false;
            }
            bool Visit(StringView, double&) override {
                return false;
            }
            bool Visit(StringView, PoolString&) override {
                return false;
            }
            bool Visit(StringView, String&) override {
                return false;
            }
        };

        /// Adds one to every int32_t and overrides no hook at all
        class IncrementingVisitor final : public NullVisitor {
        public:
            using NullVisitor::Visit;
            bool Visit(StringView, int32_t& value) override {
                ++value;
                return true;
            }
        };

        /// Overrides the compound hooks only: a container must reach it as a compound of its elements
        class CompoundVisitor final : public NullVisitor {
        public:
            using NullVisitor::Visit;

            String trace;

            bool Visit(StringView name, int32_t& value) override {
                AppendFormat(trace, "{}={} ", name, value);
                return false;
            }
            bool BeginCompound(StringView name) override {
                AppendFormat(trace, "<{}> ", name);
                return true;
            }
            void EndCompound() override {
                trace += "</> ";
            }
        };

        /// What a value visit saw: its label and the attributes of the innermost field open at that moment
        struct SeenValue {
            String label;
            FieldAttributes attributes;
        };

        /**
         * @brief Logs every call as a token and answers the hooks as scripted
         *
         * Tokens: `{` `}` a field, `<name>` `</>` a compound, `name(size+-)` a list (`+` can add, `-` can remove), `~i` the end of
         * element i, `key?` / `key?n` a new key, `/list` its end, `name=value` a value, `name?1` a presence, `name#i/n` an alternative.
         */
        class Recorder final : public NullVisitor {
        public:
            using NullVisitor::Visit;

            bool showsHidden = false;
            bool add = false;
            Optional<size_t> removeAt;
            String newTextKey;
            Optional<int64_t> newIntegerKey;
            Optional<bool> presence;
            Optional<size_t> alternative;

            Vector<SeenValue> seen;

            String Trace() const {
                String text;
                for (const String& token : _tokens) {
                    if (!text.empty()) {
                        text += ' ';
                    }
                    text += token;
                }
                return text;
            }

            void Clear() {
                _tokens.clear();
                seen.clear();
            }

            void BeginField(const FieldAttributes& attributes) override {
                _fields.push_back(attributes);
                _tokens.push_back("{");
            }
            void EndField() override {
                _fields.pop_back();
                _tokens.push_back("}");
            }
            bool ShowsHidden() const override {
                return showsHidden;
            }
            bool Visit(StringView name, bool& value) override {
                return Value(name, value);
            }
            bool Visit(StringView name, int32_t& value) override {
                return Value(name, value);
            }
            bool Visit(StringView name, float& value) override {
                return Value(name, value);
            }
            bool Visit(StringView name, String& value) override {
                return Value(name, StringView(value.data(), value.size()));
            }
            bool BeginCompound(StringView name) override {
                _tokens.push_back(Format("<{}>", name));
                return true;
            }
            void EndCompound() override {
                _tokens.push_back("</>");
            }
            bool BeginList(StringView name, const ListInfo& info) override {
                _tokens.push_back(Format("{}({}{}{})", name, info.size, info.canAdd ? "+" : "", info.canRemove ? "-" : ""));
                return true;
            }
            void EndListElement(size_t index, ListRequest& request) override {
                _tokens.push_back(Format("~{}", index));
                if (removeAt == index) {
                    request = {ListAction::Remove, index};
                }
            }
            void EndList(ListRequest& request) override {
                _tokens.push_back("/list");
                if (add) {
                    request.action = ListAction::Add;
                }
            }
            void VisitNewKey(String& key) override {
                _tokens.push_back("key?");
                key = newTextKey;
            }
            void VisitNewKey(int64_t& key) override {
                _tokens.push_back(Format("key?{}", key));
                if (newIntegerKey) {
                    key = *newIntegerKey;
                }
            }
            bool VisitPresence(StringView name, bool& present) override {
                _tokens.push_back(Format("{}?{}", name, present ? 1 : 0));
                if (presence) {
                    present = *presence;
                    return true;
                }
                return false;
            }
            bool VisitAlternative(StringView name, size_t count, size_t& index) override {
                _tokens.push_back(Format("{}#{}/{}", name, index, count));
                if (alternative) {
                    index = *alternative;
                    return true;
                }
                return false;
            }

        private:
            template <typename T>
            bool Value(StringView name, const T& value) {
                _tokens.push_back(Format("{}={}", name, value));
                seen.push_back({String(name.data(), name.size()), _fields.empty() ? FieldAttributes{} : _fields.back()});
                return false;
            }

            Vector<String> _tokens;
            Vector<FieldAttributes> _fields;
        };

    }  // namespace

    // ---- the order of the calls --------------------------------------------------------------------------------------------

    TEST(PropertyVisitorContainersTest, AVectorOfNumbersIsAListOfItsElements) {
        Vector<int32_t> list{1, 2};
        Recorder recorder;
        EXPECT_FALSE(PropVisit::Visit(recorder, list, "list"));
        EXPECT_EQ(recorder.Trace(), "list(2+-) [0]=1 ~0 [1]=2 ~1 /list");
    }

    TEST(PropertyVisitorContainersTest, TheElementsOfAVectorOfObjectsHaveTheirOwnFields) {
        Vector<ContainerItem> items(2);
        items[0].id = 1;
        items[1].id = 2;
        Recorder recorder;
        PropVisit::Visit(recorder, items, "items");
        EXPECT_EQ(recorder.Trace(), "items(2+-) <[0]> { id=1 } </> ~0 <[1]> { id=2 } </> ~1 /list");
    }

    TEST(PropertyVisitorContainersTest, TheValuesOfAMapAreLabelledByTheirKeys) {
        Map<String, float> weights;
        weights["b"] = 2.0f;
        weights["a"] = 1.5f;
        Recorder recorder;
        PropVisit::Visit(recorder, weights, "weights");
        EXPECT_EQ(recorder.Trace(), "weights(2+-) a=1.5 ~0 b=2 ~1 key? /list");
    }

    TEST(PropertyVisitorContainersTest, AnIntegerKeyIsItsNumberAndTheNewKeyIsTheFirstFreeOne) {
        Map<int32_t, String> names;
        names[3] = "c";
        names[0] = "z";
        Recorder recorder;
        PropVisit::Visit(recorder, names, "names");
        EXPECT_EQ(recorder.Trace(), "names(2+-) 0=z ~0 3=c ~1 key?1 /list");
    }

    TEST(PropertyVisitorContainersTest, AnEnumKeyIsItsNameAndGivesNoAdd) {
        Map<Shade, int32_t> shades;
        shades[Shade::Dark] = 2;
        shades[Shade::Light] = 1;
        Recorder recorder;
        recorder.add = true;
        EXPECT_FALSE(PropVisit::Visit(recorder, shades, "shades"));
        EXPECT_EQ(recorder.Trace(), "shades(2-) Light=1 ~0 Dark=2 ~1 /list");
        EXPECT_EQ(shades.size(), 2u);
    }

    TEST(PropertyVisitorContainersTest, AnArrayCanNeitherAddNorRemove) {
        Array<int32_t, 2> twin{{4, 5}};
        Recorder recorder;
        PropVisit::Visit(recorder, twin, "twin");
        EXPECT_EQ(recorder.Trace(), "twin(2) [0]=4 ~0 [1]=5 ~1 /list");
    }

    TEST(PropertyVisitorContainersTest, APairIsACompoundOfFirstAndSecond) {
        Pair<int32_t, String> entry{11, "eleven"};
        Recorder recorder;
        PropVisit::Visit(recorder, entry, "entry");
        EXPECT_EQ(recorder.Trace(), "<entry> first=11 second=eleven </>");
    }

    TEST(PropertyVisitorContainersTest, AnOptionalShowsItsPresenceThenItsValueWhenItHasOne) {
        Optional<int32_t> engaged = 4;
        Optional<int32_t> empty;
        Recorder recorder;
        PropVisit::Visit(recorder, engaged, "engaged");
        PropVisit::Visit(recorder, empty, "empty");
        EXPECT_EQ(recorder.Trace(), "engaged?1 engaged=4 empty?0");
    }

    TEST(PropertyVisitorContainersTest, AVariantShowsItsAlternativeThenTheActiveValue) {
        Variant<std::monostate, int32_t, ContainerItem> choice = 7;
        Recorder recorder;
        PropVisit::Visit(recorder, choice, "choice");
        EXPECT_EQ(recorder.Trace(), "<choice> choice#1/3 value=7 </>");

        recorder.Clear();
        ContainerItem item;
        item.id = 3;
        choice = item;
        PropVisit::Visit(recorder, choice, "choice");
        EXPECT_EQ(recorder.Trace(), "<choice> choice#2/3 <value> { id=3 } </> </>");

        recorder.Clear();
        choice = std::monostate{};
        PropVisit::Visit(recorder, choice, "choice");
        EXPECT_EQ(recorder.Trace(), "<choice> choice#0/3 </>");
    }

    TEST(PropertyVisitorContainersTest, TheElementsOfAContainerInsideAContainerAreVisited) {
        Vector<Vector<int32_t>> nested{{1}, {2, 3}};
        Recorder recorder;
        PropVisit::Visit(recorder, nested, "nested");
        EXPECT_EQ(recorder.Trace(), "nested(2+-) [0](1+-) [0]=1 ~0 /list ~0 [1](2+-) [0]=2 ~0 [1]=3 ~1 /list ~1 /list");

        recorder.Clear();
        Map<String, Optional<int32_t>> maybes;
        maybes["a"] = 1;
        Vector<Variant<int32_t, String>> mixed{Variant<int32_t, String>{2}};
        PropVisit::Visit(recorder, maybes, "maybes");
        PropVisit::Visit(recorder, mixed, "mixed");
        EXPECT_EQ(recorder.Trace(), "maybes(1+-) a?1 a=1 ~0 key? /list mixed(1+-) <[0]> [0]#0/2 value=2 </> ~0 /list");
    }

    TEST(PropertyVisitorContainersTest, AListTheVisitorDoesNotOpenIsNotVisitedOrChanged) {
        class Closed final : public NullVisitor {
        public:
            using NullVisitor::Visit;
            int32_t visits = 0;
            int32_t ends = 0;
            bool Visit(StringView, int32_t&) override {
                ++visits;
                return false;
            }
            bool BeginList(StringView, const ListInfo&) override {
                return false;
            }
            void EndList(ListRequest& request) override {
                ++ends;
                request.action = ListAction::Add;
            }
        };
        Vector<int32_t> list{1};
        Closed closed;
        EXPECT_FALSE(PropVisit::Visit(closed, list, "list"));
        EXPECT_EQ(closed.visits, 0);
        EXPECT_EQ(closed.ends, 0);
        EXPECT_EQ(list.size(), 1u);
    }

    // ---- requests ------------------------------------------------------------------------------------------------------------

    TEST(PropertyVisitorContainersTest, AnAddAppendsOneDefaultElementAfterTheLoop) {
        Vector<int32_t> list{1};
        Recorder recorder;
        recorder.add = true;
        EXPECT_TRUE(PropVisit::Visit(recorder, list, "list"));
        ASSERT_EQ(list.size(), 2u);
        EXPECT_EQ(list[1], 0);
        // The new element is not visited in the pass that added it
        EXPECT_EQ(recorder.Trace(), "list(1+-) [0]=1 ~0 /list");
    }

    TEST(PropertyVisitorContainersTest, ARemoveTakesOutTheElementAtItsIndex) {
        Vector<int32_t> list{1, 2, 3};
        Recorder recorder;
        recorder.removeAt = 1;
        EXPECT_TRUE(PropVisit::Visit(recorder, list, "list"));
        EXPECT_EQ(list, (Vector<int32_t>{1, 3}));
    }

    TEST(PropertyVisitorContainersTest, AFullFixedVectorIgnoresAnAddAndASmallVectorGrows) {
        FixedVector<int32_t, 2> fixed{1, 2};
        SmallVector<int32_t, 2> small{1, 2};
        Recorder recorder;
        recorder.add = true;
        EXPECT_FALSE(PropVisit::Visit(recorder, fixed, "fixed"));
        EXPECT_TRUE(PropVisit::Visit(recorder, small, "small"));
        EXPECT_EQ(fixed.size(), 2u);
        EXPECT_EQ(small.size(), 3u);
        EXPECT_EQ(recorder.Trace(), "fixed(2-) [0]=1 ~0 [1]=2 ~1 /list small(2+-) [0]=1 ~0 [1]=2 ~1 /list");

        FixedVector<int32_t, 2> roomy{1};
        EXPECT_TRUE(PropVisit::Visit(recorder, roomy, "roomy"));
        EXPECT_EQ(roomy.size(), 2u);
    }

    TEST(PropertyVisitorContainersTest, AnArrayIgnoresAnAddAndARemove) {
        Array<int32_t, 2> twin{{4, 5}};
        Recorder recorder;
        recorder.add = true;
        EXPECT_FALSE(PropVisit::Visit(recorder, twin, "twin"));
        recorder.add = false;
        recorder.removeAt = 0;
        EXPECT_FALSE(PropVisit::Visit(recorder, twin, "twin"));
        EXPECT_EQ(twin[0], 4);
        EXPECT_EQ(twin[1], 5);
    }

    TEST(PropertyVisitorContainersTest, AnAddToAMapAddsTheKeyOfTheVisitorOnceAndNeverAKeyItHas) {
        Map<String, float> weights;
        weights["a"] = 1.0f;
        Recorder recorder;
        recorder.add = true;
        recorder.newTextKey = "c";
        EXPECT_TRUE(PropVisit::Visit(recorder, weights, "weights"));
        ASSERT_EQ(weights.size(), 2u);
        EXPECT_EQ(weights.at("c"), 0.0f);

        recorder.newTextKey = "a";
        EXPECT_FALSE(PropVisit::Visit(recorder, weights, "weights"));
        EXPECT_EQ(weights.size(), 2u);
        EXPECT_EQ(weights.at("a"), 1.0f);
    }

    TEST(PropertyVisitorContainersTest, AnAddToAnIntegerMapTakesTheProposedKeyOrTheVisitorsOne) {
        HashMap<int32_t, String> names;
        names[0] = "zero";
        Recorder recorder;
        recorder.add = true;
        EXPECT_TRUE(PropVisit::Visit(recorder, names, "names"));
        EXPECT_TRUE(names.contains(1));

        recorder.newIntegerKey = 40;
        EXPECT_TRUE(PropVisit::Visit(recorder, names, "names"));
        EXPECT_TRUE(names.contains(40));

        // A key the key type cannot hold is no add
        Map<uint8_t, int32_t> small;
        recorder.newIntegerKey = -1;
        EXPECT_FALSE(PropVisit::Visit(recorder, small, "small"));
        EXPECT_TRUE(small.empty());
    }

    TEST(PropertyVisitorContainersTest, ARemoveFromAMapTakesTheEntryAtItsPosition) {
        Map<String, float> weights;
        weights["a"] = 1.0f;
        weights["b"] = 2.0f;
        Recorder recorder;
        recorder.removeAt = 0;
        EXPECT_TRUE(PropVisit::Visit(recorder, weights, "weights"));
        ASSERT_EQ(weights.size(), 1u);
        EXPECT_TRUE(weights.contains("b"));
    }

    TEST(PropertyVisitorContainersTest, AFullFixedHashMapThatCannotOverflowHasNoAdd) {
        FixedHashMap<String, int32_t, 1, 2, false> map;
        map["a"] = 1;
        Recorder recorder;
        recorder.add = true;
        recorder.newTextKey = "b";
        EXPECT_FALSE(PropVisit::Visit(recorder, map, "map"));
        EXPECT_EQ(recorder.Trace(), "map(1-) a=1 ~0 /list");
        EXPECT_EQ(map.size(), 1u);
    }

    TEST(PropertyVisitorContainersTest, APresenceChangeEngagesADefaultValueOrResets) {
        Optional<int32_t> maybe;
        Recorder recorder;
        recorder.presence = true;
        EXPECT_TRUE(PropVisit::Visit(recorder, maybe, "maybe"));
        ASSERT_TRUE(maybe.has_value());
        EXPECT_EQ(*maybe, 0);
        EXPECT_EQ(recorder.Trace(), "maybe?0 maybe=0");

        recorder.presence = false;
        EXPECT_TRUE(PropVisit::Visit(recorder, maybe, "maybe"));
        EXPECT_FALSE(maybe.has_value());

        // Asked for what it already is: no change
        EXPECT_FALSE(PropVisit::Visit(recorder, maybe, "maybe"));
    }

    TEST(PropertyVisitorContainersTest, AnotherAlternativeEmplacesItsDefault) {
        Variant<std::monostate, int32_t, ContainerItem> choice = 7;
        Recorder recorder;
        recorder.alternative = 2;
        EXPECT_TRUE(PropVisit::Visit(recorder, choice, "choice"));
        ASSERT_EQ(choice.index(), 2u);
        EXPECT_EQ(std::get<2>(choice).id, 0);

        // The same index or one out of range changes nothing
        EXPECT_FALSE(PropVisit::Visit(recorder, choice, "choice"));
        recorder.alternative = 3;
        EXPECT_FALSE(PropVisit::Visit(recorder, choice, "choice"));
        EXPECT_EQ(choice.index(), 2u);
    }

    // ---- attributes and visitors without the hooks ---------------------------------------------------------------------------

    TEST(PropertyVisitorContainersTest, TheElementsSeeTheAttributesOfTheirContainerField) {
        ContainerFields state;
        state.numbers = {1.0f, 2.0f};
        state.locked = {5};
        Recorder recorder;
        state.AcceptPropertyVisitor(recorder);

        int32_t numbers = 0;
        int32_t locked = 0;
        for (const SeenValue& value : recorder.seen) {
            if (value.attributes.hasRange) {
                ++numbers;
                EXPECT_EQ(value.attributes.rangeMin, 0.0);
                EXPECT_EQ(value.attributes.rangeMax, 10.0);
                EXPECT_EQ(value.attributes.step, 0.5);
            }
            if (value.attributes.readOnly) {
                ++locked;
                EXPECT_EQ(value.label, "[0]");
                EXPECT_EQ(value.attributes.tooltip, "Fixed by the level");
            }
        }
        EXPECT_EQ(numbers, 2);
        EXPECT_EQ(locked, 1);
        EXPECT_NE(recorder.Trace().find("{ numbers(2+-) [0]=1 ~0 [1]=2 ~1 /list }"), String::npos) << recorder.Trace().c_str();
    }

    TEST(PropertyVisitorContainersTest, AHiddenContainerGetsNoCallUnlessTheVisitorShowsHidden) {
        ContainerFields state;
        state.secret = {9};
        Recorder recorder;
        state.AcceptPropertyVisitor(recorder);
        EXPECT_EQ(recorder.Trace().find("secret"), String::npos);
        EXPECT_EQ(recorder.Trace().find("=9"), String::npos);

        recorder.Clear();
        recorder.showsHidden = true;
        state.AcceptPropertyVisitor(recorder);
        EXPECT_NE(recorder.Trace().find("{ secret(1+-) [0]=9 ~0 /list }"), String::npos) << recorder.Trace().c_str();
    }

    TEST(PropertyVisitorContainersTest, EveryFieldOfTheStructIsVisitedInOrder) {
        ContainerFields state;
        state.items.resize(1);
        state.weights["w"] = 1.0f;
        state.names[2] = "two";
        state.hashed["h"] = 3;
        state.twin = {{4, 5}};
        state.small = {6};
        state.fixed = {7};
        state.entry = {8, "eight"};
        state.maybe = 9;
        state.choice = 10;
        Recorder recorder;
        EXPECT_FALSE(state.AcceptPropertyVisitor(recorder));
        EXPECT_EQ(recorder.Trace(), "{ numbers(0+) /list } "
                                    "{ items(1+-) <[0]> { id=0 } </> ~0 /list } "
                                    "{ weights(1+-) w=1 ~0 key? /list } "
                                    "{ names(1+-) 2=two ~0 key?0 /list } "
                                    "{ hashed(1+-) h=3 ~0 key? /list } "
                                    "{ twin(2) [0]=4 ~0 [1]=5 ~1 /list } "
                                    "{ small(1+-) [0]=6 ~0 /list } "
                                    "{ fixed(1+-) [0]=7 ~0 /list } "
                                    "{ <entry> first=8 second=eight </> } "
                                    "{ maybe?1 maybe=9 } "
                                    "{ <choice> choice#1/3 value=10 </> } "
                                    "{ locked(0+) /list }");
    }

    TEST(PropertyVisitorContainersTest, AVisitorWithoutTheHooksEditsEveryElementAndResizesNothing) {
        ContainerFields state;
        state.items.resize(1);
        state.twin = {{4, 5}};
        state.small = {6};
        state.fixed = {7};
        state.entry = {8, "eight"};
        state.maybe = 9;
        state.choice = 10;
        state.locked = {11};
        state.hashed["h"] = 3;
        IncrementingVisitor visitor;
        EXPECT_TRUE(state.AcceptPropertyVisitor(visitor));
        EXPECT_EQ(state.items.size(), 1u);
        EXPECT_EQ(state.items[0].id, 1);
        EXPECT_EQ(state.twin[1], 6);
        EXPECT_EQ(state.small, (SmallVector<int32_t, 2>{7}));
        EXPECT_EQ(state.fixed[0], 8);
        EXPECT_EQ(state.entry.first, 9);
        EXPECT_EQ(state.maybe, Optional<int32_t>{10});
        EXPECT_EQ(std::get<1>(state.choice), 11);
        EXPECT_EQ(state.locked[0], 12);
        EXPECT_EQ(state.hashed.at("h"), 4);
        EXPECT_TRUE(state.weights.empty());

        NullVisitor nothing;
        EXPECT_FALSE(state.AcceptPropertyVisitor(nothing));
    }

    TEST(PropertyVisitorContainersTest, AVisitorWithoutTheListHooksSeesAListAsACompound) {
        Vector<int32_t> list{1, 2};
        CompoundVisitor visitor;
        EXPECT_FALSE(PropVisit::Visit(visitor, list, "list"));
        EXPECT_EQ(visitor.trace, "<list> [0]=1 [1]=2 </> ");
    }

}  // namespace BECore::Tests
